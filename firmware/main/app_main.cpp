/* 3D Printing AIR CHECK - application entry point (v1.4: SEN62, Senseair
 * Sunrise, SHT40 and SGP40 on a FireBeetle 2 ESP32-C6, a 1S4P LiFePO4 power
 * module charged over USB-C, no display, no custom PCB).
 *
 * Two tasks and nothing else:
 *   measure_task  drives the sensors according to whatever ac_engine decides,
 *                 reads the battery voltage, and hands the results to Matter
 *   ui_task       owns the button and the status LED
 *
 * The engine is the only place that decides *when* anything happens.  The
 * tasks just do what it says, which is what keeps the scheduling testable on a
 * workstation.  Numbers are shown elsewhere: in Apple Home and on the e-ink
 * dashboard, which reads this device over Matter (docs/DASHBOARD_INTERFACE.md).
 */
#include "ac_core/ac_power.h"
#include "ac_core/ac_engine.h"
#include "ac_core/ac_status.h"
#include "ac_hal/ac_hal.h"
#include "ac_console.h"
#include "ac_matter.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <cstdio>
#include <cmath>
#include <cstring>

static const char *TAG = "aircheck";

#define AC_FW_VERSION "1.4.0"
#define WDT_TIMEOUT_S 120
/* Today's outdoor CO2 background, the reference for a fresh-air
 * calibration (NOAA global mean, 2026: about 425 ppm). */
#define AC_CO2_OUTDOOR_PPM 425

static ac_engine_t s_engine;
static SemaphoreHandle_t s_lock;
static char s_manual_code[32];
static char s_qr_payload[64];
static char s_serial[24];
static ac_power_t s_power;            /* LFP module C: pwr_std state + timer */
static ac_sunrise_state_t s_co2;      /* Sunrise ABC state, held by the host */
static bool s_sen_running;       /* a continuous-mode window left it on */
static volatile uint16_t s_frc_ppm;     /* != 0: calibration requested */
static volatile bool s_asc_pending;     /* rewrite the Sunrise's ABC setting */

static float site_pressure_hpa(int altitude_m)
{
    /* ISA barometric formula; weather moves it by +-2 kPa, which the Sunrise
     * turns into +-3 % - honest, and documented in CALIBRATION.md. */
    return 1013.25f * powf(1.0f - 2.25577e-5f * (float)altitude_m, 5.25588f);
}

/* LED: the pattern that is running and when it started. */
static ac_led_pattern_t s_led;
static int64_t s_led_since_us;
static volatile ac_led_event_t s_led_event = AC_LED_EVENT_BOOT;
static volatile bool s_identify;

static inline ac_time_ms_t now_ms(void)
{
    return (ac_time_ms_t)(esp_timer_get_time() / 1000);
}

static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

/* ------------------------------------------------------------------ */

static void do_pm_measurement(uint32_t window_s, bool keep_running)
{
    ac_sen6x_values_t v;
    esp_err_t err = ac_sen6x_measure_window(window_s, keep_running, &v);
    s_sen_running = keep_running && err == ESP_OK;
    lock();
    unlock();
    ac_sample_t s;
    memset(&s, 0, sizeof(s));
    s.t = now_ms();
    s.voc_index = -1;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SEN62: %s", esp_err_to_name(err));
        if (keep_running) { ac_sen6x_power_off(); s_sen_running = false; }
        lock();
        ac_engine_sensor_failed(&s_engine, AC_ACT_SAMPLE_PM, esp_err_to_name(err));
        unlock();
        return;
    }
    s.pm1 = v.pm1; s.pm25 = v.pm25; s.pm4 = v.pm4; s.pm10 = v.pm10;
    s.pn05 = v.n05; s.pn10 = v.n10; s.typical_size = AC_INVALID_F;
    s.pm_fresh = true;
    lock();
    ac_engine_submit(&s_engine, &s);
    s_engine.health.sen6x_errors = 0;
    s_engine.health.sen6x_ok = true;
    unlock();
}

/* SHT40 first, then the SGP40 compensated with that fresh reading. */
static void do_voc_measurement(void)
{
    ac_sample_t s;
    memset(&s, 0, sizeof(s));
    s.t = now_ms();
    s.voc_index = -1;

    float t = -273.0f, rh = -1.0f;
    esp_err_t terr = ac_sht4x_measure(&t, &rh);
    lock();
    if (terr == ESP_OK) {
        s_engine.health.sht_ok = true;
        s_engine.health.sht_errors = 0;
    } else if (++s_engine.health.sht_errors >= 5) {
        s_engine.health.sht_ok = false;
    }
    if (terr != ESP_OK) {       /* compensate with the last good values */
        t = s_engine.last.temperature;
        rh = s_engine.last.humidity;
    }
    unlock();
    if (terr == ESP_OK) {
        s.temperature = t;
        s.humidity = rh;
        s.th_fresh = true;
    }

    int32_t raw = 0, index = -1;
    esp_err_t err = ac_sgp40_measure(t, rh, &raw, &index);
    if (err == ESP_OK) {
        s.voc_index = index;
        s.voc_raw = raw;
        s.voc_fresh = true;
    }
    lock();
    if (s.th_fresh || s.voc_fresh) ac_engine_submit(&s_engine, &s);
    if (err == ESP_OK) {
        s_engine.health.sgp40_errors = 0;
        s_engine.health.sgp40_ok = true;
    } else {
        ac_engine_sensor_failed(&s_engine, AC_ACT_SAMPLE_VOC, esp_err_to_name(err));
    }
    unlock();
}

/* Called between the 1 s slices of a long measurement. */
static void voc_hook(void)
{
    lock();
    bool due = ac_engine_take_voc(&s_engine, now_ms());
    unlock();
    if (due) do_voc_measurement();
}

static void do_co2_measurement(void)
{
    lock();
    float p = site_pressure_hpa(s_engine.cfg.altitude_m);
    unlock();
    ac_sunrise_values_t v;
    esp_err_t err = ac_sunrise_measure(&s_co2, p, &v);
    ac_sample_t s;
    memset(&s, 0, sizeof(s));
    s.t = now_ms();
    s.voc_index = -1;
    lock();
    if (err != ESP_OK) {
        ac_engine_sensor_failed(&s_engine, AC_ACT_SAMPLE_CO2, esp_err_to_name(err));
    } else {
        s.co2 = v.co2;
        s.co2_fresh = true;
        ac_engine_submit(&s_engine, &s);
        s_engine.health.co2_errors = 0;
        s_engine.health.co2_ok = true;
    }
    unlock();
    if (err != ESP_OK) ESP_LOGW(TAG, "Sunrise: %s", esp_err_to_name(err));
}

/* Fresh-air CO2 calibration: three ordinary measurements a minute apart so
 * the sensor sits in the reference air for a few minutes, then a Senseair
 * target calibration on the next one.  The LED blinks cyan meanwhile, then
 * green or red. */
static void do_co2_calibration(uint16_t ppm)
{
    ESP_LOGW(TAG, "fresh-air CO2 calibration to %u ppm: 3 min settling first", ppm);
    lock();
    float p = site_pressure_hpa(s_engine.cfg.altitude_m);
    unlock();
    ac_sunrise_values_t v = {};
    esp_err_t err = ESP_OK;
    for (int i = 0; i < 3 && err == ESP_OK; i++) {
        esp_task_wdt_reset();
        err = ac_sunrise_measure(&s_co2, p, &v);
        ESP_LOGI(TAG, "settling: %.0f ppm", (double)v.co2);
        ac_hal_wait_ms(50000);
    }
    esp_task_wdt_reset();
    if (err == ESP_OK) err = ac_sunrise_calibrate(&s_co2, p, ppm);
    if (err == ESP_OK)
        ESP_LOGW(TAG, "CO2 calibrated to %u ppm (it read %.0f)", ppm, (double)v.co2);
    else
        ESP_LOGE(TAG, "CO2 calibration failed: %s", esp_err_to_name(err));
    s_led_event = err == ESP_OK ? AC_LED_EVENT_CAL_OK : AC_LED_EVENT_CAL_FAIL;
}

/* The LFP module C (EDR-21).  External power - the module's USB-C, or a
 * computer on the FireBeetle's own port - means CONTINUOUS mode and no battery
 * alarms, as USB always has.  While the module charges, temperature and
 * humidity can read a little high (about 0.85-1.7 W of charger heat in the
 * case): Matter's BatChargeState says IsCharging then, and the dashboard marks
 * those values (docs/DASHBOARD_INTERFACE.md). */
static void read_battery(void)
{
    float vbat = -1.0f, ladder[2] = { 0, 0 }, vsys = 0;
    bool usb = false;
    esp_err_t err = ac_battery_read(&vbat, ladder, &vsys, &usb);
    uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000LL);
    lock();
    if (err != ESP_OK) {
        s_engine.health.battery_ok = false;
        ac_engine_set_power(&s_engine, usb, false, -1.0f, 0.0f, -1, now_ms());
        unlock();
        ac_battery_ce(0);                        /* fail-safe: charging allowed */
        return;
    }
    ac_power_out_t o = ac_power_update(&s_power, vbat, ladder, usb, now_s);
    bool cells = o.st.src != PWR_SRC_EXTERNAL_NO_CELL;
    float pct = cells ? (float)o.st.pct : -1.0f;
    s_engine.health.battery_ok = true;
    ac_engine_set_power(&s_engine, o.ext, o.charging, pct, vbat,
                        cells ? (int)o.st.lvl : -1, now_ms());
    unlock();
    ac_battery_ce(o.ce == AC_CE_HOLD ? 1 : o.ce == AC_CE_PULSE ? 2 : 0);

    static int last_key = -1;
    int key = (int)o.st.src * 16 + (int)o.st.chg * 4 + (int)o.st.fault;
    if (key != last_key) {
        ESP_LOGI(TAG, "power: %s, cells %.2f V (%u %%), %s%s%s, VSYS %.2f V",
                 o.st.src == PWR_SRC_BATTERY ? "cells" :
                 o.st.src == PWR_SRC_EXTERNAL ? "USB-C" : "USB-C, no cells",
                 (double)vbat, o.st.pct,
                 o.st.chg == PWR_CHG_CHARGING ? "charging" :
                 o.st.chg == PWR_CHG_FULL ? "full" : "not charging",
                 o.ce == AC_CE_HOLD ? ", charge paused" : "",
                 o.st.fault == PWR_FLT_LATCHED ? ", FAULT (timer?)" :
                 o.st.fault == PWR_FLT_RECOVERABLE ? ", fault (temperature/OVP)" : "",
                 (double)vsys);
        if (o.ce == AC_CE_PULSE)
            ESP_LOGW(TAG, "safety timer ran out before full: charging restarted once");
        last_key = key;
    }
    ac_matter_publish_power(&o.st, o.ext, vbat);
}

static void persist(void)
{
    lock();
    ac_baseline_store_t b = *ac_baseline_snapshot(&s_engine.base);
    ac_history_t h = s_engine.hist;
    unlock();
    ac_store_baseline_save(&b);
    ac_store_history_save(&h);
    lock();
    ac_sunrise_state_t co2 = s_co2;
    unlock();
    ac_store_blob_save("sunrise", &co2, sizeof(co2));
}

/* ------------------------------------------------------------------ */

static void on_config_changed(void)
{
    lock();
    ac_config_t c = s_engine.cfg;
    unlock();
    ac_store_config_save(&c);
    ac_matter_set_label(c.name);
    s_asc_pending = true;          /* rewritten only if it differs */
}

static void on_request_frc(uint16_t ppm) { s_frc_ppm = ppm; s_led_event = AC_LED_EVENT_CALIBRATING; }

static void start_console(void)
{
    const ac_console_ctx_t ctx = {
        .engine = &s_engine,
        .lock = lock,
        .unlock = unlock,
        .config_changed = on_config_changed,
        .request_frc = on_request_frc,
        .thread_attached = ac_matter_thread_attached,
    };
    ac_console_start(&ctx);
}

static void measure_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(nullptr);
    int64_t last_persist = 0;
    int64_t last_battery = -1;
    ac_mode_t last_mode = AC_MODE_COUNT;
    ac_device_state_t last_state = AC_STATE_COUNT;
    ac_air_quality_t last_aq = AC_AQ_UNKNOWN;

    for (;;) {
        esp_task_wdt_reset();

        lock();
        ac_plan_t plan = ac_engine_tick(&s_engine, now_ms());
        uint32_t battery_s = s_engine.cfg.battery_interval_s;
        bool continuous = ac_engine_profile(&s_engine)->pm_interval_s == 0;
        ac_mode_t mode = s_engine.mode;
        ac_device_state_t state = s_engine.state;
        ac_air_quality_t aq = s_engine.aq.level;
        uint32_t reason = s_engine.aq.reason;
        unlock();

        /* The sensor has no screen, so every transition worth knowing about
         * goes to the log. */
        if (mode != last_mode || state != last_state) {
            ESP_LOGI(TAG, "state %s, mode %s", ac_device_state_name(state),
                     ac_mode_name(mode));
            last_mode = mode;
            last_state = state;
        }
        if (aq != last_aq) {
            char why[48];
            ac_reason_text(reason, why, sizeof(why));
            ESP_LOGI(TAG, "air quality %s (%s)", ac_air_quality_name(aq), why);
            last_aq = aq;
        }

        /* Leaving continuous mode (USB unplugged) must not leave the SEN62
         * running at 75 mA until the next hourly window. */
        if (s_sen_running && !continuous) {
            ac_sen6x_power_off();
            s_sen_running = false;
        }
        if (s_frc_ppm) {
            uint16_t ppm = s_frc_ppm;
            do_co2_calibration(ppm);
            s_frc_ppm = 0;
        }
        if (s_asc_pending) {
            lock();
            bool abc = s_engine.cfg.co2_self_calibration;
            unlock();
            s_asc_pending = false;
            if (ac_sunrise_setup(abc) != ESP_OK)
                ESP_LOGW(TAG, "could not update the Sunrise's configuration");
        }

        switch (plan.action) {
        case AC_ACT_SAMPLE_PM:
            do_pm_measurement(plan.pm_window_s, plan.pm_keep_running);
            break;
        case AC_ACT_SAMPLE_VOC:  do_voc_measurement(); break;
        case AC_ACT_SAMPLE_CO2:  do_co2_measurement(); break;
        default:
            break;
        }

        /* Advance the Sunrise's ABC clock by the time that passed. */
        int64_t t = esp_timer_get_time();
        {
            static int64_t last_t = 0;
            lock();
            s_co2.abc_ms += (uint32_t)((t - last_t) / 1000);
            unlock();
            last_t = t;
        }

        /* The battery is read between sensor windows, never during one, so
         * the voltage is close to the resting voltage the curve assumes. */
        if (last_battery < 0 || t - last_battery > (int64_t)battery_s * 1000000LL) {
            read_battery();
            last_battery = t;
            /* the service console only exists while the cable is in */
            lock();
            bool usb = s_engine.usb_present;
            unlock();
            if (usb) start_console();
        }

        if (plan.publish_dirty) {
            lock();
            ac_engine_t snapshot = s_engine;
            unlock();
            ac_matter_publish(&snapshot);
        }

        /* An event record that just completed goes into flash.  The whole
         * reason to record it is to look at it after the print has finished. */
        lock();
        bool have_event = s_engine.ev.cur.complete;
        ac_event_record_t rec = s_engine.ev.cur;
        if (have_event) s_engine.ev.cur.complete = false;
        unlock();
        if (have_event) {
            ac_store_event_append(&rec);
            ESP_LOGI(TAG, "event stored: peak PM2.5 %.1f, VOC %ld, %lu s",
                     (double)rec.peak_pm25, (long)rec.peak_voc,
                     (unsigned long)rec.duration_s);
        }

        if (t - last_persist > 30LL * 60 * 1000000) {
            persist();
            last_persist = t;
        }

        uint32_t sleep_ms = plan.sleep_ms;
        if (sleep_ms == 0) sleep_ms = 20;
        if (sleep_ms > (WDT_TIMEOUT_S - 20) * 1000u)
            sleep_ms = (WDT_TIMEOUT_S - 20) * 1000u;
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

/* ------------------------------------------------------------------ */

static void handle_button(ac_button_event_t ev)
{
    switch (ev) {
    case AC_BTN_SHORT:
        s_led_event = AC_LED_EVENT_BUTTON;
        break;

    case AC_BTN_LONG:
        ESP_LOGI(TAG, "opening the commissioning window");
        ac_matter_open_commissioning_window();
        ac_matter_get_pairing_code(s_manual_code, sizeof(s_manual_code),
                                   s_qr_payload, sizeof(s_qr_payload));
        ESP_LOGI(TAG, "manual pairing code %s", s_manual_code);
        s_led_event = AC_LED_EVENT_PAIRING;
        break;

    case AC_BTN_CALIBRATE:
        /* The user has taken it outdoors (ASSEMBLY.md / CALIBRATION.md). */
        ESP_LOGW(TAG, "fresh-air CO2 calibration requested by button");
        on_request_frc(AC_CO2_OUTDOOR_PPM);
        break;

    case AC_BTN_VERY_LONG:
        ESP_LOGW(TAG, "factory reset requested");
        s_led_event = AC_LED_EVENT_RESET;
        lock();
        ac_engine_set_state(&s_engine, AC_STATE_FACTORY_RESET, now_ms());
        unlock();
        vTaskDelay(pdMS_TO_TICKS(3000));    /* let the red blink be seen */
        ac_store_erase_all();
        ac_matter_factory_reset();          /* reboots */
        break;

    default:
        break;
    }
}

static void on_identify(bool on) { s_identify = on; }

static void ui_task(void *arg)
{
    (void)arg;
    uint8_t shown = 0xFF;
    for (;;) {
        ac_button_event_t bev = ac_button_poll();
        if (bev != AC_BTN_NONE) handle_button(bev);

        /* a new user event replaces whatever pattern was running */
        ac_led_event_t lev = s_led_event;
        if (lev != AC_LED_EVENT_NONE) {
            s_led_event = AC_LED_EVENT_NONE;
            lock();
            s_led = ac_status_pattern(&s_engine, lev);
            unlock();
            s_led_since_us = esp_timer_get_time();
        }

        uint32_t elapsed = (uint32_t)((esp_timer_get_time() - s_led_since_us) / 1000);
        ac_rgb_t c = ac_status_level(&s_led, elapsed);
        if (s_led.duration_ms && elapsed >= s_led.duration_ms) {
            /* the user-triggered pattern is over; fall back to the unprompted one */
            lock();
            s_led = ac_status_pattern(&s_engine, AC_LED_EVENT_NONE);
            unlock();
            s_led_since_us = esp_timer_get_time();
            c = ac_status_level(&s_led, 0);
        }
        if (s_identify) c = ((elapsed / 250) % 2) ? AC_RGB_WHITE : AC_RGB_OFF;
        /* while the button is held, show what letting go would do */
        uint32_t held = ac_button_held_ms();
        if (held >= AC_HOLD_PAIRING_MS) c = ac_status_hold_colour(held);

        if ((uint8_t)c != shown) {
            ac_led_set((uint8_t)c);
            shown = (uint8_t)c;
        }
        /* 30 ms while something is lit or the button is down, otherwise
         * relax: tickless idle only saves power if the tasks let it. */
        bool busy = c != AC_RGB_OFF || ac_button_pressed() || s_identify ||
                    (s_led.duration_ms && elapsed < s_led.duration_ms);
        vTaskDelay(pdMS_TO_TICKS(busy ? 30 : 100));
    }
}

/* ------------------------------------------------------------------ */

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "3D Printing AIR CHECK %s", AC_FW_VERSION);

    s_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(ac_store_init());
    ESP_ERROR_CHECK(ac_hal_init());
    ESP_ERROR_CHECK(ac_button_init());
    ac_led_set(AC_RGB_WHITE);                /* proof of life before Matter starts */

    uint8_t mac[8] = { 0 };
    esp_read_mac(mac, ESP_MAC_IEEE802154);
    snprintf(s_serial, sizeof(s_serial), "AC-%02X%02X-%02X%02X",
             mac[4], mac[5], mac[6], mac[7]);
    ESP_LOGI(TAG, "serial %s", s_serial);

    ac_config_t cfg;
    if (ac_store_config_load(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "no usable stored config, using defaults");
        ac_config_defaults(&cfg);
        ac_store_config_save(&cfg);
    }
    ac_engine_init(&s_engine, &cfg, now_ms());

    ac_baseline_store_t saved;
    if (ac_store_baseline_load(&saved) == ESP_OK &&
        ac_baseline_restore(&s_engine.base, &saved)) {
        ESP_LOGI(TAG, "baseline restored: PM2.5 %.1f, VOC %ld",
                 (double)saved.pm25, (long)saved.voc);
    }
    ac_store_history_load(&s_engine.hist);

    /* A sensor that fails to start is marked down; it never stops the device
     * from starting, because a unit that will not boot cannot tell you what is
     * wrong with it. */
    if (ac_sgp40_init(cfg.profile[cfg.default_mode].voc_interval_s) != ESP_OK) {
        ESP_LOGE(TAG, "SGP40 init failed");
        s_engine.health.sgp40_ok = false;
    }
    float t0, rh0;
    if (ac_sht4x_measure(&t0, &rh0) != ESP_OK) {
        ESP_LOGE(TAG, "SHT40 not responding");
        s_engine.health.sht_ok = false;
    }
    char sen_serial[33] = { 0 };
    if (ac_sen6x_serial(sen_serial, sizeof(sen_serial)) != ESP_OK) {
        ESP_LOGE(TAG, "SEN62 not responding");
        s_engine.health.sen6x_ok = false;
    } else {
        ESP_LOGI(TAG, "SEN62 %s", sen_serial);
    }
    if (ac_sunrise_setup(cfg.co2_self_calibration) != ESP_OK) {
        ESP_LOGE(TAG, "Sunrise not responding");
        s_engine.health.co2_ok = false;
    }
    /* The Sunrise's ABC state survives a reboot.  The charge-pause state does
     * not need to: after a reboot CE starts released (charging allowed), the
     * fail-safe direction, and pwr_std re-learns "full". */
    ac_hal_set_wait_hook(voc_hook);
    ac_power_init(&s_power);
    if (ac_store_blob_load("sunrise", &s_co2, sizeof(s_co2)) != ESP_OK)
        memset(&s_co2, 0, sizeof(s_co2));
    ESP_LOGI(TAG, "power: LFP 1S4P module; site %d m = %.0f hPa",
             cfg.altitude_m, (double)site_pressure_hpa(cfg.altitude_m));

    ESP_ERROR_CHECK(ac_matter_init(&s_engine));
    ac_matter_set_identify_cb(on_identify);
    ESP_ERROR_CHECK(ac_matter_start());
    ac_matter_set_label(cfg.name);
    ac_matter_get_pairing_code(s_manual_code, sizeof(s_manual_code),
                               s_qr_payload, sizeof(s_qr_payload));
    if (!ac_matter_is_commissioned()) {
        ESP_LOGI(TAG, "not commissioned; manual code %s, QR %s",
                 s_manual_code, s_qr_payload);
        s_led_event = AC_LED_EVENT_PAIRING;
    }

#if CONFIG_PM_ENABLE
    esp_pm_config_t pm = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true,
#endif
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm));
#endif

    esp_task_wdt_config_t wdt = {
        .timeout_ms = WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt);

    xTaskCreate(measure_task, "measure", 6144, nullptr, 5, nullptr);
    xTaskCreate(ui_task, "ui", 3072, nullptr, 4, nullptr);
}
