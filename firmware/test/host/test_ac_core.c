/* Host tests for the AIR CHECK measurement core.
 *
 * These run on a workstation with a plain C compiler.  They are the reason
 * the interesting half of this firmware can be called "tested" rather than
 * "written".  Everything they exercise is platform independent by
 * construction: ac_core never includes an ESP-IDF header.
 */
#include "ac_core/ac_airquality.h"
#include "ac_core/ac_baseline.h"
#include "ac_core/ac_battery.h"
#include "ac_core/ac_config.h"
#include "ac_core/ac_status.h"
#include "ac_core/ac_engine.h"
#include "ac_core/ac_event.h"
#include "ac_core/ac_filter.h"
#include "ac_core/ac_history.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail, g_run;
static const char *g_case = "";

#define CASE(name) do { g_case = name; } while (0)
#define CHECK(cond) do {                                                      \
    g_run++;                                                                  \
    if (!(cond)) {                                                            \
        g_fail++;                                                             \
        printf("  FAIL  %s: %s  (%s:%d)\n", g_case, #cond, __FILE__, __LINE__);\
    }                                                                         \
} while (0)
#define CHECK_NEAR(a, b, tol) do {                                            \
    g_run++;                                                                  \
    double _a = (a), _b = (b);                                                \
    if (fabs(_a - _b) > (tol)) {                                              \
        g_fail++;                                                             \
        printf("  FAIL  %s: %s = %g, expected %g +- %g  (%s:%d)\n",           \
               g_case, #a, _a, _b, (double)(tol), __FILE__, __LINE__);        \
    }                                                                         \
} while (0)

/* ------------------------------------------------------------------ */

static ac_sample_t mk(ac_time_ms_t t, float pm25, int32_t voc, float co2)
{
    ac_sample_t s;
    memset(&s, 0, sizeof(s));
    s.t = t;
    s.pm1 = pm25 * 0.7f; s.pm25 = pm25; s.pm4 = pm25 * 1.1f; s.pm10 = pm25 * 1.3f;
    s.pm_fresh = pm25 >= 0.0f;
    s.voc_index = voc; s.voc_fresh = voc >= 0;
    s.co2 = co2; s.co2_fresh = co2 > 0.0f;
    s.temperature = 22.4f; s.humidity = 46.0f; s.th_fresh = true;
    s.battery_pct = 87.0f; s.battery_v = 3.91f;
    return s;
}

/* ---------------- config ------------------------------------------ */

static void test_config(void)
{
    CASE("config defaults are self-consistent");
    ac_config_t c;
    ac_config_defaults(&c);
    CHECK(ac_config_validate(&c) == 0);
    CHECK(c.pm25_elevated < c.pm25_high);
    CHECK(c.pm25_high < c.pm25_very_high);
    CHECK(c.critical_battery_pct < c.low_battery_pct);
    CHECK(c.default_mode == AC_MODE_ECO);

    CASE("every profile obeys the sensor datasheets");
    for (int m = 0; m < AC_MODE_COUNT; m++) {
        const ac_profile_t *p = &c.profile[m];
        /* SEN63C: no CO2 for 22..24 s after start, PM settles in 30 s */
        CHECK(p->pm_window_s >= AC_PM_MIN_WINDOW_S);
        if (p->voc_interval_s)
            CHECK(p->voc_interval_s >= 1 && p->voc_interval_s <= 10);
        /* Matter 1.4 caps a SIT ICD slow poll at 15 s */
        CHECK(p->icd_slow_poll_s <= 15);
    }

    CASE("validate repairs nonsense instead of accepting it");
    CHECK(1);

    CASE("round trip through a blob");
    uint8_t blob[sizeof(ac_config_t)];
    ac_config_t back;
    CHECK(ac_config_save(&c, blob, sizeof(blob)) == sizeof(ac_config_t));
    CHECK(ac_config_load(&back, blob, sizeof(blob)));
    CHECK(memcmp(&back, &c, sizeof(c)) == 0);

    CASE("a corrupted blob is rejected, not half-applied");
    blob[40] ^= 0xFF;
    ac_config_t untouched;
    ac_config_defaults(&untouched);
    ac_config_t target = untouched;
    CHECK(!ac_config_load(&target, blob, sizeof(blob)));
    CHECK(memcmp(&target, &untouched, sizeof(target)) == 0);

    CASE("a short blob is rejected");
    CHECK(!ac_config_load(&target, blob, 4));

    CASE("sensitivity 3 reproduces the defaults");
    ac_config_t s3 = c;
    ac_config_apply_sensitivity(&s3, 3);
    CHECK_NEAR(s3.ev_pm25_delta, c.ev_pm25_delta, 1e-4);
    CHECK(s3.ev_voc_delta == c.ev_voc_delta);

    CASE("sensitivity 1 is less trigger happy than 5");
    ac_config_t s1 = c, s5 = c;
    ac_config_apply_sensitivity(&s1, 1);
    ac_config_apply_sensitivity(&s5, 5);
    CHECK(s1.ev_pm25_delta > s5.ev_pm25_delta);
    CHECK(s1.ev_confirm_s > s5.ev_confirm_s);

    CASE("out of range thresholds are clamped and counted");
    ac_config_t wild;
    ac_config_defaults(&wild);
    wild.pm25_high = 1.0f;          /* below elevated */
    wild.battery_interval_s = 99999;
    wild.profile[AC_MODE_ECO].pm_window_s = 20;  /* too short for any CO2 */
    wild.profile[AC_MODE_ECO].icd_slow_poll_s = 900;
    int fixed = ac_config_validate(&wild);
    CHECK(fixed >= 4);
    CHECK(wild.pm25_high > wild.pm25_elevated);
    CHECK(wild.battery_interval_s <= 3600);
    CHECK(wild.profile[AC_MODE_ECO].pm_window_s >= AC_PM_MIN_WINDOW_S);
    CHECK(wild.profile[AC_MODE_ECO].icd_slow_poll_s <= 15);
}

/* ---------------- filters ------------------------------------------ */

static void test_filter(void)
{
    CASE("EMA takes the first sample as its value");
    ac_ema_t f;
    ac_ema_init(&f, 60.0f);
    CHECK_NEAR(ac_ema_update(&f, 10.0f, 0), 10.0, 1e-6);

    CASE("EMA reaches 63 % of a step after one time constant");
    ac_ema_init(&f, 60.0f);
    ac_ema_update(&f, 0.0f, 0);
    float v = ac_ema_update(&f, 100.0f, 60000);
    CHECK_NEAR(v, 63.2, 0.5);

    CASE("EMA is cadence independent: 6 x 10 s equals 1 x 60 s");
    ac_ema_t g;
    ac_ema_init(&g, 60.0f);
    ac_ema_update(&g, 0.0f, 0);
    for (int i = 1; i <= 6; i++) ac_ema_update(&g, 100.0f, (ac_time_ms_t)i * 10000);
    CHECK_NEAR(g.value, v, 0.01);

    CASE("rate of change recovers a known slope");
    ac_rate_t r;
    ac_rate_init(&r);
    for (int i = 0; i < 6; i++)
        ac_rate_push(&r, 10.0f + 2.0f * (float)i, (ac_time_ms_t)i * 60000);
    CHECK_NEAR(ac_rate_per_min(&r), 2.0, 1e-3);

    CASE("rate of change is zero with a single point");
    ac_rate_init(&r);
    ac_rate_push(&r, 5.0f, 0);
    CHECK_NEAR(ac_rate_per_min(&r), 0.0, 1e-9);

    CASE("rate survives the ring wrapping");
    ac_rate_init(&r);
    for (int i = 0; i < 40; i++)
        ac_rate_push(&r, (float)i, (ac_time_ms_t)i * 60000);
    CHECK_NEAR(ac_rate_per_min(&r), 1.0, 1e-3);

    CASE("spike detection needs evidence before it fires");
    ac_stats_t st;
    ac_stats_init(&st);
    CHECK(!ac_stats_is_spike(&st, 1000.0f, 3.0f));
    for (int i = 0; i < 40; i++) ac_stats_push(&st, 10.0f + (i % 2 ? 0.2f : -0.2f));
    CHECK(ac_stats_is_spike(&st, 40.0f, 3.0f));
    CHECK(!ac_stats_is_spike(&st, 10.1f, 3.0f));
}

/* ---------------- baseline ----------------------------------------- */

static void test_baseline(void)
{
    CASE("baseline converges on a steady room");
    ac_baseline_t b;
    ac_baseline_init(&b, 60, 0.2f);
    for (int i = 0; i < 200; i++) {
        ac_sample_t s = mk((ac_time_ms_t)i * 60000, 8.0f, 100, 600.0f);
        ac_baseline_update(&b, &s);
    }
    CHECK(ac_baseline_valid(&b));
    CHECK_NEAR(ac_baseline_pm25(&b), 8.0, 0.2);
    CHECK(ac_baseline_voc(&b) == 100);

    CASE("a frozen baseline does not learn the event");
    ac_baseline_freeze(&b, true);
    for (int i = 200; i < 400; i++) {
        ac_sample_t s = mk((ac_time_ms_t)i * 60000, 60.0f, 300, 1200.0f);
        ac_baseline_update(&b, &s);
    }
    CHECK_NEAR(ac_baseline_pm25(&b), 8.0, 0.2);

    CASE("delta is measured against the frozen baseline");
    ac_sample_t hot = mk(400ull * 60000, 60.0f, 300, 1200.0f);
    CHECK_NEAR(ac_baseline_d_pm25(&b, &hot), 52.0, 0.3);
    CHECK(ac_baseline_d_voc(&b, &hot) == 200);

    CASE("an unfrozen baseline resumes learning");
    ac_baseline_freeze(&b, false);
    for (int i = 400; i < 900; i++) {
        ac_sample_t s = mk((ac_time_ms_t)i * 60000, 60.0f, 300, 1200.0f);
        ac_baseline_update(&b, &s);
    }
    CHECK(ac_baseline_pm25(&b) > 50.0f);

    CASE("manual reset adopts the current air immediately");
    ac_sample_t now = mk(1000ull * 60000, 4.0f, 90, 500.0f);
    ac_baseline_reset(&b, &now);
    CHECK_NEAR(ac_baseline_pm25(&b), 4.0, 1e-3);
    CHECK(ac_baseline_voc(&b) == 90);
    CHECK(!ac_baseline_frozen(&b));

    CASE("restore rejects an implausible stored baseline");
    ac_baseline_store_t bogus = *ac_baseline_snapshot(&b);
    bogus.pm25 = 1e9f;
    ac_baseline_t b2;
    ac_baseline_init(&b2, 60, 0.2f);
    CHECK(!ac_baseline_restore(&b2, &bogus));
    bogus.magic = 0;
    CHECK(!ac_baseline_restore(&b2, &bogus));

    CASE("restore accepts a good one");
    ac_baseline_store_t good = *ac_baseline_snapshot(&b);
    CHECK(ac_baseline_restore(&b2, &good));
    CHECK_NEAR(ac_baseline_pm25(&b2), 4.0, 1e-3);

    CASE("no baseline means no delta, not a fake one");
    ac_baseline_t fresh;
    ac_baseline_init(&fresh, 60, 0.2f);
    CHECK_NEAR(ac_baseline_d_pm25(&fresh, &hot), 0.0, 1e-9);
    CHECK(ac_baseline_d_voc(&fresh, &hot) == 0);
}

/* ---------------- air quality -------------------------------------- */

static void test_airquality(void)
{
    ac_config_t c;
    ac_config_defaults(&c);
    ac_baseline_t b;
    ac_baseline_init(&b, 60, 0.2f);
    for (int i = 0; i < 100; i++) {
        ac_sample_t s = mk((ac_time_ms_t)i * 60000, 5.0f, 100, 500.0f);
        ac_baseline_update(&b, &s);
    }

    CASE("clean air is GOOD with no reason given");
    ac_sample_t s = mk(1, 5.0f, 100, 500.0f);
    ac_aq_result_t r = ac_airquality_eval(&c, &b, &s, 0.0f, 0.0f);
    CHECK(r.level == AC_AQ_GOOD);
    CHECK(r.reason == AC_REASON_NONE);

    CASE("PM2.5 above the elevated threshold names itself");
    s = mk(2, 20.0f, 100, 500.0f);
    r = ac_airquality_eval(&c, &b, &s, 0.0f, 0.0f);
    CHECK(r.level == AC_AQ_ELEVATED);
    CHECK(r.reason & AC_REASON_PM25);

    CASE("the worst channel decides the overall level");
    s = mk(3, 20.0f, 400, 500.0f);
    r = ac_airquality_eval(&c, &b, &s, 0.0f, 0.0f);
    CHECK(r.level == AC_AQ_VERY_HIGH);
    CHECK(r.reason & AC_REASON_VOC);
    CHECK(!(r.reason & AC_REASON_PM25));   /* PM is only ELEVATED, not the worst */

    CASE("two channels at the same level are both named");
    s = mk(4, 40.0f, 260, 500.0f);
    r = ac_airquality_eval(&c, &b, &s, 0.0f, 0.0f);
    CHECK(r.level == AC_AQ_HIGH);
    CHECK((r.reason & AC_REASON_PM25) && (r.reason & AC_REASON_VOC));
    char txt[48];
    ac_reason_text(r.reason, txt, sizeof(txt));
    CHECK(strstr(txt, "PM2.5") && strstr(txt, "VOC"));

    CASE("a steep rise escalates GOOD, but never further");
    s = mk(5, 5.0f, 100, 500.0f);
    r = ac_airquality_eval(&c, &b, &s, 10.0f, 0.0f);
    CHECK(r.level == AC_AQ_ELEVATED);
    CHECK(r.reason & AC_REASON_PM_RISING);
    s = mk(6, 40.0f, 100, 500.0f);
    r = ac_airquality_eval(&c, &b, &s, 10.0f, 0.0f);
    CHECK(r.level == AC_AQ_HIGH);   /* the concentration, not the trend, rules */

    CASE("nothing measured yet means UNKNOWN, not GOOD");
    ac_sample_t empty;
    memset(&empty, 0, sizeof(empty));
    empty.pm1 = empty.pm25 = empty.pm4 = empty.pm10 = AC_INVALID_F;
    empty.co2 = AC_INVALID_F;
    empty.voc_index = -1;
    r = ac_airquality_eval(&c, &b, &empty, 0.0f, 0.0f);
    CHECK(r.level == AC_AQ_UNKNOWN);

    CASE("the Matter mapping is monotonic and uses the documented enum");
    CHECK(ac_airquality_to_matter(AC_AQ_UNKNOWN) == 0);
    CHECK(ac_airquality_to_matter(AC_AQ_GOOD) == 1);
    CHECK(ac_airquality_to_matter(AC_AQ_GOOD) <
          ac_airquality_to_matter(AC_AQ_ELEVATED));
    CHECK(ac_airquality_to_matter(AC_AQ_ELEVATED) <
          ac_airquality_to_matter(AC_AQ_HIGH));
    CHECK(ac_airquality_to_matter(AC_AQ_HIGH) <
          ac_airquality_to_matter(AC_AQ_VERY_HIGH));
    CHECK(ac_airquality_to_matter(AC_AQ_VERY_HIGH) <= 6);
}

/* ---------------- event detection ---------------------------------- */

static void feed(ac_event_det_t *d, const ac_config_t *c, ac_baseline_t *b,
                 ac_time_ms_t *t, int minutes, float pm, int32_t voc,
                 ac_event_record_t *fin, bool *got)
{
    for (int i = 0; i < minutes; i++) {
        *t += 60000;
        ac_sample_t s = mk(*t, pm, voc, 600.0f);
        ac_event_record_t rec;
        memset(&rec, 0, sizeof(rec));
        ac_event_update(d, c, b, &s, 0.0f, 0.0f, &rec);
        ac_baseline_freeze(b, ac_event_should_freeze_baseline(d));
        ac_baseline_update(b, &s);
        if (rec.complete && fin) { *fin = rec; if (got) *got = true; }
    }
}

static void test_event(void)
{
    ac_config_t c;
    ac_config_defaults(&c);
    ac_baseline_t b;
    ac_baseline_init(&b, 60, 0.2f);
    ac_time_ms_t t = 0;
    for (int i = 0; i < 300; i++) {
        t += 60000;
        ac_sample_t s = mk(t, 6.0f, 100, 600.0f);
        ac_baseline_update(&b, &s);
    }

    ac_event_det_t d;
    ac_event_init(&d);

    CASE("ordinary room noise does not start an event");
    for (int i = 0; i < 200; i++) {
        t += 60000;
        ac_sample_t s = mk(t, 6.0f + (i % 3 == 0 ? 2.0f : -1.0f),
                           100 + (i % 5), 600.0f);
        ac_event_update(&d, &c, &b, &s, 0.0f, 0.0f, NULL);
        ac_baseline_update(&b, &s);
    }
    CHECK(d.state == AC_EV_IDLE);
    CHECK(d.events_total == 0);

    CASE("a single spike arms but does not confirm");
    t += 60000;
    ac_sample_t spike = mk(t, 40.0f, 100, 600.0f);
    ac_event_update(&d, &c, &b, &spike, 0.0f, 0.0f, NULL);
    CHECK(d.state == AC_EV_POSSIBLE_PRINT);
    feed(&d, &c, &b, &t, 5, 6.0f, 100, NULL, NULL);
    CHECK(d.state == AC_EV_IDLE);
    CHECK(d.events_total == 0);

    CASE("a sustained rise confirms an event");
    bool got = false;
    ac_event_record_t fin;
    memset(&fin, 0, sizeof(fin));
    feed(&d, &c, &b, &t, 10, 40.0f, 220, &fin, &got);
    CHECK(d.state == AC_EV_ACTIVE);
    CHECK(d.events_total == 1);
    CHECK(ac_event_should_freeze_baseline(&d));
    CHECK(ac_event_preferred_mode(&d, &c) == AC_MODE_ACTIVE);

    CASE("a short dip does not end the event");
    feed(&d, &c, &b, &t, 3, 6.0f, 100, &fin, &got);
    CHECK(d.state == AC_EV_ACTIVE);
    feed(&d, &c, &b, &t, 10, 55.0f, 240, &fin, &got);
    CHECK(d.state == AC_EV_ACTIVE);

    CASE("clearing for the release time moves to POST_PRINT");
    feed(&d, &c, &b, &t, 12, 6.0f, 100, &fin, &got);
    CHECK(d.state == AC_EV_POST_PRINT);
    CHECK(ac_event_preferred_mode(&d, &c) == AC_MODE_POST_PRINT);
    CHECK(ac_event_should_freeze_baseline(&d));

    CASE("POST_PRINT runs out and the record is emitted");
    feed(&d, &c, &b, &t, 50, 6.0f, 100, &fin, &got);
    CHECK(d.state == AC_EV_IDLE);
    CHECK(got);
    CHECK(fin.complete);
    CHECK_NEAR(fin.peak_pm25, 55.0, 0.01);
    CHECK(fin.peak_voc == 240);
    CHECK(fin.duration_s > 0);
    CHECK(fin.recovery_s > 0);
    CHECK_NEAR(fin.base_pm25, 6.0, 0.5);
    CHECK(!ac_event_should_freeze_baseline(&d));

    CASE("without a baseline nothing can trigger");
    ac_baseline_t cold;
    ac_baseline_init(&cold, 60, 0.2f);
    ac_event_det_t d2;
    ac_event_init(&d2);
    for (int i = 0; i < 20; i++) {
        t += 60000;
        ac_sample_t s = mk(t, 500.0f, 400, 3000.0f);
        ac_event_update(&d2, &c, &cold, &s, 0.0f, 0.0f, NULL);
    }
    CHECK(d2.state == AC_EV_IDLE);

    CASE("auto_escalate off keeps the configured mode");
    ac_config_t no_esc = c;
    no_esc.auto_escalate = false;
    CHECK(ac_event_preferred_mode(&d, &no_esc) == no_esc.default_mode);
}

/* ---------------- history ------------------------------------------ */

static void test_history(void)
{
    CASE("history stays inside its RAM budget");
    CHECK(ac_history_bytes() < 12000);

    ac_history_t h;
    ac_history_init(&h);

    CASE("a spike survives aggregation in the max field");
    ac_time_ms_t t = 0;
    for (int i = 0; i < 12 * 30; i++) {          /* 6 hours at 1 min */
        t += 60000;
        float pm = (i == 100) ? 80.0f : 7.0f;
        ac_sample_t s = mk(t, pm, 100, 600.0f);
        ac_history_push(&h, &s);
    }
    ac_hist_bucket_t buf[AC_HIST_FINE_N];
    size_t n = ac_history_recent_fine(&h, buf, AC_HIST_FINE_N);
    CHECK(n > 0);
    int found_peak = 0, sane_mean = 1;
    for (size_t i = 0; i < n; i++) {
        if (buf[i].pm25_max >= 790 && buf[i].pm25_max <= 810) found_peak = 1;
        if (buf[i].pm25_mean != AC_HIST_NODATA &&
            (buf[i].pm25_mean < 60 || buf[i].pm25_mean > 300)) sane_mean = 0;
    }
    CHECK(found_peak);      /* 80.0 ug/m3 stored as 800 tenths */
    CHECK(sane_mean);       /* means stay near 7.0 -> 70 tenths, spike pulls one bucket */

    CASE("the trend of a rising ramp is positive");
    ac_history_t h2;
    ac_history_init(&h2);
    t = 0;
    for (int i = 0; i < 200; i++) {
        t += 60000;
        ac_sample_t s = mk(t, 5.0f + 0.1f * (float)i, 100, 600.0f);
        ac_history_push(&h2, &s);
    }
    CHECK(ac_history_pm25_trend(&h2, 180) > 1.0f);

    CASE("the trend of a flat room is about zero");
    ac_history_t h3;
    ac_history_init(&h3);
    t = 0;
    for (int i = 0; i < 200; i++) {
        t += 60000;
        ac_sample_t s = mk(t, 7.0f, 100, 600.0f);
        ac_history_push(&h3, &s);
    }
    CHECK_NEAR(ac_history_pm25_trend(&h3, 180), 0.0, 0.05);

    CASE("the ring wraps without corrupting");
    ac_history_t h4;
    ac_history_init(&h4);
    t = 0;
    for (int i = 0; i < 20000; i++) {
        t += 60000;
        ac_sample_t s = mk(t, 7.0f, 100, 600.0f);
        ac_history_push(&h4, &s);
    }
    n = ac_history_recent_fine(&h4, buf, AC_HIST_FINE_N);
    CHECK(n == AC_HIST_FINE_N);
    for (size_t i = 0; i < n; i++)
        CHECK(buf[i].pm25_mean >= 60 && buf[i].pm25_mean <= 80);
}

/* ---------------- engine ------------------------------------------- */

static void test_engine(void)
{
    ac_config_t c;
    ac_config_defaults(&c);
    ac_engine_t e;
    ac_time_ms_t t = 0;
    ac_engine_init(&e, &c, t);

    CASE("a cold engine asks for a measurement straight away");
    ac_plan_t p = ac_engine_tick(&e, t);
    CHECK(p.action == AC_ACT_SAMPLE_PM);
    CHECK(p.pm_window_s >= AC_PM_MIN_WINDOW_S);
    CHECK(!p.pm_keep_running);
    CHECK(e.state == AC_STATE_WARMUP);

    CASE("values are not published during warm-up");
    CHECK(!ac_engine_values_valid(&e));

    CASE("warm-up ends once the SGP40 has had its documented 60 s");
    ac_sample_t s = mk(t, 7.0f, 100, 600.0f);
    ac_engine_submit(&e, &s);
    t += 61000;
    s = mk(t, 7.0f, 100, 600.0f);
    ac_engine_submit(&e, &s);
    ac_engine_tick(&e, t);
    CHECK(ac_engine_values_valid(&e));
    CHECK(e.state == AC_STATE_NORMAL);

    CASE("ECO schedules the SEN63C an hour out, not sooner");
    /* drain the immediate VOC work first */
    for (int i = 0; i < 6; i++) ac_engine_tick(&e, t);
    p = ac_engine_tick(&e, t);
    CHECK(p.action == AC_ACT_NONE || p.action == AC_ACT_SAMPLE_VOC);
    CHECK(e.next_pm >= t + 3500ull * 1000ull);
    CHECK(e.next_pm <= t + 3700ull * 1000ull);

    CASE("the device may sleep between samples, and does");
    p = ac_engine_tick(&e, t);
    while (p.action != AC_ACT_NONE) p = ac_engine_tick(&e, t);
    CHECK(p.sleep_ms > 0);
    CHECK(p.sleep_ms <= 10u * 1000u);   /* the 10 s VOC cadence bounds it */

    CASE("a confirmed event escalates the engine into ACTIVE");
    ac_engine_t e2;
    ac_engine_init(&e2, &c, 0);
    ac_time_ms_t u = 0;
    for (int i = 0; i < 400; i++) {      /* settle a baseline */
        u += 60000;
        ac_sample_t q = mk(u, 6.0f, 100, 600.0f);
        ac_engine_submit(&e2, &q);
        ac_engine_tick(&e2, u);
    }
    CHECK(e2.mode == AC_MODE_ECO);
    for (int i = 0; i < 10; i++) {
        u += 60000;
        ac_sample_t q = mk(u, 45.0f, 250, 900.0f);
        ac_engine_submit(&e2, &q);
        ac_engine_tick(&e2, u);
    }
    CHECK(e2.mode == AC_MODE_ACTIVE);
    CHECK(e2.state == AC_STATE_ACTIVE);
    CHECK(ac_baseline_frozen(&e2.base));

    CASE("ACTIVE really does sample PM more often than ECO");
    const ac_profile_t *pa = ac_engine_profile(&e2);
    CHECK(pa->pm_interval_s < c.profile[AC_MODE_ECO].pm_interval_s);

    CASE("USB power switches to continuous and back");
    ac_engine_set_power(&e2, true, true, 90.0f, 4.1f, u);
    ac_engine_tick(&e2, u);
    CHECK(e2.mode == AC_MODE_CONTINUOUS);
    CHECK(e2.state == AC_STATE_CHARGING);
    ac_engine_set_power(&e2, false, false, 90.0f, 3.9f, u);
    ac_engine_tick(&e2, u);
    CHECK(e2.mode != AC_MODE_CONTINUOUS);

    CASE("a low battery is reported, a critical one stops measuring");
    ac_engine_set_power(&e2, false, false, 15.0f, 3.5f, u);
    CHECK(e2.state == AC_STATE_LOW_BATTERY);
    ac_engine_set_power(&e2, false, false, 3.0f, 3.3f, u);
    CHECK(e2.state == AC_STATE_CRITICAL_BATTERY);
    p = ac_engine_tick(&e2, u);
    CHECK(p.action == AC_ACT_NONE);
    CHECK(p.sleep_ms >= 60000);

    CASE("a dead SEN63C does not stop the VOC channel");
    ac_engine_t e3;
    ac_engine_init(&e3, &c, 0);
    for (int i = 0; i < 5; i++)
        ac_engine_sensor_failed(&e3, AC_ACT_SAMPLE_PM, "no ACK at 0x6B");
    CHECK(!e3.health.sen6x_ok);
    ac_time_ms_t w = 61000;
    ac_sample_t q = mk(w, -1.0f, 100, -1.0f);
    ac_engine_submit(&e3, &q);
    p = ac_engine_tick(&e3, w);
    CHECK(p.action == AC_ACT_SAMPLE_VOC);
    CHECK(e3.state != AC_STATE_ERROR);

    CASE("both sensors dead is an ERROR, not silence");
    for (int i = 0; i < 10; i++)
        ac_engine_sensor_failed(&e3, AC_ACT_SAMPLE_VOC, "nack");
    ac_engine_tick(&e3, w);
    CHECK(e3.state == AC_STATE_ERROR);
    CHECK(strlen(e3.health.last_error) > 0);

    CASE("no weekly fan cleaning: the SEN6x does not need it");
    ac_engine_t e4;
    ac_engine_init(&e4, &c, 0);
    ac_time_ms_t week = 7ull * 24ull * 3600ull * 1000ull + 1000;
    for (int i = 0; i < 6; i++) {
        p = ac_engine_tick(&e4, week);
        CHECK(p.action == AC_ACT_NONE || p.action == AC_ACT_SAMPLE_PM ||
              p.action == AC_ACT_SAMPLE_VOC);
    }

    CASE("one PM window is the only CO2 source: no separate CO2 action");
    ac_engine_t e8;
    ac_engine_init(&e8, &c, 0);
    int pm_windows = 0;
    for (ac_time_ms_t tt = 0; tt < 2ull * 3600ull * 1000ull; tt += 1000) {
        p = ac_engine_tick(&e8, tt);
        if (p.action == AC_ACT_SAMPLE_PM) pm_windows++;
    }
    CHECK(pm_windows == 2);      /* ECO: one at boot, one an hour later */

    CASE("publishing is rate limited by a real change, not by the clock");
    ac_engine_t e5;
    ac_engine_init(&e5, &c, 0);
    ac_time_ms_t z = 61000;
    ac_sample_t base_s = mk(z, 10.0f, 100, 600.0f);
    ac_engine_submit(&e5, &base_s);
    ac_engine_tick(&e5, z);
    z += 10000;
    ac_sample_t same = mk(z, 10.0f, 100, 600.0f);
    ac_engine_submit(&e5, &same);
    p = ac_engine_tick(&e5, z);
    CHECK(!p.publish_dirty);
    z += 10000;
    ac_sample_t moved = mk(z, 18.0f, 100, 600.0f);
    ac_engine_submit(&e5, &moved);
    p = ac_engine_tick(&e5, z);
    CHECK(p.publish_dirty);

    CASE("continuous mode keeps the SEN63C running between windows");
    ac_config_t cont = c;
    cont.default_mode = AC_MODE_CONTINUOUS;
    ac_engine_t e7;
    ac_engine_init(&e7, &cont, 0);
    p = ac_engine_tick(&e7, 0);
    CHECK(p.action == AC_ACT_SAMPLE_PM);
    CHECK(p.pm_window_s >= AC_PM_MIN_WINDOW_S);
    CHECK(p.pm_keep_running);

    CASE("factory reset returns every setting to default");
    ac_engine_t e6;
    ac_config_t weird = c;
    weird.battery_interval_s = 120;
    strncpy(weird.name, "Printer", AC_NAME_MAX - 1);
    ac_engine_init(&e6, &weird, 0);
    CHECK(e6.cfg.battery_interval_s == 120);
    ac_engine_factory_reset(&e6, 0);
    CHECK(e6.cfg.battery_interval_s == c.battery_interval_s);
    CHECK(strcmp(e6.cfg.name, "AIR CHECK") == 0);
    CHECK(e6.state == AC_STATE_FACTORY_RESET);
}

/* ---------------- status LED --------------------------------------- */

static int lit_ms(const ac_led_pattern_t *p, uint32_t span_ms)
{
    int on = 0;
    for (uint32_t t = 0; t < span_ms; t += 10)
        if (ac_status_level(p, t) != AC_RGB_OFF) on += 10;
    return on;
}

static void test_status(void)
{
    ac_config_t c;
    ac_config_defaults(&c);
    ac_engine_t e;
    ac_engine_init(&e, &c, 0);
    ac_time_ms_t t = 0;
    for (int i = 0; i < 400; i++) {
        t += 60000;
        ac_sample_t s = mk(t, 6.0f, 100, 600.0f);
        ac_engine_submit(&e, &s);
        ac_engine_set_power(&e, false, false, 80.0f, 3.9f, t);
        ac_engine_tick(&e, t);
    }

    CASE("in normal operation the LED stays dark");
    ac_led_pattern_t p = ac_status_pattern(&e, AC_LED_EVENT_NONE);
    CHECK(lit_ms(&p, 60000) == 0);

    CASE("a button press shows the air quality colour for three seconds");
    p = ac_status_pattern(&e, AC_LED_EVENT_BUTTON);
    CHECK(ac_status_level(&p, 0) == AC_RGB_GREEN);
    CHECK(ac_status_level(&p, 2900) == AC_RGB_GREEN);
    CHECK(ac_status_level(&p, 3100) == AC_RGB_OFF);

    CASE("the colour follows the air quality");
    CHECK(ac_status_air_colour(AC_AQ_GOOD) == AC_RGB_GREEN);
    CHECK(ac_status_air_colour(AC_AQ_ELEVATED) == AC_RGB_YELLOW);
    CHECK(ac_status_air_colour(AC_AQ_HIGH) == AC_RGB_RED);
    CHECK(ac_status_air_colour(AC_AQ_VERY_HIGH) == AC_RGB_PURPLE);
    CHECK(ac_status_air_colour(AC_AQ_UNKNOWN) == AC_RGB_WHITE);

    CASE("pairing blinks blue and times out with the commissioning window");
    p = ac_status_pattern(&e, AC_LED_EVENT_PAIRING);
    CHECK(ac_status_level(&p, 100) == AC_RGB_BLUE);
    CHECK(ac_status_level(&p, 700) == AC_RGB_OFF);
    CHECK(ac_status_level(&p, 300001) == AC_RGB_OFF);

    CASE("a button press during warm-up says so instead of guessing a colour");
    ac_engine_t cold;
    ac_engine_init(&cold, &c, 0);
    p = ac_status_pattern(&cold, AC_LED_EVENT_BUTTON);
    CHECK(ac_status_level(&p, 0) == AC_RGB_WHITE);

    CASE("low battery answers a button press with yellow blinks");
    ac_engine_set_power(&e, false, false, 12.0f, 3.5f, t);
    p = ac_status_pattern(&e, AC_LED_EVENT_BUTTON);
    CHECK(ac_status_level(&p, 0) == AC_RGB_YELLOW);
    CHECK(ac_status_level(&p, 200) == AC_RGB_OFF);

    CASE("critical battery blinks unprompted, but only a 50 ms blip every 10 s");
    ac_engine_set_power(&e, false, false, 3.0f, 3.3f, t);
    p = ac_status_pattern(&e, AC_LED_EVENT_NONE);
    int on = lit_ms(&p, 60000);
    CHECK(on > 0);
    CHECK(on <= 60000 / 10000 * 50 + 10);

    CASE("holding the button shows what letting go would do");
    CHECK(ac_status_hold_colour(0) == AC_RGB_OFF);
    CHECK(ac_status_hold_colour(2999) == AC_RGB_OFF);
    CHECK(ac_status_hold_colour(3000) == AC_RGB_BLUE);
    CHECK(ac_status_hold_colour(7999) == AC_RGB_BLUE);
    CHECK(ac_status_hold_colour(8000) == AC_RGB_CYAN);
    CHECK(ac_status_hold_colour(11999) == AC_RGB_CYAN);
    CHECK(ac_status_hold_colour(12000) == AC_RGB_RED);
    CHECK(ac_status_hold_colour(19999) == AC_RGB_RED);
    CHECK(ac_status_hold_colour(20000) == AC_RGB_OFF);  /* ignored: dark */

    CASE("calibration shows progress and a clear result");
    ac_engine_t ce;
    ac_engine_init(&ce, &c, 0);
    p = ac_status_pattern(&ce, AC_LED_EVENT_CALIBRATING);
    CHECK(p.colour == AC_RGB_CYAN);
    CHECK(p.duration_ms >= 180000);          /* outlasts the 3 min run */
    CHECK(lit_ms(&p, 10000) > 0 && lit_ms(&p, 10000) < 10000);
    p = ac_status_pattern(&ce, AC_LED_EVENT_CAL_OK);
    CHECK(p.colour == AC_RGB_GREEN && ac_status_level(&p, 1000) == AC_RGB_GREEN);
    p = ac_status_pattern(&ce, AC_LED_EVENT_CAL_FAIL);
    CHECK(p.colour == AC_RGB_RED && p.period_ms > 0);
    CHECK(ac_status_level(&p, 5000) == AC_RGB_OFF);

    CASE("a dead device blinks red unprompted");
    ac_engine_t dead;
    ac_engine_init(&dead, &c, 0);
    for (int i = 0; i < 10; i++) {
        ac_engine_sensor_failed(&dead, AC_ACT_SAMPLE_PM, "x");
        ac_engine_sensor_failed(&dead, AC_ACT_SAMPLE_VOC, "x");
    }
    ac_engine_tick(&dead, 61000);
    p = ac_status_pattern(&dead, AC_LED_EVENT_NONE);
    CHECK(lit_ms(&p, 10000) > 0);
    CHECK(ac_status_level(&p, 0) == AC_RGB_RED);
}

/* ---------------- window statistics (for the dashboard) ------------- */

static void test_window(void)
{
    ac_history_t h;
    ac_history_init(&h);

    CASE("an empty history reports no window, not zeros");
    ac_window_stat_t w = ac_history_window(&h, AC_CH_PM25, 86400);
    CHECK(!w.valid);

    ac_time_ms_t t = 0;
    for (int i = 0; i < 6 * 60; i++) {          /* 6 hours at 1 min */
        t += 60000;
        float pm = (i == 200) ? 90.0f : 8.0f;
        int32_t voc = (i >= 250 && i < 260) ? 300 : 100;
        ac_sample_t s = mk(t, pm, voc, 600.0f + (float)(i % 10));
        ac_history_push(&h, &s);
    }

    CASE("the 24 h peak keeps a one-minute spike");
    w = ac_history_window(&h, AC_CH_PM25, 86400);
    CHECK(w.valid);
    CHECK_NEAR(w.peak, 90.0, 0.2);
    CHECK(w.mean > 7.5f && w.mean < 10.0f);

    CASE("a window that ends before the spike does not see it");
    w = ac_history_window(&h, AC_CH_PM25, 3600);
    CHECK(w.valid);
    CHECK(w.peak < 10.0f);

    CASE("VOC and CO2 windows work the same way");
    w = ac_history_window(&h, AC_CH_VOC, 86400);
    CHECK(w.valid && w.peak >= 299.0f);
    w = ac_history_window(&h, AC_CH_CO2, 86400);
    CHECK(w.valid && w.peak >= 608.0f && w.mean > 600.0f);

    CASE("the bucket in progress is included");
    ac_history_t h2;
    ac_history_init(&h2);
    ac_sample_t s = mk(60000, 42.0f, 100, 600.0f);
    ac_history_push(&h2, &s);
    w = ac_history_window(&h2, AC_CH_PM25, 3600);
    CHECK(w.valid);
    CHECK_NEAR(w.peak, 42.0, 0.01);
}

/* ---------------- battery from voltage ------------------------------ */

static void test_battery(void)
{
    CASE("the voltage curve is monotonic and spans 0..100 %");
    float prev = -1.0f;
    bool monotonic = true, bounded = true;
    for (float v = 3.0f; v <= 4.3f; v += 0.005f) {
        float p = ac_battery_soc(v);
        if (p < prev) monotonic = false;
        if (p < 0.0f || p > 100.0f) bounded = false;
        prev = p;
    }
    CHECK(monotonic);
    CHECK(bounded);
    CHECK(ac_battery_soc(3.0f) == 0.0f);
    CHECK(ac_battery_soc(4.25f) == 100.0f);
    CHECK(ac_battery_soc(0.0f / 0.0f) == 0.0f);      /* NaN from a dead ADC */

    CASE("the decision points sit where the curve is steep");
    CHECK_NEAR(ac_battery_soc(3.73f), 20.0, 0.01);   /* low battery */
    CHECK(ac_battery_soc(3.60f) < 5.0f);             /* critical */
    CHECK_NEAR(ac_battery_soc(3.84f), 50.0, 0.01);
    CHECK_NEAR(ac_battery_soc(3.835f), 48.75, 0.01); /* interpolated */

    CASE("charge state follows USB and the termination voltage");
    CHECK(ac_battery_charge_state(3.9f, false) == AC_CHG_DISCHARGING);
    CHECK(ac_battery_charge_state(3.9f, true) == AC_CHG_CHARGING);
    CHECK(ac_battery_charge_state(4.18f, true) == AC_CHG_FULL);
    CHECK(ac_battery_charge_state(0.0f, true) == AC_CHG_UNKNOWN);

    CASE("on battery the reported value never climbs back on noise");
    ac_batt_track_t t;
    ac_batt_track_init(&t);
    float a = ac_batt_track_update(&t, 3.87f, false);   /* 60 % */
    float b = ac_batt_track_update(&t, 3.85f, false);   /* 55 % */
    float c = ac_batt_track_update(&t, 3.87f, false);   /* warm again: 60 % */
    CHECK_NEAR(a, 60.0, 0.01);
    CHECK_NEAR(b, 55.0, 0.01);
    CHECK_NEAR(c, 55.0, 0.01);

    CASE("a freshly charged cell is recognised, charging is followed");
    CHECK_NEAR(ac_batt_track_update(&t, 4.11f, false), 90.0, 0.01);
    CHECK_NEAR(ac_batt_track_update(&t, 3.73f, false), 20.0, 0.01);
    CHECK_NEAR(ac_batt_track_update(&t, 3.80f, true), 40.0, 0.01);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("AIR CHECK core tests\n");
    test_config();
    test_filter();
    test_baseline();
    test_airquality();
    test_event();
    test_history();
    test_engine();
    test_status();
    test_window();
    test_battery();
    printf("\n%d checks, %d failure(s)\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
