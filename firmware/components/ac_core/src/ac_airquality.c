#include "ac_core/ac_airquality.h"

static ac_air_quality_t grade_f(float v, float e, float h, float vh)
{
    if (v < 0.0f) return AC_AQ_UNKNOWN;
    if (v >= vh) return AC_AQ_VERY_HIGH;
    if (v >= h)  return AC_AQ_HIGH;
    if (v >= e)  return AC_AQ_ELEVATED;
    return AC_AQ_GOOD;
}

static ac_air_quality_t grade_i(int32_t v, int32_t e, int32_t h, int32_t vh)
{
    if (v < 0) return AC_AQ_UNKNOWN;
    if (v >= vh) return AC_AQ_VERY_HIGH;
    if (v >= h)  return AC_AQ_HIGH;
    if (v >= e)  return AC_AQ_ELEVATED;
    return AC_AQ_GOOD;
}

ac_aq_result_t ac_airquality_eval(const ac_config_t *cfg,
                                  const ac_baseline_t *base,
                                  const ac_sample_t *s,
                                  float pm25_rate, float voc_rate)
{
    ac_aq_result_t r = { AC_AQ_UNKNOWN, AC_REASON_NONE,
                         AC_AQ_UNKNOWN, AC_AQ_UNKNOWN, AC_AQ_UNKNOWN, AC_AQ_UNKNOWN };

    r.pm25_level = grade_f(s->pm25, cfg->pm25_elevated, cfg->pm25_high, cfg->pm25_very_high);
    r.pm10_level = grade_f(s->pm10, cfg->pm10_elevated, cfg->pm10_high, cfg->pm10_very_high);
    r.voc_level  = grade_i(s->voc_index, cfg->voc_elevated, cfg->voc_high, cfg->voc_very_high);
    r.co2_level  = grade_f(s->co2, cfg->co2_elevated, cfg->co2_high, cfg->co2_very_high);

    /* Worst channel wins, and every channel at or above ELEVATED names itself
     * in the reason mask so the display and the logs can say *why*. */
    ac_air_quality_t worst = AC_AQ_UNKNOWN;
    struct { ac_air_quality_t lvl; uint32_t bit; } ch[] = {
        { r.pm25_level, AC_REASON_PM25 },
        { r.pm10_level, AC_REASON_PM10 },
        { r.voc_level,  AC_REASON_VOC  },
        { r.co2_level,  AC_REASON_CO2  },
    };
    for (unsigned i = 0; i < sizeof(ch) / sizeof(ch[0]); i++) {
        if (ch[i].lvl == AC_AQ_UNKNOWN) continue;
        if (worst == AC_AQ_UNKNOWN || ch[i].lvl > worst) worst = ch[i].lvl;
    }
    if (worst == AC_AQ_UNKNOWN) return r;   /* nothing measured yet */

    for (unsigned i = 0; i < sizeof(ch) / sizeof(ch[0]); i++)
        if (ch[i].lvl >= AC_AQ_ELEVATED && ch[i].lvl == worst)
            r.reason |= ch[i].bit;

    /* A steep rise that has not yet crossed a threshold still deserves to be
     * reported - that is exactly the first minute of a print.  Only escalate
     * GOOD to ELEVATED this way; never further, because a trend on its own is
     * not evidence of a concentration. */
    bool pm_rising = pm25_rate >= cfg->ev_pm25_rate;
    bool voc_rising = voc_rate >= (float)cfg->ev_voc_delta / 10.0f;
    if (pm_rising && ac_baseline_valid(base)) r.reason |= AC_REASON_PM_RISING;
    if (voc_rising && ac_baseline_valid(base)) r.reason |= AC_REASON_VOC_RISING;
    if (worst == AC_AQ_GOOD && (pm_rising || voc_rising) && ac_baseline_valid(base))
        worst = AC_AQ_ELEVATED;

    r.level = worst;
    return r;
}

uint8_t ac_airquality_to_matter(ac_air_quality_t q)
{
    switch (q) {
    case AC_AQ_GOOD:      return 1;  /* Good */
    case AC_AQ_ELEVATED:  return 3;  /* Moderate - we skip Fair so that the
                                      * four device states map onto four
                                      * distinct Matter values */
    case AC_AQ_HIGH:      return 4;  /* Poor */
    case AC_AQ_VERY_HIGH: return 5;  /* VeryPoor */
    default:              return 0;  /* Unknown */
    }
}
