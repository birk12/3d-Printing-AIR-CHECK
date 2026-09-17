#include "ac_matter.h"

#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#include <platform/ThreadStackManager.h>
#endif

#include <cmath>
#include <cstring>
#include <string>

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

static uint16_t s_ep_air = 0, s_ep_temp = 0, s_ep_hum = 0;
static ac_engine_t *s_engine = nullptr;
static bool s_commissioned = false;

/* last value written, so we never write the same number twice */
static float s_last[8];
static uint8_t s_last_aq = 0xFF;

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

static void add_concentration(endpoint_t *ep, uint32_t cluster_id,
                              uint8_t unit)
{
    /* Every concentration cluster is created with the NumericMeasurement
     * feature only.  LevelIndication is deliberately left off: mapping a
     * measured ug/m3 onto a five step enum adds no information that the
     * Air Quality cluster on the same endpoint does not already carry. */
    switch (cluster_id) {
    case Pm25ConcentrationMeasurement::Id: {
        cluster::pm2_5_concentration_measurement::config_t cfg;
        cfg.measurement_medium = MEDIUM_AIR;
        cfg.feature_flags = cluster::pm2_5_concentration_measurement::
                            feature::numeric_measurement::get_id();
        cfg.features.numeric_measurement.measurement_unit = unit;
        cfg.features.numeric_measurement.min_measured_value = 0.0f;
        cfg.features.numeric_measurement.max_measured_value = 1000.0f;
        cluster::pm2_5_concentration_measurement::create(ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    }
    case Pm10ConcentrationMeasurement::Id: {
        cluster::pm10_concentration_measurement::config_t cfg;
        cfg.measurement_medium = MEDIUM_AIR;
        cfg.feature_flags = cluster::pm10_concentration_measurement::
                            feature::numeric_measurement::get_id();
        cfg.features.numeric_measurement.measurement_unit = unit;
        cfg.features.numeric_measurement.min_measured_value = 0.0f;
        cfg.features.numeric_measurement.max_measured_value = 1000.0f;
        cluster::pm10_concentration_measurement::create(ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    }
    case Pm1ConcentrationMeasurement::Id: {
        cluster::pm1_concentration_measurement::config_t cfg;
        cfg.measurement_medium = MEDIUM_AIR;
        cfg.feature_flags = cluster::pm1_concentration_measurement::
                            feature::numeric_measurement::get_id();
        cfg.features.numeric_measurement.measurement_unit = unit;
        cfg.features.numeric_measurement.min_measured_value = 0.0f;
        cfg.features.numeric_measurement.max_measured_value = 1000.0f;
        cluster::pm1_concentration_measurement::create(ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    }
    case CarbonDioxideConcentrationMeasurement::Id: {
        cluster::carbon_dioxide_concentration_measurement::config_t cfg;
        cfg.measurement_medium = MEDIUM_AIR;
        cfg.feature_flags = cluster::carbon_dioxide_concentration_measurement::
                            feature::numeric_measurement::get_id();
        cfg.features.numeric_measurement.measurement_unit = unit;
        cfg.features.numeric_measurement.min_measured_value = 400.0f;
        cfg.features.numeric_measurement.max_measured_value = 5000.0f;
        cluster::carbon_dioxide_concentration_measurement::create(ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    }
    case TotalVolatileOrganicCompoundsConcentrationMeasurement::Id: {
        cluster::total_volatile_organic_compounds_concentration_measurement::config_t cfg;
        cfg.measurement_medium = MEDIUM_AIR;
        cfg.feature_flags = cluster::total_volatile_organic_compounds_concentration_measurement::
                            feature::numeric_measurement::get_id();
        cfg.features.numeric_measurement.measurement_unit = unit;
        cfg.features.numeric_measurement.min_measured_value = 0.0f;
        cfg.features.numeric_measurement.max_measured_value = 500.0f;
        cluster::total_volatile_organic_compounds_concentration_measurement::create(
            ep, &cfg, CLUSTER_FLAG_SERVER);
        break;
    }
    default:
        break;
    }
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
    /* Identify on an e-paper device means flashing the screen, which is slow
     * and ugly.  Instead the display switches to the diagnostics screen, which
     * shows the serial number - that is what a user needs to tell two
     * identical units apart. */
    ESP_LOGI(TAG, "identify: type %d", (int)type);
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

    add_concentration(ep, Pm25ConcentrationMeasurement::Id, UNIT_UGM3);
    add_concentration(ep, Pm10ConcentrationMeasurement::Id, UNIT_UGM3);
    add_concentration(ep, Pm1ConcentrationMeasurement::Id, UNIT_UGM3);
    add_concentration(ep, CarbonDioxideConcentrationMeasurement::Id, UNIT_PPM);
    /* See docs/MATTER.md: the SGP40 produces a VOC *Index*, not a
     * concentration.  Publishing it in a concentration cluster is a
     * compromise, taken because without a number Apple Home cannot build a
     * VOC automation at all.  The unit is declared as PPB and the
     * documentation says in as many words that the value is an index. */
    add_concentration(ep, TotalVolatileOrganicCompoundsConcentrationMeasurement::Id,
                      UNIT_PPB);

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
        ps.order = 1;
        strncpy(ps.description, "Battery", sizeof(ps.description) - 1);

        /* The generated create() validates that exactly one of Wired and
         * Battery is in the feature map and adds the features itself, so the
         * flags have to be set here rather than by calling feature::add()
         * afterwards. RECHG is what carries BatChargeState; Status describes
         * the power source, not the charge, and is the wrong attribute for it. */
        ps.feature_flags = cluster::power_source::feature::battery::get_id() |
                           cluster::power_source::feature::rechargeable::get_id();
        ps.features.battery.bat_charge_level = 0;        /* OK */
        ps.features.battery.bat_replacement_needed = false;
        ps.features.battery.bat_replaceability = 2;      /* UserReplaceable,
                                                          * and it genuinely is */
        ps.features.rechargeable.bat_charge_state = 0;   /* Unknown */
        ps.features.rechargeable.bat_functional_while_charging = true;

        cluster_t *psc = cluster::power_source::create(root, &ps, CLUSTER_FLAG_SERVER);
        if (psc) {
            /* Optional attributes the features do not create for us. */
            cluster::power_source::attribute::create_bat_present(psc, true);
            cluster::power_source::attribute::create_bat_percent_remaining(
                psc, nullable<uint8_t>());
            cluster::power_source::attribute::create_bat_voltage(
                psc, nullable<uint32_t>());
        } else {
            ESP_LOGE(TAG, "power source cluster was not created");
        }
    }

    ESP_LOGI(TAG, "endpoints: air=%u temperature=%u humidity=%u",
             s_ep_air, s_ep_temp, s_ep_hum);
    return ESP_OK;
}

esp_err_t ac_matter_start(void)
{
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif
    return esp_matter::start(device_event_cb);
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

esp_err_t ac_matter_publish_battery(float percent, float volts, bool charging,
                                    bool low)
{
    if (percent < 0.0f) return ESP_OK;
    /* BatPercentRemaining is in half percent units, spec 11.7.6.14 */
    esp_matter_attr_val_t pct = esp_matter_nullable_uint8(
        (uint8_t)(percent * 2.0f + 0.5f));
    attribute::update(0, PowerSource::Id,
                      PowerSource::Attributes::BatPercentRemaining::Id, &pct);

    esp_matter_attr_val_t mv = esp_matter_nullable_uint32((uint32_t)(volts * 1000.0f));
    attribute::update(0, PowerSource::Id,
                      PowerSource::Attributes::BatVoltage::Id, &mv);

    /* BatChargeLevel: 0 OK, 1 Warning, 2 Critical */
    esp_matter_attr_val_t lvl = esp_matter_enum8(low ? (percent < 5.0f ? 2 : 1) : 0);
    attribute::update(0, PowerSource::Id,
                      PowerSource::Attributes::BatChargeLevel::Id, &lvl);

    /* BatChargeState: 0 Unknown, 1 IsCharging, 2 IsAtFullCharge,
     * 3 IsNotCharging */
    uint8_t cs = charging ? (percent >= 99.0f ? 2 : 1) : 3;
    esp_matter_attr_val_t chg = esp_matter_enum8(cs);
    attribute::update(0, PowerSource::Id,
                      PowerSource::Attributes::BatChargeState::Id, &chg);
    return ESP_OK;
}

bool ac_matter_is_commissioned(void)
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
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
    chip::PayloadContents payload;
    /* GetPayloadContents lives in the global namespace, not chip:: */
    CHIP_ERROR err = GetPayloadContents(
        payload, chip::RendezvousInformationFlags(
                     chip::RendezvousInformationFlag::kBLE));
    if (err != CHIP_NO_ERROR) return ESP_FAIL;

    if (manual && manual_len) {
        chip::ManualSetupPayloadGenerator gen(payload);
        chip::MutableCharSpan span(manual, manual_len);
        if (gen.payloadDecimalStringRepresentation(span) != CHIP_NO_ERROR)
            manual[0] = '\0';
    }
    if (qr && qr_len) {
        chip::QRCodeSetupPayloadGenerator gen(payload);
        std::string out;
        if (gen.payloadBase38Representation(out) == CHIP_NO_ERROR)
            strncpy(qr, out.c_str(), qr_len - 1);
        else
            qr[0] = '\0';
        qr[qr_len - 1] = '\0';
    }
    return ESP_OK;
}
