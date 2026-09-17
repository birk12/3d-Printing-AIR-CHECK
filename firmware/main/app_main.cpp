/* 3D Printing AIR CHECK - application entry point.
 *
 * Two tasks and nothing else:
 *   measure_task  drives the sensors according to whatever ac_engine decides,
 *                 and hands the results to Matter
 *   ui_task       owns the button and the e-paper panel
 *
 * The engine is the only place that decides *when* anything happens.  The
 * tasks just do what it says, which is what keeps the scheduling testable on a
 * workstation.
 */
#include "ac_core/ac_display.h"
#include "ac_core/ac_engine.h"
#include "ac_hal/ac_hal.h"
#include "ac_matter.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <cstdio>
#include <cstring>

static const char *TAG = "aircheck";

#define AC_FW_VERSION "1.0.0"
#define WDT_TIMEOUT_S 120

static ac_engine_t s_engine;
static SemaphoreHandle_t s_lock;
static ac_canvas_t s_canvas;
static ac_screen_t s_screen = AC_SCREEN_OVERVIEW;
static int64_t s_display_on_until_us;
static bool s_display_awake;
static char s_manual_code[32];
static char s_qr_payload[64];
static char s_serial[24];
static uint32_t s_full_refreshes;

static inline ac_time_ms_t now_ms(void)
{
    return (ac_time_ms_t)(esp_timer_get_time() / 1000);
}

static void lock(void)   { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

/* ------------------------------------------------------------------ */

static void build_ui_context(ac_ui_context_t *ui)
{
    ui->thread_attached = ac_matter_thread_attached();
    ui->matter_commissioned = ac_matter_is_commissioned();
    ui->thread_rssi_neg = 0;
    ui->fw_version = AC_FW_VERSION;
    ui->serial = s_serial;
    ui->pairing_code = s_manual_code[0] ? s_manual_code : nullptr;
    ui->qr_payload = s_qr_payload[0] ? s_qr_payload : nullptr;
    ui->events_today = s_engine.ev.events_total;
}

static void redraw(ac_screen_t screen, bool force_full)
{
    ac_ui_context_t ui;
    build_ui_context(&ui);

    lock();
    ac_display_render(&s_canvas, &s_engine, &ui, screen, now_ms());
    unlock();

    ac_rail_epd(true);
    if (ac_epd_init() != ESP_OK) {
        ESP_LOGE(TAG, "e-paper init failed");
        ac_rail_epd(false);
        return;
    }
    /* A partial update is about six times cheaper than a full one, but the
     * SSD1681 accumulates ghosting.  One full refresh every 20 partials keeps
     * the panel clean without costing anything measurable. */
    esp_err_t err;
    if (force_full || (s_full_refreshes % 20) == 0)
        err = ac_epd_full_update(s_canvas.px);
    else
        err = ac_epd_partial_update(s_canvas.px);
    s_full_refreshes++;
    if (err != ESP_OK) ESP_LOGW(TAG, "e-paper update: %s", esp_err_to_name(err));
    ac_epd_sleep();
    ac_rail_epd(false);
}

/* ------------------------------------------------------------------ */

static void do_pm_measurement(uint32_t window_s)
{
    ac_sps30_values_t v;
    esp_err_t err = ac_sps30_measure_window(window_s, &v);
    ac_sample_t s;
    memset(&s, 0, sizeof(s));
    s.t = now_ms();
    s.voc_index = -1;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPS30: %s", esp_err_to_name(err));
        lock();
        ac_engine_sensor_failed(&s_engine, AC_ACT_SAMPLE_PM, esp_err_to_name(err));
        unlock();
        return;
    }
    s.pm1 = v.pm1; s.pm25 = v.pm25; s.pm4 = v.pm4; s.pm10 = v.pm10;
    s.pn05 = v.n05; s.pn10 = v.n10; s.typical_size = v.typical_size;
    s.pm_fresh = true;
    lock();
    ac_engine_submit(&s_engine, &s);
    s_engine.health.sps30_errors = 0;
    s_engine.health.sps30_ok = true;
    unlock();
}

static void do_voc_measurement(void)
{
    float t, rh;
    lock();
    t = s_engine.last.temperature;
    rh = s_engine.last.humidity;
    unlock();

    int32_t raw = 0, index = -1;
    esp_err_t err = ac_sgp40_measure(t, rh, &raw, &index);
    ac_sample_t s;
    memset(&s, 0, sizeof(s));
    s.t = now_ms();
    s.voc_index = -1;
    if (err != ESP_OK) {
        lock();
        ac_engine_sensor_failed(&s_engine, AC_ACT_SAMPLE_VOC, esp_err_to_name(err));
        unlock();
        return;
    }
    s.voc_index = index;
    s.voc_raw = raw;
    s.voc_fresh = true;
    lock();
    ac_engine_submit(&s_engine, &s);
    s_engine.health.sgp40_errors = 0;
    s_engine.health.sgp40_ok = true;
    unlock();
}

static void do_co2_measurement(void)
{
    float co2 = 0, t = 0, rh = 0;
    esp_err_t err = ac_scd41_single_shot(&co2, &t, &rh);
    ac_sample_t s;
    memset(&s, 0, sizeof(s));
    s.t = now_ms();
    s.voc_index = -1;
    if (err != ESP_OK) {
        lock();
        ac_engine_sensor_failed(&s_engine, AC_ACT_SAMPLE_CO2, esp_err_to_name(err));
        unlock();
        return;
    }
    s.co2 = co2; s.co2_fresh = true;
    s.temperature = t; s.humidity = rh; s.th_fresh = true;
    lock();
    ac_engine_submit(&s_engine, &s);
    s_engine.health.scd41_errors = 0;
    s_engine.health.scd41_ok = true;
    unlock();
}

static void persist(void)
{
    lock();
    ac_baseline_store_t b = *ac_baseline_snapshot(&s_engine.base);
    ac_history_t h = s_engine.hist;
    unlock();
    ac_store_baseline_save(&b);
    ac_store_history_save(&h);
}

/* ------------------------------------------------------------------ */

static void measure_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(nullptr);
    int64_t last_persist = 0;

    for (;;) {
        esp_task_wdt_reset();

        lock();
        ac_plan_t plan = ac_engine_tick(&s_engine, now_ms());
        unlock();

        switch (plan.action) {
        case AC_ACT_SAMPLE_PM:   do_pm_measurement(plan.pm_window_s); break;
        case AC_ACT_SAMPLE_VOC:  do_voc_measurement(); break;
        case AC_ACT_SAMPLE_CO2:  do_co2_measurement(); break;
        case AC_ACT_FAN_CLEAN:
            ESP_LOGI(TAG, "weekly fan cleaning");
            ac_rail_sps30(true);
            ac_sps30_init();
            ac_sps30_start_measurement();
            ac_sps30_start_fan_cleaning();
            vTaskDelay(pdMS_TO_TICKS(12000));
            ac_sps30_stop_measurement();
            ac_rail_sps30(false);
            break;
        default:
            break;
        }

        float v = 0, pct = -1;
        bool charging = false;
        if (ac_battery_read(&v, &pct, &charging) == ESP_OK) {
            lock();
            ac_engine_set_power(&s_engine, charging, charging, pct, v, now_ms());
            bool low = s_engine.state == AC_STATE_LOW_BATTERY ||
                       s_engine.state == AC_STATE_CRITICAL_BATTERY;
            unlock();
            ac_matter_publish_battery(pct, v, charging, low);
        }

        if (plan.publish_dirty) {
            lock();
            ac_engine_t snapshot = s_engine;
            unlock();
            ac_matter_publish(&snapshot);
        }
        if (plan.display_dirty && s_display_awake)
            redraw(s_screen, false);

        /* An event record that just completed goes into flash.  There is no
         * point keeping it only in RAM: the whole reason to record it is to be
         * able to look at it after the print has finished. */
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

        int64_t t = esp_timer_get_time();
        if (t - last_persist > 30LL * 60 * 1000000) {
            persist();
            last_persist = t;
        }

        uint32_t sleep_ms = plan.sleep_ms;
        if (sleep_ms == 0) sleep_ms = 20;
        /* Never sleep past the watchdog. */
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
        if (!s_display_awake) {
            s_display_awake = true;
            redraw(s_screen, true);
        } else {
            s_screen = (ac_screen_t)((s_screen + 1) % AC_SCREEN_COUNT);
            redraw(s_screen, false);
        }
        lock();
        s_display_on_until_us = esp_timer_get_time() +
            (int64_t)s_engine.cfg.display_timeout_s * 1000000LL;
        unlock();
        break;

    case AC_BTN_LONG:
        ESP_LOGI(TAG, "opening the commissioning window");
        ac_matter_open_commissioning_window();
        ac_matter_get_pairing_code(s_manual_code, sizeof(s_manual_code),
                                   s_qr_payload, sizeof(s_qr_payload));
        s_display_awake = true;
        redraw(AC_SCREEN_COMMISSION, true);
        s_display_on_until_us = esp_timer_get_time() + 300LL * 1000000LL;
        break;

    case AC_BTN_VERY_LONG:
        ESP_LOGW(TAG, "factory reset requested");
        lock();
        ac_engine_set_state(&s_engine, AC_STATE_FACTORY_RESET, now_ms());
        unlock();
        redraw(AC_SCREEN_OVERVIEW, true);
        ac_store_erase_all();
        ac_matter_factory_reset();      /* reboots */
        break;

    default:
        break;
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    for (;;) {
        ac_button_event_t ev = ac_button_poll();
        if (ev != AC_BTN_NONE) handle_button(ev);

        if (s_display_awake && esp_timer_get_time() > s_display_on_until_us) {
            /* "Off" on e-paper means: stop refreshing.  The last screen stays
             * readable at zero current, which is the whole point of the
             * technology - a blanked panel would only waste one more refresh. */
            s_display_awake = false;
            ESP_LOGD(TAG, "display idle, holding the last image");
        }
        vTaskDelay(pdMS_TO_TICKS(30));
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

    uint8_t mac[8] = { 0 };
    esp_read_mac(mac, ESP_MAC_IEEE802154);
    snprintf(s_serial, sizeof(s_serial), "AC-%02X%02X-%02X%02X",
             mac[4], mac[5], mac[6], mac[7]);

    ac_config_t cfg;
    if (ac_store_config_load(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "no stored config, using defaults");
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

    /* Sensors.  A failure here is logged and the channel is marked down; it
     * never stops the device from starting, because a unit that boots into a
     * blank screen is useless for working out what is wrong with it. */
    if (ac_sgp40_init(cfg.profile[cfg.default_mode].voc_interval_s) != ESP_OK) {
        ESP_LOGE(TAG, "SGP40 init failed");
        s_engine.health.sgp40_ok = false;
    }
    if (ac_scd41_init() != ESP_OK) {
        ESP_LOGE(TAG, "SCD41 init failed");
        s_engine.health.scd41_ok = false;
    }
    ac_battery_hibernate(true);

    ESP_ERROR_CHECK(ac_matter_init(&s_engine));
    ESP_ERROR_CHECK(ac_matter_start());
    ac_matter_get_pairing_code(s_manual_code, sizeof(s_manual_code),
                               s_qr_payload, sizeof(s_qr_payload));
    if (!ac_matter_is_commissioned())
        ESP_LOGI(TAG, "not commissioned; pairing code %s", s_manual_code);

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

    s_display_awake = true;
    s_display_on_until_us = esp_timer_get_time() +
                            (int64_t)cfg.display_timeout_s * 1000000LL;
    redraw(AC_SCREEN_WARMUP, true);

    xTaskCreate(measure_task, "measure", 6144, nullptr, 5, nullptr);
    xTaskCreate(ui_task, "ui", 4096, nullptr, 4, nullptr);
}
