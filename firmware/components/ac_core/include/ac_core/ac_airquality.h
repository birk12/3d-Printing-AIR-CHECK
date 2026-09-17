/* Air quality classification.
 *
 * The output is deliberately a *reporting* state with a stated reason, not a
 * health verdict.  Wording rule for everything downstream: "air quality
 * elevated", never "this air is unsafe".  See docs/RISKS.md.
 */
#ifndef AC_AIRQUALITY_H
#define AC_AIRQUALITY_H

#include "ac_config.h"
#include "ac_baseline.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ac_air_quality_t level;
    uint32_t reason;        /* bitmask of ac_reason_t */
    ac_air_quality_t pm25_level, pm10_level, voc_level, co2_level;
} ac_aq_result_t;

/* Pure function of (sample, config, baseline, trends).  No state. */
ac_aq_result_t ac_airquality_eval(const ac_config_t *cfg,
                                  const ac_baseline_t *base,
                                  const ac_sample_t *s,
                                  float pm25_rate_per_min,
                                  float voc_rate_per_min);

/* Matter AirQualityEnum, spec 1.4 cluster 0x005B:
 * 0 Unknown, 1 Good, 2 Fair, 3 Moderate, 4 Poor, 5 VeryPoor, 6 ExtremelyPoor */
uint8_t ac_airquality_to_matter(ac_air_quality_t q);

#ifdef __cplusplus
}
#endif
#endif
