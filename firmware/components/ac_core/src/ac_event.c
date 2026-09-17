#include "ac_core/ac_event.h"
#include <string.h>

void ac_event_init(ac_event_det_t *d)
{
    memset(d, 0, sizeof(*d));
    d->state = AC_EV_IDLE;
}

static void enter(ac_event_det_t *d, ac_event_state_t s, ac_time_ms_t t)
{
    d->state = s;
    d->state_since = t;
}

static bool triggered(const ac_config_t *cfg, const ac_baseline_t *base,
                      const ac_sample_t *s, float pm25_rate, float voc_rate)
{
    /* Without a baseline we have nothing to compare against, and firing on an
     * absolute number would make every device in a dusty workshop sit in
     * ACTIVE forever. */
    if (!ac_baseline_valid(base)) return false;

    bool pm_hit = false, voc_hit = false;
    if (s->pm25 >= 0.0f) {
        float d_pm = ac_baseline_d_pm25(base, s);
        pm_hit = (d_pm >= cfg->ev_pm25_delta) || (pm25_rate >= cfg->ev_pm25_rate);
    }
    if (s->voc_index >= 0) {
        int32_t d_voc = ac_baseline_d_voc(base, s);
        voc_hit = (d_voc >= cfg->ev_voc_delta) ||
                  (voc_rate >= (float)cfg->ev_voc_delta / 10.0f);
    }
    /* Either channel alone is enough to *arm* the detector, because in ECO the
     * PM channel only samples every few hours while VOC samples every 10 s -
     * the cheap channel is the tripwire, the expensive one is the measurement. */
    return pm_hit || voc_hit;
}

static void track_peaks(ac_event_det_t *d, const ac_sample_t *s)
{
    bool newpeak = false;
    if (s->pm_fresh) {
        if (s->pm1  > d->cur.peak_pm1)  d->cur.peak_pm1  = s->pm1;
        if (s->pm10 > d->cur.peak_pm10) d->cur.peak_pm10 = s->pm10;
        if (s->pm25 > d->cur.peak_pm25) { d->cur.peak_pm25 = s->pm25; newpeak = true; }
    }
    if (s->voc_fresh && s->voc_index > d->cur.peak_voc) {
        d->cur.peak_voc = s->voc_index;
        newpeak = true;
    }
    if (s->co2_fresh && s->co2 > d->cur.peak_co2) d->cur.peak_co2 = s->co2;
    if (newpeak) d->peak_ms = s->t;
}

bool ac_event_update(ac_event_det_t *d, const ac_config_t *cfg,
                     const ac_baseline_t *base, const ac_sample_t *s,
                     float pm25_rate, float voc_rate,
                     ac_event_record_t *finished)
{
    ac_event_state_t prev = d->state;
    bool trig = triggered(cfg, base, s, pm25_rate, voc_rate);

    if (trig) {
        if (d->trigger_since == 0) d->trigger_since = s->t;
        d->clear_since = 0;
    } else {
        if (d->clear_since == 0) d->clear_since = s->t;
        d->trigger_since = 0;
    }

    const ac_time_ms_t confirm_ms = (ac_time_ms_t)cfg->ev_confirm_s * 1000u;
    const ac_time_ms_t release_ms = (ac_time_ms_t)cfg->ev_release_s * 1000u;
    const ac_time_ms_t post_ms    = (ac_time_ms_t)cfg->post_event_s * 1000u;

    switch (d->state) {
    case AC_EV_IDLE:
        if (trig) {
            memset(&d->cur, 0, sizeof(d->cur));
            d->cur.started_ms = s->t;
            d->cur.base_pm25 = ac_baseline_pm25(base);
            d->cur.base_voc  = ac_baseline_voc(base);
            d->cur.base_co2  = ac_baseline_co2(base);
            d->cur.peak_voc  = -1;
            d->peak_ms = s->t;
            track_peaks(d, s);
            enter(d, AC_EV_POSSIBLE_PRINT, s->t);
        }
        break;

    case AC_EV_POSSIBLE_PRINT:
        track_peaks(d, s);
        if (!trig && d->clear_since && s->t - d->clear_since >= confirm_ms) {
            /* it did not hold: forget it, no event is recorded */
            enter(d, AC_EV_IDLE, s->t);
        } else if (trig && d->trigger_since &&
                   s->t - d->trigger_since >= confirm_ms) {
            d->events_total++;
            enter(d, AC_EV_ACTIVE, s->t);
        }
        break;

    case AC_EV_ACTIVE:
        track_peaks(d, s);
        if (!trig && d->clear_since && s->t - d->clear_since >= release_ms) {
            d->cur.duration_s = (uint32_t)((s->t - d->cur.started_ms) / 1000u);
            enter(d, AC_EV_POST_PRINT, s->t);
        }
        break;

    case AC_EV_POST_PRINT:
        track_peaks(d, s);
        if (trig && d->trigger_since && s->t - d->trigger_since >= confirm_ms) {
            /* it started again before we finished recovering */
            enter(d, AC_EV_ACTIVE, s->t);
        } else if (s->t - d->state_since >= post_ms) {
            enter(d, AC_EV_NORMALIZED, s->t);
        }
        break;

    case AC_EV_NORMALIZED:
        d->cur.recovery_s = (uint32_t)((s->t - d->peak_ms) / 1000u);
        d->cur.co2_delta = (d->cur.peak_co2 > 0.0f && d->cur.base_co2 > 0.0f)
                           ? (d->cur.peak_co2 - d->cur.base_co2) : 0.0f;
        d->cur.complete = true;
        if (finished) *finished = d->cur;
        enter(d, AC_EV_IDLE, s->t);
        break;

    default:
        enter(d, AC_EV_IDLE, s->t);
        break;
    }

    return d->state != prev;
}

bool ac_event_should_freeze_baseline(const ac_event_det_t *d)
{
    return d->state == AC_EV_POSSIBLE_PRINT ||
           d->state == AC_EV_ACTIVE ||
           d->state == AC_EV_POST_PRINT;
}

ac_mode_t ac_event_preferred_mode(const ac_event_det_t *d, const ac_config_t *cfg)
{
    if (!cfg->auto_escalate) return cfg->default_mode;
    switch (d->state) {
    case AC_EV_POSSIBLE_PRINT:
    case AC_EV_ACTIVE:
        return AC_MODE_ACTIVE;
    case AC_EV_POST_PRINT:
        return AC_MODE_POST_PRINT;
    default:
        return cfg->default_mode;
    }
}
