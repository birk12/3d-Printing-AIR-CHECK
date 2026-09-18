/* Rolling history.
 *
 * RAW -> FILTER -> AGGREGATE -> STORE, as the brief specifies.  Raw samples
 * are never stored: two rings of aggregated buckets are kept instead, which
 * costs a fixed 5.4 kB of RAM and survives a reboot through NVS.
 *
 *   fine   : 5 min buckets, 288 of them  -> 24 h
 *   coarse : 1 h buckets, 168 of them    -> 7 days
 *
 * Each bucket holds min / mean / max so that a short spike is still visible
 * after aggregation - averaging alone would hide exactly what we are looking
 * for.
 */
#ifndef AC_HISTORY_H
#define AC_HISTORY_H

#include "ac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AC_HIST_FINE_BUCKET_S    300u
#define AC_HIST_FINE_N           288u
#define AC_HIST_COARSE_BUCKET_S  3600u
#define AC_HIST_COARSE_N         168u

/* Values are stored as int16 in fixed point to keep the rings small:
 *   PM   : 0.1 ug/m3   (0 .. 3276 ug/m3)
 *   VOC  : 1 index point
 *   CO2  : 1 ppm (clipped at 32767)
 *   T    : 0.01 degC
 *   RH   : 0.1 %
 * -32768 marks "no data". */
#define AC_HIST_NODATA  ((int16_t)-32768)

typedef struct {
    int16_t pm25_min, pm25_mean, pm25_max;
    int16_t pm10_mean;
    int16_t voc_mean, voc_max;
    int16_t co2_mean, co2_max;
    int16_t t_mean, rh_mean;
    uint16_t n;
} ac_hist_bucket_t;

/* accumulator for the bucket currently being filled */
typedef struct {
    double pm25, pm10, voc, co2, t, rh;
    float pm25_min, pm25_max, voc_max, co2_max;
    uint32_t n_pm, n_voc, n_co2, n_th;
} ac_hist_acc_t;

typedef struct {
    ac_hist_bucket_t fine[AC_HIST_FINE_N];
    ac_hist_bucket_t coarse[AC_HIST_COARSE_N];
    uint16_t fine_head, coarse_head;
    ac_time_ms_t fine_start, coarse_start;
    ac_hist_acc_t acc_fine, acc_coarse;
} ac_history_t;

void ac_history_init(ac_history_t *h);
void ac_history_push(ac_history_t *h, const ac_sample_t *s);
/* Force-close the bucket in progress, e.g. before a deliberate reboot. */
void ac_history_flush(ac_history_t *h, ac_time_ms_t now);

/* Read back the most recent `n` fine buckets, newest last.  Returns how many
 * were actually written. */
size_t ac_history_recent_fine(const ac_history_t *h, ac_hist_bucket_t *out, size_t n);
size_t ac_history_recent_coarse(const ac_history_t *h, ac_hist_bucket_t *out, size_t n);
/* Trend over the last `minutes`, in ug/m3 per hour.  Used by screen 6. */
float ac_history_pm25_trend(const ac_history_t *h, uint32_t minutes);
size_t ac_history_bytes(void);

/* Peak and mean over the last `seconds`, for the Matter PeakMeasuredValue and
 * AverageMeasuredValue attributes that the dashboard reads.  Built from the
 * 5 min ring plus the bucket still being filled, so a spike in the last few
 * minutes is not missed.  PM10 only stores a mean per bucket, so its "peak" is
 * the highest 5 min mean - documented, not hidden. */
typedef enum { AC_CH_PM25 = 0, AC_CH_PM10, AC_CH_CO2, AC_CH_VOC } ac_channel_t;
typedef struct { float peak; float mean; bool valid; } ac_window_stat_t;
ac_window_stat_t ac_history_window(const ac_history_t *h, ac_channel_t ch,
                                   uint32_t seconds);

#ifdef __cplusplus
}
#endif
#endif
