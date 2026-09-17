/* Display model and renderer.
 *
 * Pure software: it fills a 200 x 200 1-bit framebuffer.  Nothing in here
 * knows that an SSD1681 exists, which is what lets the host test render every
 * screen to a PNG and lets a human look at the result before any hardware is
 * built.
 *
 * Layout rules the screens follow:
 *   - one primary number per screen, 46 px tall, left aligned on a 12 px grid
 *   - its label above it in 12 px, its unit beside it in 20 px
 *   - a status strip along the bottom, always in the same place
 *   - no icons except the battery, and no decoration that does not carry data
 */
#ifndef AC_DISPLAY_H
#define AC_DISPLAY_H

#include "ac_engine.h"
#include "ac_font.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AC_DISP_W 200
#define AC_DISP_H 200
#define AC_DISP_STRIDE ((AC_DISP_W + 7) / 8)
#define AC_DISP_BYTES (AC_DISP_STRIDE * AC_DISP_H)

typedef enum {
    AC_SCREEN_OVERVIEW = 0,
    AC_SCREEN_PARTICLES,
    AC_SCREEN_GAS,
    AC_SCREEN_CLIMATE,
    AC_SCREEN_SYSTEM,
    AC_SCREEN_TREND,
    AC_SCREEN_COUNT,
    /* not part of the button cycle */
    AC_SCREEN_WARMUP = 100,
    AC_SCREEN_COMMISSION,
    AC_SCREEN_DIAGNOSTIC,
    AC_SCREEN_CRITICAL,
    AC_SCREEN_SLEEP,
} ac_screen_t;

typedef struct {
    uint8_t px[AC_DISP_BYTES];   /* 1 = black */
} ac_canvas_t;

/* Extra facts the renderer needs that the engine does not own. */
typedef struct {
    bool thread_attached;
    bool matter_commissioned;
    uint8_t thread_rssi_neg;     /* absolute value of the RSSI in dBm */
    const char *fw_version;
    const char *serial;
    const char *pairing_code;    /* 11 digit manual code, may be NULL */
    const char *qr_payload;      /* MT:... , may be NULL */
    uint32_t events_today;
} ac_ui_context_t;

void ac_canvas_clear(ac_canvas_t *c);
void ac_canvas_pixel(ac_canvas_t *c, int x, int y, bool on);
void ac_canvas_fill(ac_canvas_t *c, int x, int y, int w, int h, bool on);
void ac_canvas_frame(ac_canvas_t *c, int x, int y, int w, int h, int t, bool on);
int  ac_text_width(const ac_font_t *f, const char *s);
int  ac_canvas_text(ac_canvas_t *c, const ac_font_t *f, int x, int y_baseline,
                    const char *s, bool on);
/* x is the right edge */
int  ac_canvas_text_right(ac_canvas_t *c, const ac_font_t *f, int x_right,
                          int y_baseline, const char *s, bool on);

void ac_display_render(ac_canvas_t *c, const ac_engine_t *e,
                       const ac_ui_context_t *ui, ac_screen_t screen,
                       ac_time_ms_t now);
const char *ac_screen_title(ac_screen_t s);

#ifdef __cplusplus
}
#endif
#endif
