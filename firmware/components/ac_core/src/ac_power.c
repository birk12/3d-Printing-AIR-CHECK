#include "ac_core/ac_power.h"

void ac_power_init(ac_power_t *p)
{
    pwr_init(&p->pwr);
    p->chg_accum_s = 0;
    p->last_s = 0;
    p->was_charging = false;
}

bool ac_power_boot_should_sleep(float vbat, const float ladder_v[2])
{
    bool ext = pwr_k_decode(ladder_v[0]).ext && pwr_k_decode(ladder_v[1]).ext;
    return !ext && vbat >= PWR_V_ABSENT && vbat < PWR_V_CRIT + PWR_V_HYST;
}

ac_power_out_t ac_power_update(ac_power_t *p, float vbat, const float ladder_v[2],
                               bool usb_host, uint32_t now_s)
{
    pwr_k_t k0 = pwr_k_decode(ladder_v[0]);
    pwr_k_t k1 = pwr_k_decode(ladder_v[1]);
    pwr_raw_t r = {
        .vbat = vbat,
        .ext = k0.ext && k1.ext,
        .chg_low = { k0.chg_low, k1.chg_low },
        .flt_low = { k0.flt_low, k1.flt_low },
    };
    ac_power_out_t o;
    o.st = pwr_update(&p->pwr, &r);
    o.ext = r.ext || usb_host;
    o.charging = o.st.chg == PWR_CHG_CHARGING;
    o.ce = AC_CE_RELEASE;
    /* Charging time in this USB session: only the intervals that began with
     * CHG_N "charging" count, so a pause (NTC, charge hold) does not. */
    if (!r.ext) {
        p->chg_accum_s = 0;                      /* the session ended */
    } else if (p->was_charging && p->last_s != 0 && now_s > p->last_s) {
        p->chg_accum_s += now_s - p->last_s;
    }
    p->was_charging = r.ext && o.charging;
    p->last_s = now_s;

    if (r.ext && !o.charging && o.st.fault == PWR_FLT_RECOVERABLE &&
        p->chg_accum_s >= AC_TIMER_SUSPECT_S)
        o.st.fault = PWR_FLT_LATCHED;            /* taken for the 6 h safety timer */
    else if (r.ext && o.st.fault == PWR_FLT_NONE && !o.charging)
        p->chg_accum_s = 0;                      /* charge ended normally */

    if (pwr_timer_retry(&p->pwr, &o.st, vbat)) {
        o.ce = AC_CE_PULSE;
        p->chg_accum_s = 0;                      /* a new 6 h window */
        /* The first, expected timer run is not a fault: nothing is broken.
         * Only a timer run after the retry is published as LATCHED. */
        o.st.fault = PWR_FLT_NONE;
    } else if (pwr_hold_update(&p->pwr, &o.st, vbat, now_s)) {
        o.ce = AC_CE_HOLD;
    }
    return o;
}
