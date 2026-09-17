#include "ac_core/ac_display.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GRID 12          /* left margin and the unit of vertical rhythm */
/* Three non-ASCII glyphs live in control codes 1..3, see tools/fontgen. */
#define MICRO  "\x01"
#define DEGREE "\x02"
#define CUBED  "\x03"
#define STATUS_Y 186     /* baseline of the bottom status strip */
#define RULE_Y   172

void ac_canvas_clear(ac_canvas_t *c) { memset(c->px, 0, sizeof(c->px)); }

void ac_canvas_pixel(ac_canvas_t *c, int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= AC_DISP_W || y >= AC_DISP_H) return;
    uint8_t *p = &c->px[y * AC_DISP_STRIDE + (x >> 3)];
    uint8_t m = (uint8_t)(0x80u >> (x & 7));
    if (on) *p |= m; else *p = (uint8_t)(*p & ~m);
}

void ac_canvas_fill(ac_canvas_t *c, int x, int y, int w, int h, bool on)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            ac_canvas_pixel(c, i, j, on);
}

void ac_canvas_frame(ac_canvas_t *c, int x, int y, int w, int h, int t, bool on)
{
    ac_canvas_fill(c, x, y, w, t, on);
    ac_canvas_fill(c, x, y + h - t, w, t, on);
    ac_canvas_fill(c, x, y, t, h, on);
    ac_canvas_fill(c, x + w - t, y, t, h, on);
}

static const uint8_t *glyph(const ac_font_t *f, unsigned char ch,
                            int *w, int *adv)
{
    if (ch < f->first || ch > f->last) { *w = 0; *adv = 0; return NULL; }
    unsigned i = (unsigned)(ch - f->first);
    *w = f->width[i];
    *adv = f->advance[i];
    return &f->bits[f->offset[i]];
}

int ac_text_width(const ac_font_t *f, const char *s)
{
    int x = 0, w, adv;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        glyph(f, *p, &w, &adv);
        x += adv;
    }
    return x;
}

int ac_canvas_text(ac_canvas_t *c, const ac_font_t *f, int x, int y_baseline,
                   const char *s, bool on)
{
    int nbytes = (f->height + 7) / 8;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        int w, adv;
        const uint8_t *g = glyph(f, *p, &w, &adv);
        if (g) {
            for (int col = 0; col < w; col++) {
                const uint8_t *cb = g + (size_t)col * nbytes;
                for (int row = 0; row < f->height; row++) {
                    if (cb[row >> 3] & (0x80u >> (row & 7)))
                        ac_canvas_pixel(c, x + col,
                                        y_baseline - f->baseline + row, on);
                }
            }
        }
        x += adv;
    }
    return x;
}

int ac_canvas_text_right(ac_canvas_t *c, const ac_font_t *f, int x_right,
                         int y_baseline, const char *s, bool on)
{
    return ac_canvas_text(c, f, x_right - ac_text_width(f, s), y_baseline, s, on);
}

/* ------------------------------------------------------------------ */

static void fmt_f(char *buf, size_t n, float v, int dp)
{
    if (v < 0.0f || !(v == v)) { snprintf(buf, n, "--"); return; }
    snprintf(buf, n, "%.*f", dp, (double)v);
}

static void fmt_i(char *buf, size_t n, int32_t v)
{
    if (v < 0) { snprintf(buf, n, "--"); return; }
    snprintf(buf, n, "%ld", (long)v);
}

static void battery_icon(ac_canvas_t *c, int x, int y, float pct, bool charging)
{
    const int w = 22, h = 11;
    ac_canvas_frame(c, x, y, w, h, 1, true);
    ac_canvas_fill(c, x + w, y + 3, 2, h - 6, true);
    if (pct > 0.0f) {
        int fillw = (int)((float)(w - 4) * pct / 100.0f + 0.5f);
        if (fillw > w - 4) fillw = w - 4;
        ac_canvas_fill(c, x + 2, y + 2, fillw, h - 4, true);
    }
    if (charging) {
        /* a small bolt cut out of the fill so it reads at any level */
        for (int i = 0; i < 5; i++) {
            ac_canvas_pixel(c, x + 11 - i / 2, y + 2 + i, false);
            ac_canvas_pixel(c, x + 12 - i / 2, y + 2 + i, false);
        }
        for (int i = 0; i < 4; i++) {
            ac_canvas_pixel(c, x + 10 + i / 2, y + 5 + i, false);
            ac_canvas_pixel(c, x + 11 + i / 2, y + 5 + i, false);
        }
    }
}

static void header(ac_canvas_t *c, const ac_engine_t *e, const char *title)
{
    ac_canvas_text(c, &ac_font_small, GRID, 16, title, true);
    char buf[16];
    snprintf(buf, sizeof(buf), "%s", ac_mode_name(e->mode));
    ac_canvas_text_right(c, &ac_font_small, AC_DISP_W - GRID, 16, buf, true);
    ac_canvas_fill(c, GRID, 22, AC_DISP_W - 2 * GRID, 1, true);
}

static void status_strip(ac_canvas_t *c, const ac_engine_t *e,
                         const ac_ui_context_t *ui)
{
    ac_canvas_fill(c, GRID, RULE_Y, AC_DISP_W - 2 * GRID, 1, true);
    char buf[24];
    const char *net = !ui->matter_commissioned ? "NOT PAIRED"
                      : (ui->thread_attached ? "THREAD" : "OFFLINE");
    ac_canvas_text(c, &ac_font_small, GRID, STATUS_Y, net, true);

    float pct = e->last.battery_pct;
    if (pct >= 0.0f) snprintf(buf, sizeof(buf), "%d%%", (int)(pct + 0.5f));
    else snprintf(buf, sizeof(buf), "--");
    int tw = ac_text_width(&ac_font_small, buf);
    ac_canvas_text(c, &ac_font_small, AC_DISP_W - GRID - tw, STATUS_Y, buf, true);
    battery_icon(c, AC_DISP_W - GRID - tw - 32, STATUS_Y - 10, pct, e->charging);
}

/* A big value with its label above and unit to the right. */
static void hero(ac_canvas_t *c, const char *label, const char *value,
                 const char *unit, int y_baseline)
{
    ac_canvas_text(c, &ac_font_small, GRID, y_baseline - 52, label, true);
    int x = ac_canvas_text(c, &ac_font_large, GRID, y_baseline, value, true);
    if (unit && *unit) {
        const ac_font_t *f = &ac_font_medium;
        if (x + 6 + ac_text_width(f, unit) > AC_DISP_W - GRID) f = &ac_font_small;
        ac_canvas_text(c, f, x + 6, y_baseline, unit, true);
    }
}

/* Label on the left, value right aligned.  The value drops to the small face
 * rather than running into the label - a collision here is the classic way a
 * fixed layout stops being readable the first time a real value is longer
 * than the mock-up's. */
static void kv_row(ac_canvas_t *c, int y, const char *k, const char *v)
{
    int kw = ac_text_width(&ac_font_small, k);
    ac_canvas_text(c, &ac_font_small, GRID, y, k, true);
    int room = AC_DISP_W - 2 * GRID - kw - 6;
    const ac_font_t *f = (ac_text_width(&ac_font_medium, v) <= room)
                         ? &ac_font_medium : &ac_font_small;
    ac_canvas_text_right(c, f, AC_DISP_W - GRID, y, v, true);
}

const char *ac_screen_title(ac_screen_t s)
{
    switch (s) {
    case AC_SCREEN_OVERVIEW:  return "AIR CHECK";
    case AC_SCREEN_PARTICLES: return "PARTICLES";
    case AC_SCREEN_GAS:       return "VOC / CO2";
    case AC_SCREEN_CLIMATE:   return "CLIMATE";
    case AC_SCREEN_SYSTEM:    return "SYSTEM";
    case AC_SCREEN_TREND:     return "TREND";
    default:                  return "AIR CHECK";
    }
}

static void screen_overview(ac_canvas_t *c, const ac_engine_t *e,
                            const ac_ui_context_t *ui)
{
    char v[16], r[40];
    header(c, e, e->cfg.name[0] ? e->cfg.name : "AIR CHECK");

    fmt_f(v, sizeof(v), e->last.pm25, e->last.pm25 < 10.0f ? 1 : 0);
    hero(c, "PM2.5", v, MICRO "g/m" CUBED, 92);

    /* air quality band, inverted so it reads as a chip */
    const char *q = ac_air_quality_name(e->aq.level);
    int qw = ac_text_width(&ac_font_medium, q) + 16;
    ac_canvas_fill(c, GRID, 104, qw, 26, true);
    ac_canvas_text(c, &ac_font_medium, GRID + 8, 124, q, false);

    ac_reason_text(e->aq.reason, r, sizeof(r));
    if (r[0] && r[0] != '-')
        ac_canvas_text(c, &ac_font_small, GRID + qw + 8, 122, r, true);

    fmt_i(v, sizeof(v), e->last.voc_index);
    char line[40];
    snprintf(line, sizeof(line), "VOC %s", v);
    ac_canvas_text(c, &ac_font_small, GRID, 150, line, true);
    fmt_f(v, sizeof(v), e->last.co2, 0);
    snprintf(line, sizeof(line), "CO2 %s ppm", v);
    ac_canvas_text_right(c, &ac_font_small, AC_DISP_W - GRID, 150, line, true);
    fmt_f(v, sizeof(v), e->last.temperature, 1);
    snprintf(line, sizeof(line), "%s " DEGREE "C", v);
    ac_canvas_text(c, &ac_font_small, GRID, 166, line, true);
    fmt_f(v, sizeof(v), e->last.humidity, 0);
    snprintf(line, sizeof(line), "%s %% RH", v);
    ac_canvas_text_right(c, &ac_font_small, AC_DISP_W - GRID, 166, line, true);

    status_strip(c, e, ui);
}

static void screen_particles(ac_canvas_t *c, const ac_engine_t *e,
                             const ac_ui_context_t *ui)
{
    char v[16], line[32];
    header(c, e, "PARTICLES");
    fmt_f(v, sizeof(v), e->last.pm25, e->last.pm25 < 10.0f ? 1 : 0);
    hero(c, "PM2.5", v, MICRO "g/m" CUBED, 92);

    fmt_f(v, sizeof(v), e->last.pm1, 1);  kv_row(c, 118, "PM1.0", v);
    fmt_f(v, sizeof(v), e->last.pm4, 1);  kv_row(c, 138, "PM4", v);
    fmt_f(v, sizeof(v), e->last.pm10, 1); kv_row(c, 158, "PM10", v);

    if (e->cfg.display_show_delta && ac_baseline_valid(&e->base)) {
        float d = ac_baseline_d_pm25(&e->base, &e->last);
        snprintf(line, sizeof(line), "%+.1f vs base", (double)d);
        ac_canvas_text_right(c, &ac_font_small, AC_DISP_W - GRID, 40, line, true);
    }
    status_strip(c, e, ui);
}

static void screen_gas(ac_canvas_t *c, const ac_engine_t *e,
                       const ac_ui_context_t *ui)
{
    char v[16], line[32];
    header(c, e, "VOC / CO2");
    fmt_i(v, sizeof(v), e->last.voc_index);
    hero(c, "VOC INDEX", v, "", 92);
    if (ac_baseline_valid(&e->base)) {
        snprintf(line, sizeof(line), "base %ld",
                 (long)ac_baseline_voc(&e->base));
        ac_canvas_text_right(c, &ac_font_small, AC_DISP_W - GRID, 92, line, true);
    }
    ac_canvas_text_right(c, &ac_font_small, AC_DISP_W - GRID, 166,
                         "100 = 24 h average", true);
    fmt_f(v, sizeof(v), e->last.co2, 0);
    ac_canvas_text(c, &ac_font_small, GRID, 124, "CO2", true);
    int x = ac_canvas_text(c, &ac_font_medium, GRID, 150, v, true);
    ac_canvas_text(c, &ac_font_small, x + 6, 150, "ppm", true);
    status_strip(c, e, ui);
}

static void screen_climate(ac_canvas_t *c, const ac_engine_t *e,
                           const ac_ui_context_t *ui)
{
    char v[16];
    header(c, e, "CLIMATE");
    fmt_f(v, sizeof(v), e->last.temperature, 1);
    hero(c, "TEMPERATURE", v, DEGREE "C", 92);
    fmt_f(v, sizeof(v), e->last.humidity, 0);
    ac_canvas_text(c, &ac_font_small, GRID, 124, "HUMIDITY", true);
    int x = ac_canvas_text(c, &ac_font_medium, GRID, 152, v, true);
    ac_canvas_text(c, &ac_font_small, x + 6, 152, "% RH", true);
    ac_canvas_text_right(c, &ac_font_small, AC_DISP_W - GRID, 152,
                         "SCD41", true);
    status_strip(c, e, ui);
}

static void screen_system(ac_canvas_t *c, const ac_engine_t *e,
                          const ac_ui_context_t *ui, ac_time_ms_t now)
{
    char v[24];
    header(c, e, "SYSTEM");
    fmt_f(v, sizeof(v), e->last.battery_pct, 0);
    hero(c, "BATTERY", v, "%", 96);

    snprintf(v, sizeof(v), "%.2f V", (double)e->last.battery_v);
    kv_row(c, 118, e->charging ? "CHARGING" : "VOLTAGE", v);
    kv_row(c, 138, "THREAD", ui->thread_attached ? "UP" : "DOWN");
    kv_row(c, 158, "MATTER", ui->matter_commissioned ? "PAIRED" : "NOT PAIRED");
    uint32_t up = ac_engine_uptime_s(e, now);
    snprintf(v, sizeof(v), "%ud %02uh", up / 86400u, (up % 86400u) / 3600u);
    /* shares the hero label's line, right aligned, so it cannot collide */
    ac_canvas_text_right(c, &ac_font_small, AC_DISP_W - GRID, 44, v, true);
    status_strip(c, e, ui);
}

static void screen_trend(ac_canvas_t *c, const ac_engine_t *e,
                         const ac_ui_context_t *ui)
{
    header(c, e, "TREND");

    /* 24 h PM2.5 sparkline from the fine history ring */
    ac_hist_bucket_t buf[AC_HIST_FINE_N];
    size_t n = ac_history_recent_fine(&e->hist, buf, AC_HIST_FINE_N);
    const int gx = GRID, gy = 40, gw = AC_DISP_W - 2 * GRID, gh = 74;
    ac_canvas_fill(c, gx, gy + gh, gw, 1, true);
    ac_canvas_fill(c, gx, gy, 1, gh + 1, true);

    if (n >= 2) {
        float mx = 1.0f;
        for (size_t i = 0; i < n; i++)
            if (buf[i].pm25_max != AC_HIST_NODATA &&
                buf[i].pm25_max / 10.0f > mx) mx = buf[i].pm25_max / 10.0f;
        int prev = -1;
        for (size_t i = 0; i < n; i++) {
            if (buf[i].pm25_mean == AC_HIST_NODATA) continue;
            int x = gx + 1 + (int)((float)i * (float)(gw - 2) / (float)(n - 1));
            int y = gy + gh - (int)((buf[i].pm25_mean / 10.0f) / mx * (float)gh);
            if (prev >= 0)
                for (int yy = (y < prev ? y : prev); yy <= (y < prev ? prev : y); yy++)
                    ac_canvas_pixel(c, x, yy, true);
            ac_canvas_pixel(c, x, y, true);
            prev = y;
        }
        char lbl[24];
        snprintf(lbl, sizeof(lbl), "max %.0f", (double)mx);
        ac_canvas_text_right(c, &ac_font_small, AC_DISP_W - GRID, gy - 2, lbl, true);
    } else {
        ac_canvas_text(c, &ac_font_small, gx + 4, gy + gh / 2,
                       "COLLECTING DATA", true);
    }
    ac_canvas_text(c, &ac_font_small, gx, gy - 2, "PM2.5, 24 h", true);

    char v[32];
    float tr = ac_history_pm25_trend(&e->hist, 180);
    snprintf(v, sizeof(v), "%+.1f/h", (double)tr);
    kv_row(c, 138, "3 H TREND", v);
    snprintf(v, sizeof(v), "%s", ac_event_state_name(e->ev.state));
    kv_row(c, 160, "EVENT", v);
    status_strip(c, e, ui);
}

static void screen_warmup(ac_canvas_t *c, const ac_engine_t *e,
                          const ac_ui_context_t *ui, ac_time_ms_t now)
{
    header(c, e, e->cfg.name[0] ? e->cfg.name : "AIR CHECK");
    ac_canvas_text(c, &ac_font_medium, GRID, 90, "WARMING UP", true);
    char v[40];
    uint32_t up = ac_engine_uptime_s(e, now);
    uint32_t togo = up < AC_SGP40_USABLE_S ? AC_SGP40_USABLE_S - up : 0;
    snprintf(v, sizeof(v), "%lu s remaining", (unsigned long)togo);
    ac_canvas_text(c, &ac_font_small, GRID, 116, v, true);
    ac_canvas_text(c, &ac_font_small, GRID, 138,
                   "No values are published", true);
    ac_canvas_text(c, &ac_font_small, GRID, 152,
                   "until they are valid.", true);
    status_strip(c, e, ui);
}

static void screen_commission(ac_canvas_t *c, const ac_engine_t *e,
                              const ac_ui_context_t *ui)
{
    header(c, e, "ADD TO APPLE HOME");
    ac_canvas_text(c, &ac_font_small, GRID, 46,
                   "Scan the code on the back,", true);
    ac_canvas_text(c, &ac_font_small, GRID, 60,
                   "or enter this pairing code:", true);
    if (ui->pairing_code)
        ac_canvas_text(c, &ac_font_medium, GRID, 100, ui->pairing_code, true);
    ac_canvas_text(c, &ac_font_small, GRID, 130,
                   "Needs a HomePod or Apple TV", true);
    ac_canvas_text(c, &ac_font_small, GRID, 144,
                   "as the Thread border router.", true);
    status_strip(c, e, ui);
}

static void screen_diagnostic(ac_canvas_t *c, const ac_engine_t *e,
                              const ac_ui_context_t *ui)
{
    char v[32];
    header(c, e, "DIAGNOSTICS");
    int y = 44;
    snprintf(v, sizeof(v), "%s", ui->fw_version ? ui->fw_version : "?");
    kv_row(c, y, "FW", v); y += 18;
    snprintf(v, sizeof(v), "%.2f V  %.0f%%", (double)e->last.battery_v,
             (double)e->last.battery_pct);
    kv_row(c, y, "BATT", v); y += 18;
    kv_row(c, y, "SPS30", e->health.sps30_ok ? "OK" : "FAIL"); y += 18;
    kv_row(c, y, "SGP40", e->health.sgp40_ok ? "OK" : "FAIL"); y += 18;
    kv_row(c, y, "SCD41", e->health.scd41_ok ? "OK" : "FAIL"); y += 18;
    snprintf(v, sizeof(v), "%s -%u dBm",
             ui->thread_attached ? "UP" : "DN", ui->thread_rssi_neg);
    kv_row(c, y, "NET", v); y += 18;
    if (ui->serial)
        ac_canvas_text(c, &ac_font_small, GRID, y, ui->serial, true);
    status_strip(c, e, ui);
}

static void screen_critical(ac_canvas_t *c, const ac_engine_t *e,
                            const ac_ui_context_t *ui)
{
    header(c, e, e->cfg.name[0] ? e->cfg.name : "AIR CHECK");
    ac_canvas_fill(c, GRID, 60, AC_DISP_W - 2 * GRID, 34, true);
    ac_canvas_text(c, &ac_font_medium, GRID + 10, 86, "LOW BATTERY", false);
    ac_canvas_text(c, &ac_font_small, GRID, 116,
                   "Measurements are paused.", true);
    ac_canvas_text(c, &ac_font_small, GRID, 132,
                   "Connect USB-C to continue.", true);
    status_strip(c, e, ui);
}

void ac_display_render(ac_canvas_t *c, const ac_engine_t *e,
                       const ac_ui_context_t *ui, ac_screen_t screen,
                       ac_time_ms_t now)
{
    ac_canvas_clear(c);
    switch (screen) {
    case AC_SCREEN_OVERVIEW:   screen_overview(c, e, ui); break;
    case AC_SCREEN_PARTICLES:  screen_particles(c, e, ui); break;
    case AC_SCREEN_GAS:        screen_gas(c, e, ui); break;
    case AC_SCREEN_CLIMATE:    screen_climate(c, e, ui); break;
    case AC_SCREEN_SYSTEM:     screen_system(c, e, ui, now); break;
    case AC_SCREEN_TREND:      screen_trend(c, e, ui); break;
    case AC_SCREEN_WARMUP:     screen_warmup(c, e, ui, now); break;
    case AC_SCREEN_COMMISSION: screen_commission(c, e, ui); break;
    case AC_SCREEN_DIAGNOSTIC: screen_diagnostic(c, e, ui); break;
    case AC_SCREEN_CRITICAL:   screen_critical(c, e, ui); break;
    default:                   screen_overview(c, e, ui); break;
    }
}
