/* Status LED - the only user-facing output on the sensor since v1.1.
 *
 * The sensor has no display any more: numbers belong on the dashboard and in
 * Apple Home.  What the LED has to answer is the question someone standing
 * next to the printer actually asks - "is the air OK, and is the thing still
 * working?" - with as little light (and current) as possible.
 *
 * Pure logic: ac_status_pattern() maps engine state to a pattern, and
 * ac_status_level() evaluates that pattern at a point in time.  The HAL just
 * copies the result onto three GPIOs.
 */
#ifndef AC_STATUS_H
#define AC_STATUS_H

#include "ac_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AC_RGB_OFF    = 0,
    AC_RGB_RED    = 1 << 0,
    AC_RGB_GREEN  = 1 << 1,
    AC_RGB_BLUE   = 1 << 2,
    AC_RGB_YELLOW = AC_RGB_RED | AC_RGB_GREEN,
    AC_RGB_CYAN   = AC_RGB_GREEN | AC_RGB_BLUE,
    AC_RGB_PURPLE = AC_RGB_RED | AC_RGB_BLUE,
    AC_RGB_WHITE  = AC_RGB_RED | AC_RGB_GREEN | AC_RGB_BLUE,
} ac_rgb_t;

typedef enum {
    AC_LED_EVENT_NONE = 0,
    AC_LED_EVENT_BUTTON,      /* short press: show the air quality */
    AC_LED_EVENT_PAIRING,     /* long press: commissioning window open */
    AC_LED_EVENT_RESET,       /* very long press: factory reset */
    AC_LED_EVENT_BOOT,
} ac_led_event_t;

typedef struct {
    ac_rgb_t colour;
    uint16_t on_ms;           /* 0 = off for the whole period */
    uint16_t period_ms;       /* 0 = steady */
    uint32_t duration_ms;     /* how long the pattern runs, 0 = until replaced */
} ac_led_pattern_t;

/* What the LED should do right now, given the engine and the last user
 * event.  In normal operation the answer is "nothing": a status LED that is
 * lit all the time is both annoying and a measurable share of the budget. */
ac_led_pattern_t ac_status_pattern(const ac_engine_t *e, ac_led_event_t ev);

/* Evaluate a pattern `elapsed_ms` after it started.  Returns the colour to
 * show, or AC_RGB_OFF.  Once duration_ms has passed the pattern is over and
 * the result is always off. */
ac_rgb_t ac_status_level(const ac_led_pattern_t *p, uint32_t elapsed_ms);

/* The air-quality colour on its own: green, yellow, red, purple. */
ac_rgb_t ac_status_air_colour(ac_air_quality_t q);

#ifdef __cplusplus
}
#endif
#endif
