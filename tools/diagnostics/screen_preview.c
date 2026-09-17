/* Renders every UI screen to a PBM so a human can look at the display before
 * any hardware exists.  Build:
 *   cc -Ifirmware/components/ac_core/include \
 *      firmware/components/ac_core/src/[a-z]*.c \
 *      tools/diagnostics/screen_preview.c -lm -o /tmp/preview
 *   /tmp/preview outdir
 */
#include "ac_core/ac_display.h"
#include <stdio.h>
#include <string.h>

static void write_pbm(const ac_canvas_t *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P1\n%d %d\n", AC_DISP_W, AC_DISP_H);
    for (int y = 0; y < AC_DISP_H; y++) {
        for (int x = 0; x < AC_DISP_W; x++) {
            int on = (c->px[y * AC_DISP_STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1;
            fputc(on ? '1' : '0', f);
            fputc(x == AC_DISP_W - 1 ? '\n' : ' ', f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    ac_config_t cfg;
    ac_config_defaults(&cfg);
    strncpy(cfg.name, "3D PRINTER", AC_NAME_MAX - 1);

    ac_engine_t e;
    ac_engine_init(&e, &cfg, 0);
    ac_time_ms_t t = 0;
    /* a plausible day: quiet room, then a print, then recovery */
    for (int i = 0; i < 900; i++) {
        t += 60000;
        float pm = 6.0f;
        int32_t voc = 95 + (i % 5);
        float co2 = 600.0f + (float)(i % 40) * 3.0f;
        if (i > 600 && i < 700) { pm = 18.0f + (float)((i - 600) % 7); voc = 170; co2 = 900.0f; }
        else if (i >= 700 && i < 760) { pm = 11.0f; voc = 130; co2 = 800.0f; }
        ac_sample_t s;
        memset(&s, 0, sizeof(s));
        s.t = t;
        s.pm1 = pm * 0.72f; s.pm25 = pm; s.pm4 = pm * 1.08f; s.pm10 = pm * 1.25f;
        s.pm_fresh = true;
        s.voc_index = voc; s.voc_fresh = true;
        s.co2 = co2; s.co2_fresh = true;
        s.temperature = 22.4f; s.humidity = 46.0f; s.th_fresh = true;
        s.battery_pct = 87.0f; s.battery_v = 3.91f;
        ac_engine_submit(&e, &s);
        ac_engine_set_power(&e, false, false, 87.0f, 3.91f, t);
        ac_engine_tick(&e, t);
    }

    ac_ui_context_t ui = { true, true, 62, "1.0.0", "AC-0001-2026",
                           "3497-011-2332", "MT:Y.K90AFN00KA0648G00", 2 };
    struct { ac_screen_t s; const char *name; } list[] = {
        { AC_SCREEN_OVERVIEW,   "1-overview" },
        { AC_SCREEN_PARTICLES,  "2-particles" },
        { AC_SCREEN_GAS,        "3-gas" },
        { AC_SCREEN_CLIMATE,    "4-climate" },
        { AC_SCREEN_SYSTEM,     "5-system" },
        { AC_SCREEN_TREND,      "6-trend" },
        { AC_SCREEN_WARMUP,     "7-warmup" },
        { AC_SCREEN_COMMISSION, "8-commission" },
        { AC_SCREEN_DIAGNOSTIC, "9-diagnostic" },
        { AC_SCREEN_CRITICAL,   "10-critical" },
    };
    ac_canvas_t c;
    char path[512];
    for (unsigned i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
        ac_display_render(&c, &e, &ui, list[i].s, t);
        snprintf(path, sizeof(path), "%s/screen-%s.pbm", dir, list[i].name);
        write_pbm(&c, path);
    }
    printf("wrote %zu screens to %s\n", sizeof(list) / sizeof(list[0]), dir);
    return 0;
}
