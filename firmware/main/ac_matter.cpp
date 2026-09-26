#include "ac_matter.h"

#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>

#include <app/EventLogging.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <esp_openthread_types.h>
#include <esp_openthread.h>
#include <esp_openthread_lock.h>
#include <openthread/platform/radio.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#include <platform/ThreadStackManager.h>

/* ESP-IDF only ships these as example headers, and esp-matter gets them from
 * examples/common - which this project deliberately does not pull in (it
 * depends on espressif/button from the registry).  Spelling them out here is
 * three lines and removes the dependency. */
#define AC_OT_RADIO_CONFIG()  { .radio_mode = RADIO_MODE_NATIVE }
#define AC_OT_HOST_CONFIG()   { .host_connection_mode = HOST_CONNECTION_MODE_NONE }
#define AC_OT_PORT_CONFIG()   { .storage_partition_name = "nvs", \
                                .netif_queue_size = 10,          \
                                .task_queue_size = 10 }
#endif

#include <cinttypes>
#include <cmath>
#include <cstring>

static const char *TAG = "ac_matter";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

/* Matter's concentration measurement clusters carry a MeasurementUnitEnum.
 * Spec 1.4, 2.10.6.3: 0 PPM, 1 PPB, 2 PPT, 3 MGM3, 4 UGM3, 5 NGM3, 6 PM3,
 * 7 BQM3.  Particulates are reported in ug/m3 and CO2 in ppm, which is what
 * the sensors actually produce. */
enum : uint8_t { UNIT_PPM = 0, UNIT_PPB = 1, UNIT_UGM3 = 4 };
/* MeasurementMediumEnum: 0 Air, 1 Water, 2 Soil. */
enum : uint8_t { MEDIUM_AIR = 0 };

static uint16_t s_ep_air = 0, s_ep_temp = 0, s_ep_hum = 0, s_ep_wired = 0;
static ac_engine_t *s_engine = nullptr;
static void (*s_identify_cb)(bool on) = nullptr;
static bool s_commissioned = false;

/* last value written, so we never write the same number twice */
static float s_last[16];

static esp_err_t write_float(uint16_t ep, uint32_t cluster, uint32_t attr,
                             float value, int slot)
{
    if (!(value >= 0.0f)) return ESP_OK;         /* not measured yet */
    if (std::fabs(value - s_last[slot]) < 1e-3f) return ESP_OK;
    s_last[slot] = value;
    esp_matter_attr_val_t val = esp_matter_nullable_float(value);
    return attribute::update(ep, cluster, attr, &val);
}

static esp_err_t write_i16(uint16_t ep, uint32_t cluster, uint32_t attr,
                           int16_t value)
{
    esp_matter_attr_val_t val = esp_matter_nullable_int16(value);
    return attribute::update(ep, cluster, attr, &val);
}

static esp_err_t write_u16(uint16_t ep, uint32_t cluster, uint32_t attr,
                           uint16_t value)
{
    esp_matter_attr_val_t val = esp_matter_nullable_uint16(value);
    return attribute::update(ep, cluster, attr, &val);
}

static esp_err_t write_u8(uint16_t ep, uint32_t cluster, uint32_t attr,
                          uint8_t value)
{
    esp_matter_attr_val_t val = esp_matter_uint8(value);
    return attribute::update(ep, cluster, attr, &val);
}

/* Every concentration cluster shares one config type and differs only in its
 * create() entry point, so this is a switch over five one-line calls rather
 * than five near-identical blocks. */
/* 24 h: the window the dashboard's "peak today" and "average today" read. */
static constexpr uint32_t k_stat_window_s = 24u * 3600u;

static void add_concentration(endpoint_t *ep, uint32_t cluster_id,
                              uint8_t unit, float lo, float hi, bool stats)
{
    using namespace esp_matter::cluster;
    namespace f = concentration_measurement::feature;
    concentration_measurement::config_t cfg;
    cfg.measurement_medium = MEDIUM_AIR;
    /* NumericMeasurement, plus PeakMeasurement and AverageMeasurement where
     * `stats` is set.  Those two are standard features of the concentration
     * clusters and exist precisely so a controller - here the e-ink dashboard
     * - can show "peak today" without keeping its own history, which matters
     * for a dashboard that sleeps most of the time.  LevelIndication is left
     * off: a five step enum derived from a measured ug/m3 adds nothing the Air
     * Quality cluster on this endpoint does not already carry. */
    cfg.feature_flags = f::numeric_measurement::get_id();
    cfg.features.numeric_measurement.measurement_unit = unit;
    cfg.features.numeric_measurement.min_measured_value = lo;
    cfg.features.numeric_measurement.max_measured_value = hi;
    if (stats) {
        cfg.feature_flags |= f::peak_measurement::get_id() |
                             f::average_measurement::get_id();
        cfg.features.peak_measurement.peak_measured_value_window = k_stat_window_s;
        cfg.features.average_measurement.average_measured_value_window = k_stat_window_s;
    }

    cluster_t *c = nullptr;
    switch (cluster_id) {
    case Pm25ConcentrationMeasurement::Id:
        c = pm25_concentration_measurement::create(ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    case Pm10ConcentrationMeasurement::Id:
        c = pm10_concentration_measurement::create(ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    case Pm1ConcentrationMeasurement::Id:
        c = pm1_concentration_measurement::create(ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    case CarbonDioxideConcentrationMeasurement::Id:
        c = carbon_dioxide_concentration_measurement::create(ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    case TotalVolatileOrganicCompoundsConcentrationMeasurement::Id:
        c = total_volatile_organic_compounds_concentration_measurement::create(
                ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    default:
        break;
    }
    if (!c)
        ESP_LOGE(TAG, "concentration cluster 0x%04" PRIX32 " was not created",
                 cluster_id);
}

static esp_err_t attribute_update_cb(attribute::callback_type_t type,
                                     uint16_t endpoint_id, uint32_t cluster_id,
                                     uint32_t attribute_id,
                                     esp_matter_attr_val_t *val, void *priv)
{
    (void)type; (void)endpoint_id; (void)cluster_id; (void)attribute_id;
    (void)val; (void)priv;
    /* This device is read only over Matter: every attribute it exposes is a
     * measurement.  Nothing a controller writes changes what we measure. */
    return ESP_OK;
}

static esp_err_t identification_cb(identification::callback_type_t type,
                                   uint16_t endpoint_id, uint8_t effect_id,
                                   uint8_t effect_variant, void *priv)
{
    (void)endpoint_id; (void)effect_id; (void)effect_variant; (void)priv;
    /* Identify blinks the status LED white - the way to tell two identical
     * units apart from the Home app or from the dashboard. */
    ESP_LOGI(TAG, "identify: type %d", (int)type);
    if (s_identify_cb) s_identify_cb(type == identification::callback_type_t::START);
    return ESP_OK;
}

static void device_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg;
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "commissioning complete");
        s_commissioned = true;
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "commissioning window open");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGW(TAG, "fabric removed");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            s_commissioned = false;
            auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            if (!mgr.IsCommissioningWindowOpen()) {
                (void)mgr.OpenBasicCommissioningWindow(
                    chip::System::Clock::Seconds16(300),
                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
            }
        }
        break;
    default:
        break;
    }
}

esp_err_t ac_matter_init(ac_engine_t *engine)
{
    s_engine = engine;
    for (unsigned i = 0; i < sizeof(s_last) / sizeof(s_last[0]); i++)
        s_last[i] = -1.0f;

    node::config_t node_config;
    node_t *node = node::create(&node_config, attribute_update_cb, identification_cb);
    if (!node) return ESP_FAIL;

    /* ---- endpoint 1: air quality ---------------------------------- */
    air_quality_sensor::config_t aq_config;
    endpoint_t *ep = air_quality_sensor::create(node, &aq_config,
                                                ENDPOINT_FLAG_NONE, nullptr);
    if (!ep) return ESP_FAIL;
    s_ep_air = endpoint::get_id(ep);

    /* The Air Quality cluster's optional features decide which enum values we
     * are allowed to report.  We add Fair, Moderate and VeryPoor so the four
     * device states map onto distinct values; ExtremelyPoor is left out
     * because nothing in this device's thresholds would ever produce it. */
    cluster_t *aq = cluster::get(ep, AirQuality::Id);
    if (aq) {
        cluster::air_quality::feature::fair::add(aq);
        cluster::air_quality::feature::moderate::add(aq);
        cluster::air_quality::feature::very_poor::add(aq);
    }

    add_concentration(ep, Pm25ConcentrationMeasurement::Id, UNIT_UGM3, 0.0f, 1000.0f, true);
    add_concentration(ep, Pm10ConcentrationMeasurement::Id, UNIT_UGM3, 0.0f, 1000.0f, true);
    add_concentration(ep, Pm1ConcentrationMeasurement::Id, UNIT_UGM3, 0.0f, 1000.0f, false);
    add_concentration(ep, CarbonDioxideConcentrationMeasurement::Id, UNIT_PPM,
                      400.0f, 5000.0f, true);
    /* See docs/MATTER.md: the SGP40 produces a VOC *Index*, not a
     * concentration.  Publishing it in a concentration cluster is a
     * compromise, taken because without a number Apple Home cannot build a
     * VOC automation at all.  The unit is declared as PPB and the
     * documentation says in as many words that the value is an index. */
    add_concentration(ep, TotalVolatileOrganicCompoundsConcentrationMeasurement::Id,
                      UNIT_PPB, 0.0f, 500.0f, true);

    /* ---- endpoints 2 and 3: temperature and humidity -------------- */
    temperature_sensor::config_t t_config;
    endpoint_t *ep_t = temperature_sensor::create(node, &t_config,
                                                  ENDPOINT_FLAG_NONE, nullptr);
    if (!ep_t) return ESP_FAIL;
    s_ep_temp = endpoint::get_id(ep_t);

    humidity_sensor::config_t h_config;
    endpoint_t *ep_h = humidity_sensor::create(node, &h_config,
                                               ENDPOINT_FLAG_NONE, nullptr);
    if (!ep_h) return ESP_FAIL;
    s_ep_hum = endpoint::get_id(ep_h);

    /* ---- endpoint 0: power source --------------------------------- */
    endpoint_t *root = endpoint::get(node, 0);
    if (root) {
        cluster::power_source::config_t ps;
        ps.status = 1;                  /* Active */
        ps.order = 1;                   /* after the USB-C source (order 0) */
        strncpy(ps.description, "LiFePO4 1S4P", sizeof(ps.description) - 1);

        /* The generated create() validates that exactly one of Wired and
         * Battery is in the feature map and adds the features itself, so the
         * flags have to be set here rather than by calling feature::add()
         * afterwards.  Since v1.4: 1S4P LiFePO4, charged in the device by the
         * power module and held in two Keystone 1049 holders - Battery,
         * Rechargeable and Replaceable. */
        ps.feature_flags = cluster::power_source::feature::battery::get_id() |
                           cluster::power_source::feature::rechargeable::get_id() |
                           cluster::power_source::feature::replaceable::get_id();
        ps.features.battery.bat_charge_level = 0;        /* OK */
        ps.features.battery.bat_replacement_needed = false;
        ps.features.battery.bat_replaceability = 2;      /* UserReplaceable */
        ps.features.rechargeable.bat_charge_state = 0;   /* Unknown until read */
        ps.features.rechargeable.bat_functional_while_charging = true;
        strncpy(ps.features.replaceable.bat_replacement_description,
                "4 x AER18650m2A2 LiFePO4, all at once",
                sizeof(ps.features.replaceable.bat_replacement_description) - 1);
        ps.features.replaceable.bat_quantity = 4;

        cluster_t *psc = cluster::power_source::create(root, &ps, CLUSTER_FLAG_SERVER);
        if (psc) {
            /* Optional attributes the features do not create for us. */
            cluster::power_source::attribute::create_bat_present(psc, true);
            /* PowerSource/Enums.h: BatCommonDesignationEnum::k18650 = 0x4C,
             * BatApprovedChemistryEnum::kLithiumIronPhosphate = 0x14 */
            cluster::power_source::attribute::create_bat_common_designation(psc, 0x4C, 0, 80);
            cluster::power_source::attribute::create_bat_approved_chemistry(psc, 0x14, 0, 32);
            cluster::power_source::attribute::create_bat_percent_remaining(
                psc, nullable<uint8_t>(), nullable<uint8_t>(0),
                nullable<uint8_t>(200));
            cluster::power_source::attribute::create_bat_voltage(
                psc, nullable<uint32_t>(), nullable<uint32_t>(0),
                nullable<uint32_t>(4000));
            /* Charge faults go out as the BatChargeFaultChange event: the
             * SDK's Power Source server only serves EndpointList itself, so a
             * list attribute like ActiveBatChargeFaults would have no one to
             * answer it (docs/MATTER.md). */
            cluster::power_source::event::create_bat_charge_fault_change(psc);
        } else {
            ESP_LOGE(TAG, "power source cluster was not created");
        }
    }

    /* The USB-C input as a second, wired power source on its own endpoint
     * (one Power Source cluster per endpoint).  Order 0: preferred. */
    {
        endpoint::power_source::config_t w;
        w.power_source.status = 3;                       /* Unavailable until read */
        w.power_source.order = 0;
        strncpy(w.power_source.description, "USB-C",
                sizeof(w.power_source.description) - 1);
        w.power_source.feature_flags = cluster::power_source::feature::wired::get_id();
        w.power_source.features.wired.wired_current_type = 1;   /* DC */
        endpoint_t *ep_w = endpoint::power_source::create(node, &w, ENDPOINT_FLAG_NONE, NULL);
        if (ep_w) {
            s_ep_wired = endpoint::get_id(ep_w);
            cluster_t *wc = cluster::get(ep_w, PowerSource::Id);
            if (wc) cluster::power_source::attribute::create_wired_present(wc, false);
        } else {
            ESP_LOGE(TAG, "wired power source endpoint was not created");
        }
    }

    ESP_LOGI(TAG, "endpoints: air=%u temperature=%u humidity=%u usb-c=%u",
             s_ep_air, s_ep_temp, s_ep_hum, s_ep_wired);
    return ESP_OK;
}

esp_err_t ac_matter_start(void)
{
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    esp_openthread_platform_config_t config = {
        .radio_config = AC_OT_RADIO_CONFIG(),
        .host_config = AC_OT_HOST_CONFIG(),
        .port_config = AC_OT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif
    return esp_matter::start(device_event_cb);
}

static void write_stat(uint32_t cluster, uint32_t peak_attr, uint32_t avg_attr,
                       ac_window_stat_t w, int slot)
{
    if (!w.valid) return;
    write_float(s_ep_air, cluster, peak_attr, w.peak, slot);
    write_float(s_ep_air, cluster, avg_attr, w.mean, slot + 1);
}

static void publish_stats(const ac_engine_t *e)
{
    const ac_history_t *h = &e->hist;
    write_stat(Pm25ConcentrationMeasurement::Id,
               Pm25ConcentrationMeasurement::Attributes::PeakMeasuredValue::Id,
               Pm25ConcentrationMeasurement::Attributes::AverageMeasuredValue::Id,
               ac_history_window(h, AC_CH_PM25, k_stat_window_s), 8);
    write_stat(Pm10ConcentrationMeasurement::Id,
               Pm10ConcentrationMeasurement::Attributes::PeakMeasuredValue::Id,
               Pm10ConcentrationMeasurement::Attributes::AverageMeasuredValue::Id,
               ac_history_window(h, AC_CH_PM10, k_stat_window_s), 10);
    write_stat(CarbonDioxideConcentrationMeasurement::Id,
               CarbonDioxideConcentrationMeasurement::Attributes::PeakMeasuredValue::Id,
               CarbonDioxideConcentrationMeasurement::Attributes::AverageMeasuredValue::Id,
               ac_history_window(h, AC_CH_CO2, k_stat_window_s), 12);
    if (e->cfg.voc_publish_index_as_ppb)
        write_stat(TotalVolatileOrganicCompoundsConcentrationMeasurement::Id,
                   TotalVolatileOrganicCompoundsConcentrationMeasurement::Attributes::
                       PeakMeasuredValue::Id,
                   TotalVolatileOrganicCompoundsConcentrationMeasurement::Attributes::
                       AverageMeasuredValue::Id,
                   ac_history_window(h, AC_CH_VOC, k_stat_window_s), 14);
}

esp_err_t ac_matter_publish(const ac_engine_t *e)
{
    if (!s_ep_air || !ac_engine_values_valid(e)) return ESP_OK;

    write_u8(s_ep_air, AirQuality::Id, AirQuality::Attributes::AirQuality::Id,
             ac_airquality_to_matter(e->aq.level));

    write_float(s_ep_air, Pm25ConcentrationMeasurement::Id,
                Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id,
                e->last.pm25, 0);
    write_float(s_ep_air, Pm10ConcentrationMeasurement::Id,
                Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id,
                e->last.pm10, 1);
    write_float(s_ep_air, Pm1ConcentrationMeasurement::Id,
                Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id,
                e->last.pm1, 2);
    write_float(s_ep_air, CarbonDioxideConcentrationMeasurement::Id,
                CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id,
                e->last.co2, 3);
    if (e->cfg.voc_publish_index_as_ppb && e->last.voc_index >= 0)
        write_float(s_ep_air,
                    TotalVolatileOrganicCompoundsConcentrationMeasurement::Id,
                    TotalVolatileOrganicCompoundsConcentrationMeasurement::
                        Attributes::MeasuredValue::Id,
                    (float)e->last.voc_index, 4);

    publish_stats(e);

    if (e->last.temperature > -50.0f)
        write_i16(s_ep_temp, TemperatureMeasurement::Id,
                  TemperatureMeasurement::Attributes::MeasuredValue::Id,
                  (int16_t)(e->last.temperature * 100.0f));
    if (e->last.humidity >= 0.0f)
        write_u16(s_ep_hum, RelativeHumidityMeasurement::Id,
                  RelativeHumidityMeasurement::Attributes::MeasuredValue::Id,
                  (uint16_t)(e->last.humidity * 100.0f));
    return ESP_OK;
}

/* BatChargeFaultEnum (PowerSource/Enums.h): 0 Unspecified, 1 AmbientTooHot,
 * 2 AmbientTooCold, 10 SafetyTimeout.  0xFF: no fault. */
static void log_charge_fault(uint8_t now, uint8_t before)
{
    using namespace chip::app::Clusters::PowerSource;
    BatChargeFaultEnum cur[1] = { static_cast<BatChargeFaultEnum>(now) };
    BatChargeFaultEnum prev[1] = { static_cast<BatChargeFaultEnum>(before) };
    Events::BatChargeFaultChange::Type ev;
    ev.current = chip::app::DataModel::List<const BatChargeFaultEnum>(cur, now == 0xFF ? 0 : 1);
    ev.previous = chip::app::DataModel::List<const BatChargeFaultEnum>(prev, before == 0xFF ? 0 : 1);
    chip::EventNumber n;
    chip::DeviceLayer::StackLock lock;
    if (chip::app::LogEvent(ev, 0, n) != CHIP_NO_ERROR)
        ESP_LOGW(TAG, "BatChargeFaultChange event not logged");
}

esp_err_t ac_matter_publish_power(const pwr_state_t *st, bool ext, float vbat)
{
    /* pwr_std reports "battery" also when there is no cell and no USB-C
     * (a computer on the FireBeetle's own port keeps the device alive). */
    bool cells = st->src != PWR_SRC_EXTERNAL_NO_CELL && vbat >= PWR_V_ABSENT;
    /* The USB-C source is the module's input only; a computer on the
     * FireBeetle's port (ext) does not power the module. */
    bool usbc = st->src != PWR_SRC_BATTERY;
    (void)ext;

    /* USB-C (wired) source: Active while powered. */
    if (s_ep_wired) {
        esp_matter_attr_val_t ws = esp_matter_enum8(usbc ? 1 : 3);
        attribute::update(s_ep_wired, PowerSource::Id, PowerSource::Attributes::Status::Id, &ws);
        esp_matter_attr_val_t wp = esp_matter_bool(usbc);
        attribute::update(s_ep_wired, PowerSource::Id,
                          PowerSource::Attributes::WiredPresent::Id, &wp);
    }

    /* Battery source: Active on the cells, Standby behind USB-C, Unavailable
     * without cells. */
    esp_matter_attr_val_t st8 = esp_matter_enum8(!cells ? 3 : usbc ? 2 : 1);
    attribute::update(0, PowerSource::Id, PowerSource::Attributes::Status::Id, &st8);
    esp_matter_attr_val_t pr = esp_matter_bool(cells);
    attribute::update(0, PowerSource::Id, PowerSource::Attributes::BatPresent::Id, &pr);
    if (!cells) {
        esp_matter_attr_val_t none = esp_matter_nullable_uint8(nullable<uint8_t>());
        attribute::update(0, PowerSource::Id,
                          PowerSource::Attributes::BatPercentRemaining::Id, &none);
        return ESP_OK;
    }
    /* BatPercentRemaining is in half percent units, spec 11.7.6.14 */
    esp_matter_attr_val_t pct = esp_matter_nullable_uint8(pwr_matter_pct(st->pct));
    attribute::update(0, PowerSource::Id,
                      PowerSource::Attributes::BatPercentRemaining::Id, &pct);
    esp_matter_attr_val_t mv = esp_matter_nullable_uint32((uint32_t)(vbat * 1000.0f));
    attribute::update(0, PowerSource::Id, PowerSource::Attributes::BatVoltage::Id, &mv);
    /* BatChargeLevel 0 OK / 1 Warning / 2 Critical - pwr_std's levels */
    esp_matter_attr_val_t lvl = esp_matter_enum8((uint8_t)st->lvl);
    attribute::update(0, PowerSource::Id, PowerSource::Attributes::BatChargeLevel::Id, &lvl);
    /* BatChargeState: IsCharging marks the window in which temperature and
     * humidity may read a little high (charger heat). */
    esp_matter_attr_val_t cs = esp_matter_enum8((uint8_t)st->chg);
    attribute::update(0, PowerSource::Id, PowerSource::Attributes::BatChargeState::Id, &cs);
    /* A latched fault (timer ran out twice, or ISET/overcurrent) needs the
     * user: report it as "replacement needed" - the closest standard flag. */
    esp_matter_attr_val_t need = esp_matter_bool(st->fault == PWR_FLT_LATCHED);
    attribute::update(0, PowerSource::Id,
                      PowerSource::Attributes::BatReplacementNeeded::Id, &need);

    /* Charge faults as events.  PWR-K cannot tell an NTC too-hot from a
     * too-cold fault (or from input OVP): the room temperature from the SHT40
     * decides where it can, otherwise the fault stays Unspecified. */
    static uint8_t s_last_fault = 0xFF;
    uint8_t code = 0xFF;
    if (st->fault == PWR_FLT_LATCHED) {
        code = 10;                                       /* SafetyTimeout */
    } else if (st->fault == PWR_FLT_RECOVERABLE) {
        bool t_ok = s_engine && s_engine->health.sht_ok &&
                    s_engine->last.temperature > -40.0f;
        float t = t_ok ? s_engine->last.temperature : 20.0f;
        code = t <= 5.0f ? 2 : t >= 40.0f ? 1 : 0;       /* too cold / too hot / ? */
    }
    if (code != s_last_fault) {
        log_charge_fault(code, s_last_fault);
        s_last_fault = code;
    }
    return ESP_OK;
}

void ac_matter_set_identify_cb(void (*cb)(bool on)) { s_identify_cb = cb; }

esp_err_t ac_matter_set_label(const char *label)
{
    /* Basic Information / NodeLabel is the standard, controller-readable place
     * for a user-given name.  Apple Home keeps its own names inside its own
     * fabric and a second controller never sees them, so this is how the
     * dashboard tells "3D Printer" from "Room" without being told twice.  The
     * spec caps it at 32 characters, the same as AC_NAME_MAX - 1. */
    static char buf[33];
    strncpy(buf, label ? label : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    esp_matter_attr_val_t val = esp_matter_char_str(buf, (uint16_t)strlen(buf));
    return attribute::update(0, BasicInformation::Id,
                             BasicInformation::Attributes::NodeLabel::Id, &val);
}

bool ac_matter_is_commissioned(void)
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

esp_err_t ac_matter_set_tx_power(int8_t dbm)
{
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    static int8_t s_dbm = 0;            /* 0: nothing asked for yet */
    if (dbm == s_dbm) return ESP_OK;
    if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(100))) {
        ESP_LOGW(TAG, "tx power %d dBm: OpenThread busy, will retry", (int)dbm);
        return ESP_ERR_TIMEOUT;         /* the next battery read tries again */
    }
    otError err = otPlatRadioSetTransmitPower(esp_openthread_get_instance(), dbm);
    esp_openthread_lock_release();
    if (err != OT_ERROR_NONE) {
        ESP_LOGW(TAG, "tx power %d dBm: %d", (int)dbm, (int)err);
        return ESP_FAIL;
    }
    s_dbm = dbm;
    ESP_LOGI(TAG, "802.15.4 transmit power %d dBm", (int)dbm);
    return ESP_OK;
#else
    (void)dbm;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool ac_matter_thread_attached(void)
{
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    return chip::DeviceLayer::ConnectivityMgr().IsThreadAttached();
#else
    return false;
#endif
}

esp_err_t ac_matter_open_commissioning_window(void)
{
    auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (mgr.IsCommissioningWindowOpen()) return ESP_OK;
    CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds16(300),
        chip::CommissioningWindowAdvertisement::kAllSupported);
    return err == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL;
}

esp_err_t ac_matter_factory_reset(void)
{
    ESP_LOGW(TAG, "factory reset: erasing Matter and Thread credentials");
    esp_matter::factory_reset();
    return ESP_OK;
}

esp_err_t ac_matter_get_pairing_code(char *manual, size_t manual_len,
                                     char *qr, size_t qr_len)
{
    /* BLE is the rendezvous method: this device has no Wi-Fi in the build and
     * no IP connectivity until it has joined a Thread network. */
    const chip::RendezvousInformationFlags rv(chip::RendezvousInformationFlag::kBLE);

    if (manual && manual_len) {
        chip::MutableCharSpan span(manual, manual_len);
        if (GetManualPairingCode(span, rv) != CHIP_NO_ERROR) manual[0] = '\0';
        else manual[manual_len - 1] = '\0';
    }
    if (qr && qr_len) {
        chip::MutableCharSpan span(qr, qr_len);
        if (GetQRCode(span, rv) != CHIP_NO_ERROR) qr[0] = '\0';
        else qr[qr_len - 1] = '\0';
    }
    return ESP_OK;
}
