/* Service console - see ac_console.h for the protocol. */
#include "ac_console.h"

#include "ac_hal/ac_hal.h"

#include <esp_console.h>
#include <esp_log.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

static const char *TAG = "console";
static ac_console_ctx_t s_ctx;
static bool s_started;

/* ------------------------------------------------------------------ */
/* the settings a user may change, and where they live in ac_config_t */

typedef enum { T_STR, T_BOOL, T_F32, T_I32, T_U32, T_U8, T_MODE } kind_t;

typedef struct {
    const char *key;
    kind_t kind;
    size_t off;
} setting_t;

#define S(k, t) { #k, t, offsetof(ac_config_t, k) }
static const setting_t k_settings[] = {
    S(name, T_STR),               S(location, T_STR),
    S(default_mode, T_MODE),
    S(pm25_elevated, T_F32),      S(pm25_high, T_F32),     S(pm25_very_high, T_F32),
    S(pm10_elevated, T_F32),      S(pm10_high, T_F32),     S(pm10_very_high, T_F32),
    S(voc_elevated, T_I32),       S(voc_high, T_I32),      S(voc_very_high, T_I32),
    S(co2_elevated, T_F32),       S(co2_high, T_F32),      S(co2_very_high, T_F32),
    S(ev_sensitivity, T_U8),      S(ev_confirm_s, T_U32),  S(ev_release_s, T_U32),
    S(post_event_s, T_U32),
    S(led_show_air_quality, T_BOOL),
    S(battery_interval_s, T_U32),
    S(low_battery_pct, T_F32),    S(critical_battery_pct, T_F32),
    S(voc_publish_index_as_ppb, T_BOOL),
    S(co2_self_calibration, T_BOOL),
    S(auto_escalate, T_BOOL),
};
#undef S

static const setting_t *find(const char *key)
{
    for (const setting_t &s : k_settings)
        if (strcmp(s.key, key) == 0) return &s;
    return nullptr;
}

static void print_setting(const ac_config_t *c, const setting_t *s)
{
    const uint8_t *base = (const uint8_t *)c + s->off;
    switch (s->kind) {
    case T_STR:  printf("%s = %s\n", s->key, (const char *)base); break;
    case T_BOOL: printf("%s = %s\n", s->key, *(const bool *)base ? "true" : "false"); break;
    case T_F32:  printf("%s = %g\n", s->key, (double)*(const float *)base); break;
    case T_I32:  printf("%s = %ld\n", s->key, (long)*(const int32_t *)base); break;
    case T_U32:  printf("%s = %lu\n", s->key, (unsigned long)*(const uint32_t *)base); break;
    case T_U8:   printf("%s = %u\n", s->key, (unsigned)*(const uint8_t *)base); break;
    case T_MODE: printf("%s = %s\n", s->key, ac_mode_name(*(const ac_mode_t *)base)); break;
    }
}

static bool parse_bool(const char *v, bool *out)
{
    if (!strcasecmp(v, "true") || !strcmp(v, "1") || !strcasecmp(v, "on"))   { *out = true;  return true; }
    if (!strcasecmp(v, "false") || !strcmp(v, "0") || !strcasecmp(v, "off")) { *out = false; return true; }
    return false;
}

static bool apply(ac_config_t *c, const setting_t *s, const char *v)
{
    uint8_t *base = (uint8_t *)c + s->off;
    char *end = nullptr;
    switch (s->kind) {
    case T_STR:
        strncpy((char *)base, v, AC_NAME_MAX - 1);
        ((char *)base)[AC_NAME_MAX - 1] = '\0';
        return true;
    case T_BOOL:
        return parse_bool(v, (bool *)base);
    case T_F32: {
        float f = strtof(v, &end);
        if (end == v || *end) return false;
        *(float *)base = f;
        return true;
    }
    case T_I32: case T_U32: case T_U8: {
        long n = strtol(v, &end, 10);
        if (end == v || *end || n < 0) return false;
        if (s->kind == T_I32) *(int32_t *)base = (int32_t)n;
        else if (s->kind == T_U32) *(uint32_t *)base = (uint32_t)n;
        else { if (n > 255) return false; *(uint8_t *)base = (uint8_t)n; }
        return true;
    }
    case T_MODE:
        for (int m = 0; m < AC_MODE_COUNT; m++)
            if (!strcasecmp(v, ac_mode_name((ac_mode_t)m))) {
                *(ac_mode_t *)base = (ac_mode_t)m;
                return true;
            }
        return false;
    }
    return false;
}

/* ------------------------------------------------------------------ */

static int cmd_config(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "get")) {
        s_ctx.lock();
        ac_config_t c = s_ctx.engine->cfg;
        s_ctx.unlock();
        for (const setting_t &s : k_settings) print_setting(&c, &s);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "set")) {
        const setting_t *s = find(argv[2]);
        if (!s) { printf("unknown setting '%s'\n", argv[2]); return 1; }
        /* names may contain spaces: join the rest of the line back up */
        char value[AC_NAME_MAX * 2] = "";
        for (int i = 3; i < argc; i++) {
            if (i > 3) strlcat(value, " ", sizeof(value));
            strlcat(value, argv[i], sizeof(value));
        }
        s_ctx.lock();
        ac_config_t c = s_ctx.engine->cfg;
        bool ok = apply(&c, s, value);
        int fixed = 0;
        if (ok) {
            if (!strcmp(s->key, "ev_sensitivity"))
                ac_config_apply_sensitivity(&c, c.ev_sensitivity);
            fixed = ac_config_validate(&c);
            s_ctx.engine->cfg = c;
        }
        s_ctx.unlock();
        if (!ok) { printf("cannot parse '%s' for %s\n", value, s->key); return 1; }
        s_ctx.config_changed();
        print_setting(&c, s);
        if (fixed) printf("(%d value(s) were clamped into range)\n", fixed);
        return 0;
    }
    printf("usage: config get | config set <key> <value>\n");
    return 1;
}

static int cmd_baseline(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "reset")) {
        s_ctx.lock();
        ac_engine_baseline_reset(s_ctx.engine);
        s_ctx.unlock();
        printf("baseline set to the current air (no sensor was calibrated)\n");
        return 0;
    }
    printf("usage: baseline reset\n");
    return 1;
}

static int cmd_co2(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "frc")) {
        long ppm = strtol(argv[2], nullptr, 10);
        if (ppm < 400 || ppm > 2000) { printf("ppm must be 400..2000\n"); return 1; }
        s_ctx.request_frc((uint16_t)ppm);
        printf("calibration queued: 3 min in the current air, then recalibrate "
               "to %ld ppm. Only do this outdoors or at a wide-open window.\n", ppm);
        return 0;
    }
    printf("usage: co2 frc <ppm>   (outdoors, typically 425)\n");
    return 1;
}

static int cmd_events(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32_t n = ac_store_event_count();
    printf("%lu stored event(s)\n", (unsigned long)n);
    for (uint32_t i = 0; i < n; i++) {
        ac_event_record_t r;
        if (ac_store_event_get(i, &r) != ESP_OK) continue;
        printf("#%lu  %lu s, recovery %lu s, peak PM2.5 %.1f (base %.1f), "
               "VOC %ld (base %ld), CO2 +%.0f ppm\n",
               (unsigned long)i, (unsigned long)r.duration_s,
               (unsigned long)r.recovery_s, (double)r.peak_pm25,
               (double)r.base_pm25, (long)r.peak_voc, (long)r.base_voc,
               (double)r.co2_delta);
    }
    return 0;
}

static int cmd_diag(int argc, char **argv)
{
    (void)argc; (void)argv;
    s_ctx.lock();
    const ac_engine_t &e = *s_ctx.engine;
    printf("state %s, mode %s, air %s\n", ac_device_state_name(e.state),
           ac_mode_name(e.mode), ac_air_quality_name(e.aq.level));
    printf("PM1 %.1f  PM2.5 %.1f  PM10 %.1f ug/m3  CO2 %.0f ppm  VOC %ld  "
           "%.1f C  %.0f %%RH\n", (double)e.last.pm1, (double)e.last.pm25,
           (double)e.last.pm10, (double)e.last.co2, (long)e.last.voc_index,
           (double)e.last.temperature, (double)e.last.humidity);
    printf("battery %.2f V, %.0f %%, usb %s\n", (double)e.last.battery_v,
           (double)e.last.battery_pct, e.usb_present ? "yes" : "no");
    printf("health: SEN63C %s (%u errors), SGP40 %s (%u errors), battery %s\n",
           e.health.sen6x_ok ? "ok" : "DOWN", e.health.sen6x_errors,
           e.health.sgp40_ok ? "ok" : "DOWN", e.health.sgp40_errors,
           e.health.battery_ok ? "ok" : "DOWN");
    if (e.health.last_error[0]) printf("last error: %s\n", e.health.last_error);
    s_ctx.unlock();
    printf("thread %s\n", s_ctx.thread_attached() ? "attached" : "not attached");
    return 0;
}

void ac_console_start(const ac_console_ctx_t *ctx)
{
    if (s_started) return;
    s_ctx = *ctx;

    esp_console_repl_t *repl = nullptr;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "aircheck>";
    rc.max_cmdline_length = 128;
    esp_console_dev_usb_serial_jtag_config_t hw =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&hw, &rc, &repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no console: %s", esp_err_to_name(err));
        return;
    }

    const esp_console_cmd_t cmds[] = {
        { .command = "config",   .help = "config get | config set <key> <value>",
          .hint = nullptr, .func = cmd_config, .argtable = nullptr },
        { .command = "baseline", .help = "baseline reset",
          .hint = nullptr, .func = cmd_baseline, .argtable = nullptr },
        { .command = "co2",      .help = "co2 frc <ppm> - fresh-air calibration, outdoors only",
          .hint = nullptr, .func = cmd_co2, .argtable = nullptr },
        { .command = "events",   .help = "events dump",
          .hint = nullptr, .func = cmd_events, .argtable = nullptr },
        { .command = "diag",     .help = "state, values, health",
          .hint = nullptr, .func = cmd_diag, .argtable = nullptr },
    };
    for (const esp_console_cmd_t &c : cmds) esp_console_cmd_register(&c);
    err = esp_console_start_repl(repl);
    if (err == ESP_OK) {
        s_started = true;
        ESP_LOGI(TAG, "service console on USB; type 'help'");
    }
}
