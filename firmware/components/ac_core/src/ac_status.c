#include "ac_core/ac_status.h"

ac_rgb_t ac_status_air_colour(ac_air_quality_t q)
{
    switch (q) {
    case AC_AQ_GOOD:      return AC_RGB_GREEN;
    case AC_AQ_ELEVATED:  return AC_RGB_YELLOW;
    case AC_AQ_HIGH:      return AC_RGB_RED;
    case AC_AQ_VERY_HIGH: return AC_RGB_PURPLE;
    default:              return AC_RGB_WHITE;   /* nothing measured yet */
    }
}

static ac_led_pattern_t pat(ac_rgb_t c, uint16_t on, uint16_t period, uint32_t dur)
{
    ac_led_pattern_t p = { c, on, period, dur };
    return p;
}

ac_led_pattern_t ac_status_pattern(const ac_engine_t *e, ac_led_event_t ev)
{
    /* User-initiated events always win: the user is looking at the device. */
    switch (ev) {
    case AC_LED_EVENT_RESET:
        return pat(AC_RGB_RED, 100, 200, 3000);          /* fast red */
    case AC_LED_EVENT_PAIRING:
        return pat(AC_RGB_BLUE, 500, 1000, 300000);      /* slow blue, 5 min */
    case AC_LED_EVENT_BOOT:
        return pat(AC_RGB_WHITE, 300, 0, 300);
    case AC_LED_EVENT_CALIBRATING:
        /* slow cyan while the SEN63C breathes outdoor air for 3 min */
        return pat(AC_RGB_CYAN, 250, 1000, 240000);
    case AC_LED_EVENT_CAL_OK:
        return pat(AC_RGB_GREEN, 2000, 0, 2000);
    case AC_LED_EVENT_CAL_FAIL:
        return pat(AC_RGB_RED, 100, 200, 2000);
    case AC_LED_EVENT_BUTTON:
        if (e->state == AC_STATE_ERROR)
            return pat(AC_RGB_RED, 100, 400, 3000);      /* red blink = fault */
        if (e->state == AC_STATE_LOW_BATTERY ||
            e->state == AC_STATE_CRITICAL_BATTERY)
            return pat(AC_RGB_YELLOW, 100, 400, 3000);   /* yellow blink = charge me */
        if (!e->warm)
            return pat(AC_RGB_WHITE, 500, 1000, 3000);   /* white pulse = warming up */
        if (!e->cfg.led_show_air_quality)
            return pat(AC_RGB_GREEN, 200, 0, 200);       /* just "alive" */
        return pat(ac_status_air_colour(e->aq.level), 3000, 0, 3000);
    default:
        break;
    }

    /* Unprompted: only things someone must notice without pressing anything.
     * A 50 ms blip every 10 s is visible across a room and costs ~0.01 %
     * of the battery budget. */
    if (e->state == AC_STATE_CRITICAL_BATTERY)
        return pat(AC_RGB_RED, 50, 10000, 0);
    if (e->state == AC_STATE_ERROR)
        return pat(AC_RGB_RED, 50, 5000, 0);
    return pat(AC_RGB_OFF, 0, 0, 0);
}

ac_rgb_t ac_status_hold_colour(uint32_t held_ms)
{
    if (held_ms >= AC_HOLD_ABORT_MS)     return AC_RGB_OFF;
    if (held_ms >= AC_HOLD_RESET_MS)     return AC_RGB_RED;
    if (held_ms >= AC_HOLD_CALIBRATE_MS) return AC_RGB_CYAN;
    if (held_ms >= AC_HOLD_PAIRING_MS)   return AC_RGB_BLUE;
    return AC_RGB_OFF;
}

ac_rgb_t ac_status_level(const ac_led_pattern_t *p, uint32_t elapsed_ms)
{
    if (p->colour == AC_RGB_OFF || p->on_ms == 0) return AC_RGB_OFF;
    if (p->duration_ms && elapsed_ms >= p->duration_ms) return AC_RGB_OFF;
    if (p->period_ms == 0) return p->colour;               /* steady */
    return (elapsed_ms % p->period_ms) < p->on_ms ? p->colour : AC_RGB_OFF;
}
