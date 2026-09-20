/* Host test: cc -std=c99 -Wall -Wextra -o t test_pwr_std.c pwr_std.c && ./t */
#include <stdio.h>
#include "pwr_std.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static pwr_raw_t raw(float v, bool ext, bool chg, bool flt)
{
    pwr_raw_t r = { v, ext, { chg, chg }, { flt, flt } };
    return r;
}

int main(void)
{
    pwr_ctx_t c;
    pwr_state_t s;
    pwr_raw_t r;

    /* OCV table */
    CHECK(pwr_lfp_pct(2.9f) == 0);
    CHECK(pwr_lfp_pct(3.20f) == 10);
    CHECK(pwr_lfp_pct(3.5f) == 100);
    CHECK(pwr_matter_pct(50) == 100);

    /* Module B: USB, no cell, STAT2 toggling */
    pwr_init(&c);
    r = raw(0.0f, true, false, false); r.chg_low[1] = true;
    s = pwr_update(&c, &r);
    CHECK(s.src == PWR_SRC_EXTERNAL_NO_CELL);
    CHECK(s.chg == PWR_CHG_UNKNOWN);

    /* Module A on battery: levels with hysteresis */
    pwr_init(&c);
    r = raw(3.30f, false, false, false); s = pwr_update(&c, &r);
    CHECK(s.src == PWR_SRC_BATTERY && s.chg == PWR_CHG_NOT_CHARGING && s.lvl == PWR_LVL_OK);
    r = raw(3.19f, false, false, false); s = pwr_update(&c, &r);
    CHECK(s.lvl == PWR_LVL_WARNING);
    r = raw(3.22f, false, false, false); s = pwr_update(&c, &r);
    CHECK(s.lvl == PWR_LVL_WARNING);          /* hysteresis holds */
    r = raw(3.09f, false, false, false); s = pwr_update(&c, &r);
    CHECK(s.lvl == PWR_LVL_CRITICAL);
    /* fresh cells from the MX4 */
    r = raw(3.45f, false, false, false); s = pwr_update(&c, &r);
    CHECK(s.new_pack && s.lvl == PWR_LVL_OK);

    /* Module C: charge, full, hold, release */
    pwr_init(&c);
    r = raw(3.25f, true, true, false); s = pwr_update(&c, &r);
    CHECK(s.src == PWR_SRC_EXTERNAL && s.chg == PWR_CHG_CHARGING);
    CHECK(!pwr_hold_update(&c, &s, r.vbat, 100));
    r = raw(3.45f, true, false, false); s = pwr_update(&c, &r);
    CHECK(s.chg == PWR_CHG_FULL && s.pct == 100);
    CHECK(pwr_hold_update(&c, &s, r.vbat, 200));           /* pause */
    r = raw(3.36f, true, false, false); s = pwr_update(&c, &r);
    CHECK(s.chg == PWR_CHG_FULL);                           /* paused, still full */
    CHECK(pwr_hold_update(&c, &s, r.vbat, 300));
    r = raw(3.29f, true, false, false); s = pwr_update(&c, &r);
    CHECK(!pwr_hold_update(&c, &s, r.vbat, 400));          /* below 3.30 V: release */
    /* hold expires after 30 days */
    r = raw(3.25f, true, true, false); s = pwr_update(&c, &r);
    r = raw(3.45f, true, false, false); s = pwr_update(&c, &r);
    CHECK(pwr_hold_update(&c, &s, r.vbat, 1000));
    CHECK(!pwr_hold_update(&c, &s, r.vbat, 1000 + PWR_HOLD_MAX_S));
    /* USB pulled: no hold */
    r = raw(3.40f, false, false, false); s = pwr_update(&c, &r);
    CHECK(!pwr_hold_update(&c, &s, r.vbat, 5000));

    /* Faults */
    pwr_init(&c);
    r = raw(3.30f, true, false, true); s = pwr_update(&c, &r);
    CHECK(s.fault == PWR_FLT_RECOVERABLE && s.chg == PWR_CHG_NOT_CHARGING);
    r = raw(3.30f, true, true, true); s = pwr_update(&c, &r);
    CHECK(s.fault == PWR_FLT_LATCHED);
    CHECK(!pwr_hold_update(&c, &s, r.vbat, 10));

    /* Safety-timer retry: once per USB session */
    pwr_init(&c);
    r = raw(3.30f, true, true, true); s = pwr_update(&c, &r);
    CHECK(pwr_timer_retry(&c, &s, r.vbat));
    CHECK(!pwr_timer_retry(&c, &s, r.vbat));               /* only once */
    r = raw(3.30f, false, false, false); s = pwr_update(&c, &r);  /* USB out */
    r = raw(3.30f, true, true, true); s = pwr_update(&c, &r);
    CHECK(pwr_timer_retry(&c, &s, r.vbat));                /* new session */
    pwr_init(&c);
    r = raw(3.45f, true, true, true); s = pwr_update(&c, &r);
    CHECK(!pwr_timer_retry(&c, &s, r.vbat));               /* already full */
    r = raw(3.30f, true, false, true); s = pwr_update(&c, &r);
    CHECK(!pwr_timer_retry(&c, &s, r.vbat));               /* recoverable: no */

    /* One-point calibration */
    {
        pwr_cal_status_t st;
        float k = pwr_cal_factor(3.28f, 3.30f, &st);
        CHECK(k > 1.005f && k < 1.007f && st == PWR_CAL_OK);
        CHECK(pwr_cal_factor(3.30f, 3.30f, &st) == 1.0f && st == PWR_CAL_OK);
        CHECK(pwr_cal_factor(3.00f, 3.60f, &st) == 1.0f && st == PWR_CAL_OUT_OF_RANGE);
        CHECK(pwr_cal_factor(3.60f, 3.00f, &st) == 1.0f && st == PWR_CAL_OUT_OF_RANGE);
        CHECK(pwr_cal_factor(0.0f, 3.30f, &st) == 1.0f && st == PWR_CAL_NO_CELL);
        CHECK(pwr_cal_factor(3.28f, 3.30f, NULL) > 1.0f);  /* NULL erlaubt */
    }
    CHECK(pwr_cal_apply(3.28f, 1.006f) > 3.29f);
    CHECK(pwr_cal_apply(3.28f, 1.9f) == 3.28f);           /* implausible: ignored */

    /* PWR-K ladder, band edges from the worst-case calculation */
    {
        pwr_k_t k;
        k = pwr_k_decode(0.0f);  CHECK(!k.ext);
        k = pwr_k_decode(0.99f); CHECK(k.ext && k.flt_low && !k.chg_low);
        k = pwr_k_decode(1.60f); CHECK(k.ext && k.flt_low);   /* worst case incl. VOL */
        k = pwr_k_decode(2.09f); CHECK(k.ext && k.chg_low && !k.flt_low);
        k = pwr_k_decode(2.46f); CHECK(k.chg_low);
        k = pwr_k_decode(2.85f); CHECK(k.ext && !k.chg_low && !k.flt_low);
        k = pwr_k_decode(3.15f); CHECK(k.ext && !k.chg_low);
    }

    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails != 0;
}
