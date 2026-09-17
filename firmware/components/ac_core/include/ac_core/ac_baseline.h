/* Persistent baseline: "what does this room normally look like".
 *
 * Two rules the brief insists on and that this implementation enforces:
 *  - the baseline is NOT updated while an emission event is in progress,
 *    otherwise the event quietly becomes the new normal;
 *  - resetting the baseline is not the same thing as calibrating the sensor.
 *    ac_baseline_reset() only touches our own reference numbers.  It never
 *    writes to a sensor.  Forced recalibration of the SCD41 is a separate,
 *    separately documented operation in ac_hal.
 */
#ifndef AC_BASELINE_H
#define AC_BASELINE_H

#include "ac_filter.h"
#include "ac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AC_BASELINE_MAGIC 0x42534C31u  /* "BSL1" */

typedef struct {
    uint32_t magic;
    uint32_t saved_unix;     /* 0 if the device had no time source */
    float pm25;
    float pm10;
    float pm1;
    int32_t voc;
    float co2;
    uint32_t samples;
    bool valid;
} ac_baseline_store_t;

typedef struct {
    ac_baseline_store_t s;
    ac_ema_t e_pm25, e_pm10, e_pm1, e_co2;
    ac_ema_t e_voc;
    ac_time_ms_t last_update;
    uint32_t update_s;
    bool frozen;
} ac_baseline_t;

void ac_baseline_init(ac_baseline_t *b, uint32_t update_s, float alpha);
/* Restore from NVS.  Returns false (and leaves a freshly initialised baseline)
 * when the blob is unusable. */
bool ac_baseline_restore(ac_baseline_t *b, const ac_baseline_store_t *st);
const ac_baseline_store_t *ac_baseline_snapshot(const ac_baseline_t *b);

/* Feed a sample.  Ignored while frozen, and ignored for channels whose
 * _fresh flag is clear. */
void ac_baseline_update(ac_baseline_t *b, const ac_sample_t *s);
/* Freeze during an event so a print does not become the new normal. */
void ac_baseline_freeze(ac_baseline_t *b, bool frozen);
bool ac_baseline_frozen(const ac_baseline_t *b);
/* "SET ROOM AIR AS BASELINE": take the current sample as the reference. */
void ac_baseline_reset(ac_baseline_t *b, const ac_sample_t *now);
bool ac_baseline_valid(const ac_baseline_t *b);

float   ac_baseline_pm25(const ac_baseline_t *b);
float   ac_baseline_pm10(const ac_baseline_t *b);
int32_t ac_baseline_voc(const ac_baseline_t *b);
float   ac_baseline_co2(const ac_baseline_t *b);

/* Deltas against the baseline.  Return 0 when no baseline exists yet, so a
 * cold device never reports a spurious excursion. */
float   ac_baseline_d_pm25(const ac_baseline_t *b, const ac_sample_t *s);
int32_t ac_baseline_d_voc(const ac_baseline_t *b, const ac_sample_t *s);
float   ac_baseline_d_co2(const ac_baseline_t *b, const ac_sample_t *s);

#ifdef __cplusplus
}
#endif
#endif
