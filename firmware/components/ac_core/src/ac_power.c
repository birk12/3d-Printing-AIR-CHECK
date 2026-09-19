#include "ac_core/ac_power.h"

void ac_power_init(ac_power_t *p)
{
    pwr_init(&p->pwr);
    p->chg_since_s = 0;
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
    if (now_s == 0) now_s = 1;                   /* 0 means "not charging" */

    if (!r.ext) {
        p->chg_since_s = 0;
    } else if (o.charging) {
        if (p->chg_since_s == 0) p->chg_since_s = now_s;
    } else if (o.st.fault == PWR_FLT_RECOVERABLE && p->chg_since_s != 0 &&
               now_s - p->chg_since_s >= AC_TIMER_SUSPECT_S) {
        o.st.fault = PWR_FLT_LATCHED;            /* the 6 h safety timer */
    } else if (o.st.fault == PWR_FLT_NONE) {
        p->chg_since_s = 0;                      /* charge ended normally */
    }

    if (pwr_timer_retry(&p->pwr, &o.st, vbat)) {
        o.ce = AC_CE_PULSE;
        p->chg_since_s = now_s;                  /* a new 6 h window */
    } else if (pwr_hold_update(&p->pwr, &o.st, vbat, now_s)) {
        o.ce = AC_CE_HOLD;
    }
    return o;
}
