#include "ac_core/ac_engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SEC(x) ((ac_time_ms_t)(x) * 1000u)
/* The SPS30's auto-cleaning counter resets whenever the sensor is switched
 * off, and we switch it off after every measurement.  The datasheet is
 * explicit: "make sure to trigger a cleaning cycle at least every week if the
 * sensor is switched off and on periodically". */
#define FAN_CLEAN_PERIOD_MS  SEC(7u * 24u * 3600u)

static void set_state(ac_engine_t *e, ac_device_state_t s, ac_time_ms_t now)
{
    if (e->state == s) return;
    e->state = s;
    e->state_since = now;
}

void ac_engine_init(ac_engine_t *e, const ac_config_t *cfg, ac_time_ms_t now)
{
    memset(e, 0, sizeof(*e));
    if (cfg) e->cfg = *cfg; else ac_config_defaults(&e->cfg);
    ac_config_validate(&e->cfg);

    ac_baseline_init(&e->base, e->cfg.baseline_update_s, e->cfg.baseline_alpha);
    ac_event_init(&e->ev);
    ac_history_init(&e->hist);

    /* Time constants: short enough that a print is still visible, long enough
     * that a single noisy SPS30 window does not flip a state. */
    ac_ema_init(&e->f_pm25, 120.0f);
    ac_ema_init(&e->f_pm10, 120.0f);
    ac_ema_init(&e->f_voc,  60.0f);
    ac_ema_init(&e->f_co2,  300.0f);
    ac_rate_init(&e->r_pm25);
    ac_rate_init(&e->r_voc);
    ac_stats_init(&e->st_pm25);

    e->last.pm1 = e->last.pm25 = e->last.pm4 = e->last.pm10 = AC_INVALID_F;
    e->last.co2 = AC_INVALID_F;
    e->last.temperature = e->last.humidity = AC_INVALID_F;
    e->last.voc_index = -1;
    e->last.voc_raw = -1;
    e->pub_voc = -1;

    e->mode = e->cfg.default_mode;
    e->boot_ms = now;
    e->state = AC_STATE_BOOT;
    e->state_since = now;
    e->next_pm = now;          /* take one measurement of everything at once */
    e->next_voc = now;
    e->next_co2 = now;
    e->last_fan_clean = now;   /* a fresh boot counts as recently cleaned */

    e->health.sps30_ok = e->health.sgp40_ok = e->health.scd41_ok = true;
    e->health.battery_ok = true;
}

const ac_profile_t *ac_engine_profile(const ac_engine_t *e)
{
    ac_mode_t m = e->mode < AC_MODE_COUNT ? e->mode : AC_MODE_ECO;
    return &e->cfg.profile[m];
}

void ac_engine_set_mode(ac_engine_t *e, ac_mode_t m, ac_time_ms_t now)
{
    if (m >= AC_MODE_COUNT || m == e->mode) return;
    e->mode = m;
    const ac_profile_t *p = ac_engine_profile(e);
    /* Re-plan immediately: switching into ACTIVE must not wait out the ECO
     * interval that is already running. */
    if (p->pm_interval_s == 0) e->next_pm = now;
    else if (e->next_pm > now + SEC(p->pm_interval_s))
        e->next_pm = now;
    if (p->voc_interval_s && e->next_voc > now + SEC(p->voc_interval_s))
        e->next_voc = now;
    if (p->co2_interval_s && e->next_co2 > now + SEC(p->co2_interval_s))
        e->next_co2 = now;
}

void ac_engine_submit(ac_engine_t *e, const ac_sample_t *s)
{
    ac_sample_t *d = &e->last;
    d->t = s->t;
    d->pm_fresh = d->voc_fresh = d->co2_fresh = d->th_fresh = false;

    if (s->pm_fresh) {
        d->pm1 = s->pm1; d->pm25 = s->pm25; d->pm4 = s->pm4; d->pm10 = s->pm10;
        d->pn05 = s->pn05; d->pn10 = s->pn10; d->typical_size = s->typical_size;
        d->pm_fresh = true;
        ac_ema_update(&e->f_pm25, s->pm25, s->t);
        ac_ema_update(&e->f_pm10, s->pm10, s->t);
        ac_rate_push(&e->r_pm25, s->pm25, s->t);
        ac_stats_push(&e->st_pm25, s->pm25);
    }
    if (s->voc_fresh) {
        d->voc_index = s->voc_index;
        d->voc_raw = s->voc_raw;
        d->voc_fresh = true;
        e->voc_samples++;
        if (s->voc_index >= 0) {
            ac_ema_update(&e->f_voc, (float)s->voc_index, s->t);
            ac_rate_push(&e->r_voc, (float)s->voc_index, s->t);
        }
    }
    if (s->co2_fresh) {
        d->co2 = s->co2;
        d->co2_fresh = true;
        ac_ema_update(&e->f_co2, s->co2, s->t);
    }
    if (s->th_fresh) {
        d->temperature = s->temperature;
        d->humidity = s->humidity;
        d->th_fresh = true;
    }
}

void ac_engine_set_power(ac_engine_t *e, bool usb, bool charging,
                         float pct, float v, ac_time_ms_t now)
{
    e->usb_present = usb;
    e->charging = charging;
    e->last.battery_pct = pct;
    e->last.battery_v = v;
    e->last.charging = charging;

    if (pct >= 0.0f) {
        if (pct <= e->cfg.critical_battery_pct && !usb)
            set_state(e, AC_STATE_CRITICAL_BATTERY, now);
        else if (pct <= e->cfg.low_battery_pct && !usb &&
                 e->state != AC_STATE_CRITICAL_BATTERY)
            set_state(e, AC_STATE_LOW_BATTERY, now);
        else if ((e->state == AC_STATE_LOW_BATTERY ||
                  e->state == AC_STATE_CRITICAL_BATTERY) &&
                 (usb || pct > e->cfg.low_battery_pct + 5.0f))
            set_state(e, AC_STATE_NORMAL, now);
    }
}

void ac_engine_set_state(ac_engine_t *e, ac_device_state_t s, ac_time_ms_t now)
{
    set_state(e, s, now);
}

void ac_engine_sensor_failed(ac_engine_t *e, ac_action_t which, const char *why)
{
    switch (which) {
    case AC_ACT_SAMPLE_PM:
        e->health.sps30_errors++;
        if (e->health.sps30_errors >= 3) e->health.sps30_ok = false;
        break;
    case AC_ACT_SAMPLE_VOC:
        e->health.sgp40_errors++;
        if (e->health.sgp40_errors >= 5) e->health.sgp40_ok = false;
        break;
    case AC_ACT_SAMPLE_CO2:
        e->health.scd41_errors++;
        if (e->health.scd41_errors >= 3) e->health.scd41_ok = false;
        break;
    default:
        break;
    }
    if (why) {
        strncpy(e->health.last_error, why, sizeof(e->health.last_error) - 1);
        e->health.last_error[sizeof(e->health.last_error) - 1] = '\0';
    }
}

void ac_engine_baseline_reset(ac_engine_t *e)
{
    ac_baseline_reset(&e->base, &e->last);
}

void ac_engine_factory_reset(ac_engine_t *e, ac_time_ms_t now)
{
    ac_config_t def;
    ac_config_defaults(&def);
    ac_engine_init(e, &def, now);
    set_state(e, AC_STATE_FACTORY_RESET, now);
}

bool ac_engine_values_valid(const ac_engine_t *e) { return e->warm; }

uint32_t ac_engine_uptime_s(const ac_engine_t *e, ac_time_ms_t now)
{
    return (uint32_t)((now - e->boot_ms) / 1000u);
}

static bool publish_changed(ac_engine_t *e)
{
    bool ch = false;
    /* Report thresholds: enough to be useful in Apple Home, coarse enough not
     * to wake the radio on sensor noise.  Matter subscriptions are rate
     * limited by the controller anyway, but every report costs a Thread
     * transmission and this device is on a battery. */
    if (e->last.pm25 >= 0.0f && fabsf(e->last.pm25 - e->pub_pm25) >= 0.5f) {
        e->pub_pm25 = e->last.pm25; ch = true;
    }
    if (e->last.pm10 >= 0.0f && fabsf(e->last.pm10 - e->pub_pm10) >= 1.0f) {
        e->pub_pm10 = e->last.pm10; ch = true;
    }
    if (e->last.pm1 >= 0.0f && fabsf(e->last.pm1 - e->pub_pm1) >= 0.5f) {
        e->pub_pm1 = e->last.pm1; ch = true;
    }
    if (e->last.co2 >= 0.0f && fabsf(e->last.co2 - e->pub_co2) >= 15.0f) {
        e->pub_co2 = e->last.co2; ch = true;
    }
    if (e->last.voc_index >= 0 && labs((long)e->last.voc_index - e->pub_voc) >= 5) {
        e->pub_voc = e->last.voc_index; ch = true;
    }
    if (e->last.temperature > -50.0f &&
        fabsf(e->last.temperature - e->pub_t) >= 0.2f) {
        e->pub_t = e->last.temperature; ch = true;
    }
    if (e->last.humidity >= 0.0f && fabsf(e->last.humidity - e->pub_rh) >= 1.0f) {
        e->pub_rh = e->last.humidity; ch = true;
    }
    if (e->last.battery_pct >= 0.0f &&
        fabsf(e->last.battery_pct - e->pub_batt) >= 1.0f) {
        e->pub_batt = e->last.battery_pct; ch = true;
    }
    uint8_t aq = ac_airquality_to_matter(e->aq.level);
    if (aq != e->pub_aq) { e->pub_aq = aq; ch = true; }
    return ch;
}

static ac_time_ms_t soonest(ac_time_ms_t a, ac_time_ms_t b)
{
    return a < b ? a : b;
}

ac_plan_t ac_engine_tick(ac_engine_t *e, ac_time_ms_t now)
{
    ac_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    const ac_profile_t *p = ac_engine_profile(e);

    /* ---- warm-up ---------------------------------------------------------
     * Do not publish anything until the gas sensor has had its documented
     * 60 s and we have at least one PM window behind us.  Until then the
     * status LED pulses white and Matter reports the attributes as
     * unavailable rather than as plausible-looking nonsense. */
    if (!e->warm) {
        bool voc_ready = (!e->health.sgp40_ok) || (e->cfg.profile[e->mode].voc_interval_s == 0) ||
                         (now - e->boot_ms >= SEC(AC_SGP40_USABLE_S));
        bool pm_ready = (!e->health.sps30_ok) || (e->last.pm25 >= 0.0f);
        if (voc_ready && pm_ready) {
            e->warm = true;
            plan.status_dirty = true;
        } else if (e->state == AC_STATE_BOOT) {
            set_state(e, AC_STATE_WARMUP, now);
            plan.status_dirty = true;
        }
    }

    /* ---- classification --------------------------------------------------*/
    float pm_rate = ac_rate_per_min(&e->r_pm25);
    float voc_rate = ac_rate_per_min(&e->r_voc);
    ac_aq_result_t aq = ac_airquality_eval(&e->cfg, &e->base, &e->last,
                                           pm_rate, voc_rate);
    if (aq.level != e->aq.level || aq.reason != e->aq.reason)
        plan.status_dirty = true;
    e->aq = aq;

    /* ---- event detection -------------------------------------------------*/
    ac_event_record_t done;
    bool changed = ac_event_update(&e->ev, &e->cfg, &e->base, &e->last,
                                   pm_rate, voc_rate, &done);
    ac_baseline_freeze(&e->base, ac_event_should_freeze_baseline(&e->ev));
    if (changed) plan.status_dirty = true;

    /* ---- baseline and history -------------------------------------------*/
    if (e->warm) {
        ac_baseline_update(&e->base, &e->last);
        ac_history_push(&e->hist, &e->last);
    }

    /* ---- mode escalation -------------------------------------------------*/
    if (e->state != AC_STATE_CRITICAL_BATTERY) {
        ac_mode_t want = ac_event_preferred_mode(&e->ev, &e->cfg);
        if (e->usb_present) want = AC_MODE_CONTINUOUS;
        if (want != e->mode) {
            ac_engine_set_mode(e, want, now);
            p = ac_engine_profile(e);
            plan.status_dirty = true;
        }
    }

    /* ---- device state ----------------------------------------------------*/
    if (e->state != AC_STATE_LOW_BATTERY &&
        e->state != AC_STATE_CRITICAL_BATTERY &&
        e->state != AC_STATE_COMMISSIONING &&
        e->state != AC_STATE_FACTORY_RESET) {
        if (!e->health.sps30_ok && !e->health.sgp40_ok && !e->health.scd41_ok)
            set_state(e, AC_STATE_ERROR, now);
        else if (e->usb_present)
            set_state(e, AC_STATE_CHARGING, now);
        else if (!e->warm)
            set_state(e, AC_STATE_WARMUP, now);
        else switch (e->ev.state) {
            case AC_EV_ACTIVE:
            case AC_EV_POSSIBLE_PRINT: set_state(e, AC_STATE_ACTIVE, now); break;
            case AC_EV_POST_PRINT:     set_state(e, AC_STATE_POST_PRINT, now); break;
            default:                   set_state(e, AC_STATE_NORMAL, now); break;
        }
    }

    /* ---- scheduling ------------------------------------------------------*/
    if (e->state == AC_STATE_CRITICAL_BATTERY) {
        /* Everything except the clock is off.  We keep reporting battery over
         * Matter so the notification automation still fires. */
        plan.action = AC_ACT_NONE;
        plan.sleep_ms = SEC(300);
        plan.publish_dirty = publish_changed(e);
        return plan;
    }

    if (e->health.sps30_ok && now - e->last_fan_clean >= FAN_CLEAN_PERIOD_MS) {
        plan.action = AC_ACT_FAN_CLEAN;
        e->last_fan_clean = now;
        plan.sleep_ms = 0;
        plan.publish_dirty = publish_changed(e);
        return plan;
    }

    if (e->health.sps30_ok && p->pm_interval_s == 0) {
        /* "Continuous" means back-to-back measurement windows, not a one
         * second window: the SPS30 needs 30 s in measurement mode before its
         * output is usable, so a short window would return numbers the
         * datasheet says not to trust. */
        plan.action = AC_ACT_SAMPLE_PM;
        plan.pm_window_s = p->pm_window_s >= AC_SPS30_REC_WINDOW_S
                           ? p->pm_window_s : 60;
        plan.sleep_ms = 0;
        plan.publish_dirty = publish_changed(e);
        return plan;
    }

    if (e->health.sps30_ok && now >= e->next_pm) {
        plan.action = AC_ACT_SAMPLE_PM;
        plan.pm_window_s = p->pm_window_s < AC_SPS30_MIN_WINDOW_S
                           ? AC_SPS30_MIN_WINDOW_S : p->pm_window_s;
        e->next_pm = now + SEC(p->pm_interval_s);
        plan.sleep_ms = 0;
        plan.publish_dirty = publish_changed(e);
        return plan;
    }
    if (e->health.scd41_ok && p->co2_interval_s && now >= e->next_co2) {
        plan.action = AC_ACT_SAMPLE_CO2;
        e->next_co2 = now + SEC(p->co2_interval_s);
        plan.sleep_ms = 0;
        plan.publish_dirty = publish_changed(e);
        return plan;
    }
    if (e->health.sgp40_ok && p->voc_interval_s && now >= e->next_voc) {
        plan.action = AC_ACT_SAMPLE_VOC;
        e->next_voc = now + SEC(p->voc_interval_s);
        plan.sleep_ms = 0;
        plan.publish_dirty = publish_changed(e);
        return plan;
    }

    /* Nothing due: work out how long we may sleep. */
    ac_time_ms_t next = now + SEC(3600);
    if (e->health.sps30_ok && p->pm_interval_s) next = soonest(next, e->next_pm);
    if (e->health.sgp40_ok && p->voc_interval_s) next = soonest(next, e->next_voc);
    if (e->health.scd41_ok && p->co2_interval_s) next = soonest(next, e->next_co2);
    plan.action = AC_ACT_NONE;
    plan.sleep_ms = (next > now) ? (uint32_t)(next - now) : 0u;
    plan.publish_dirty = publish_changed(e);
    return plan;
}
