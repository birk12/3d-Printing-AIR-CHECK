#include "pwr_std.h"

/* Resting LFP voltage -> state of charge, read off TI SLUAAR1 Fig. 2-3 and
 * rounded down.  Coarse on purpose. */
static const struct { float v; uint8_t pct; } LFP_OCV[] = {
    { 3.00f,   0 },
    { 3.10f,   6 },
    { 3.20f,  10 },
    { 3.25f,  25 },
    { 3.30f,  55 },
    { 3.33f,  75 },
    { 3.40f, 100 },
};
#define LFP_OCV_N (sizeof LFP_OCV / sizeof LFP_OCV[0])

uint8_t pwr_lfp_pct(float v)
{
    if (v <= LFP_OCV[0].v) return 0;
    if (v >= LFP_OCV[LFP_OCV_N - 1].v) return 100;
    for (unsigned i = 1; i < LFP_OCV_N; i++) {
        if (v < LFP_OCV[i].v) {
            float f = (v - LFP_OCV[i - 1].v) / (LFP_OCV[i].v - LFP_OCV[i - 1].v);
            return (uint8_t)(LFP_OCV[i - 1].pct + f * (LFP_OCV[i].pct - LFP_OCV[i - 1].pct));
        }
    }
    return 100;
}

void pwr_init(pwr_ctx_t *c)
{
    c->lvl = PWR_LVL_OK;
    c->last_vbat = -1.0f;
    c->was_charging = false;
    c->hold = false;
    c->hold_since_s = 0;
    c->retried = false;
}

static pwr_lvl_t level(pwr_lvl_t prev, float v)
{
    /* Going down at the threshold, back up only past threshold + HYST. */
    switch (prev) {
    case PWR_LVL_CRITICAL:
        if (v >= PWR_V_WARN + PWR_V_HYST) return PWR_LVL_OK;
        if (v >= PWR_V_CRIT + PWR_V_HYST) return PWR_LVL_WARNING;
        return PWR_LVL_CRITICAL;
    case PWR_LVL_WARNING:
        if (v < PWR_V_CRIT) return PWR_LVL_CRITICAL;
        if (v >= PWR_V_WARN + PWR_V_HYST) return PWR_LVL_OK;
        return PWR_LVL_WARNING;
    default:
        if (v < PWR_V_CRIT) return PWR_LVL_CRITICAL;
        if (v < PWR_V_WARN) return PWR_LVL_WARNING;
        return PWR_LVL_OK;
    }
}

pwr_state_t pwr_update(pwr_ctx_t *c, const pwr_raw_t *r)
{
    pwr_state_t s = { PWR_SRC_BATTERY, PWR_CHG_UNKNOWN, PWR_LVL_OK, PWR_FLT_NONE, 0, false };
    bool cell = r->vbat >= PWR_V_ABSENT;

    /* STAT1/STAT2 (BQ25185 Table 6-2).  A toggling STAT2 means "no cell,
     * charger probing"; treat it as not charging. */
    bool flt = r->flt_low[0] && r->flt_low[1];
    bool chg = r->chg_low[0] && r->chg_low[1];
    bool chg_stable = r->chg_low[0] == r->chg_low[1];
    if (flt)
        s.fault = chg ? PWR_FLT_LATCHED : PWR_FLT_RECOVERABLE;

    if (r->ext)
        s.src = cell ? PWR_SRC_EXTERNAL : PWR_SRC_EXTERNAL_NO_CELL;

    if (!cell) {
        s.chg = PWR_CHG_UNKNOWN;
        s.lvl = PWR_LVL_OK;           /* no battery to report on          */
        c->last_vbat = -1.0f;
        c->was_charging = false;
        return s;
    }

    if (!r->ext) {
        s.chg = PWR_CHG_NOT_CHARGING;
        c->was_charging = false;
        c->retried = false;
    } else if (s.fault != PWR_FLT_NONE || !chg_stable) {
        s.chg = PWR_CHG_NOT_CHARGING;
    } else if (chg) {
        s.chg = PWR_CHG_CHARGING;
        c->was_charging = true;
    } else if (c->was_charging || c->hold || r->vbat >= PWR_V_FULL) {
        s.chg = PWR_CHG_FULL;         /* charge ended, or paused when full */
    } else {
        s.chg = PWR_CHG_NOT_CHARGING;
    }

    /* Externally recharged cells: a jump from "low" to "fresh". */
    if (c->last_vbat >= 0.0f && c->last_vbat < PWR_V_WARN + PWR_V_HYST &&
        r->vbat >= PWR_V_NEWPACK)
        s.new_pack = true;
    c->last_vbat = r->vbat;

    c->lvl = level(s.new_pack ? PWR_LVL_OK : c->lvl, r->vbat);
    s.lvl = c->lvl;
    s.pct = (s.chg == PWR_CHG_FULL) ? 100 : pwr_lfp_pct(r->vbat);
    return s;
}

bool pwr_hold_update(pwr_ctx_t *c, const pwr_state_t *s, float vbat, uint32_t now_s)
{
    if (s->src != PWR_SRC_EXTERNAL || s->fault != PWR_FLT_NONE) {
        c->hold = false;              /* USB gone or fault: never hold    */
        return false;
    }
    if (c->hold) {
        if (vbat < PWR_V_RECHARGE || (uint32_t)(now_s - c->hold_since_s) >= PWR_HOLD_MAX_S) {
            c->hold = false;
            c->was_charging = false;  /* next "full" needs a real charge  */
        }
        return c->hold;
    }
    if (s->chg == PWR_CHG_FULL && c->was_charging) {
        c->hold = true;
        c->hold_since_s = now_s;
    }
    return c->hold;
}

pwr_k_t pwr_k_decode(float v)
{
    pwr_k_t k = { false, false, false };
    if (v < PWR_K_USB_MIN) return k;
    k.ext = true;
    if (v < PWR_K_CHG_MIN)       k.flt_low = true;
    else if (v < PWR_K_IDLE_MIN) k.chg_low = true;
    return k;
}

bool pwr_timer_retry(pwr_ctx_t *c, const pwr_state_t *s, float vbat)
{
    if (s->src != PWR_SRC_EXTERNAL || s->fault != PWR_FLT_LATCHED ||
        vbat >= PWR_V_FULL || c->retried)
        return false;
    c->retried = true;
    return true;
}
