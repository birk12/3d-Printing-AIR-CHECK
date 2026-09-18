#include "ac_core/ac_history.h"
#include <math.h>
#include <string.h>

static int16_t fx(double v, double scale)
{
    if (!(v > -1e9 && v < 1e9)) return AC_HIST_NODATA;
    double x = v * scale;
    if (x > 32767.0) x = 32767.0;
    if (x < -32767.0) x = -32767.0;
    return (int16_t)lrint(x);
}

static void acc_reset(ac_hist_acc_t *a)
{
    memset(a, 0, sizeof(*a));
    a->pm25_min =  1e9f;
    a->pm25_max = -1e9f;
    a->voc_max  = -1e9f;
    a->co2_max  = -1e9f;
}

void ac_history_init(ac_history_t *h)
{
    memset(h, 0, sizeof(*h));
    for (unsigned i = 0; i < AC_HIST_FINE_N; i++) h->fine[i].n = 0;
    for (unsigned i = 0; i < AC_HIST_COARSE_N; i++) h->coarse[i].n = 0;
    acc_reset(&h->acc_fine);
    acc_reset(&h->acc_coarse);
}

#define ACC_ADD(A, s) do {                                                    \
    if ((s)->pm_fresh && (s)->pm25 >= 0.0f) {                                 \
        (A).pm25 += (s)->pm25; (A).pm10 += (s)->pm10; (A).n_pm++;             \
        if ((s)->pm25 < (A).pm25_min) (A).pm25_min = (s)->pm25;               \
        if ((s)->pm25 > (A).pm25_max) (A).pm25_max = (s)->pm25;               \
    }                                                                         \
    if ((s)->voc_fresh && (s)->voc_index >= 0) {                              \
        (A).voc += (double)(s)->voc_index; (A).n_voc++;                       \
        if ((float)(s)->voc_index > (A).voc_max) (A).voc_max = (float)(s)->voc_index; \
    }                                                                         \
    if ((s)->co2_fresh && (s)->co2 > 0.0f) {                                  \
        (A).co2 += (s)->co2; (A).n_co2++;                                     \
        if ((s)->co2 > (A).co2_max) (A).co2_max = (s)->co2;                   \
    }                                                                         \
    if ((s)->th_fresh) {                                                      \
        (A).t += (s)->temperature; (A).rh += (s)->humidity; (A).n_th++;       \
    }                                                                         \
} while (0)

#define ACC_CLOSE(A, B) do {                                                  \
    memset(&(B), 0, sizeof(B));                                               \
    (B).pm25_mean = (A).n_pm ? fx((A).pm25 / (A).n_pm, 10.0) : AC_HIST_NODATA;\
    (B).pm10_mean = (A).n_pm ? fx((A).pm10 / (A).n_pm, 10.0) : AC_HIST_NODATA;\
    (B).pm25_min  = (A).n_pm ? fx((A).pm25_min, 10.0) : AC_HIST_NODATA;       \
    (B).pm25_max  = (A).n_pm ? fx((A).pm25_max, 10.0) : AC_HIST_NODATA;       \
    (B).voc_mean  = (A).n_voc ? fx((A).voc / (A).n_voc, 1.0) : AC_HIST_NODATA;\
    (B).voc_max   = (A).n_voc ? fx((A).voc_max, 1.0) : AC_HIST_NODATA;        \
    (B).co2_mean  = (A).n_co2 ? fx((A).co2 / (A).n_co2, 1.0) : AC_HIST_NODATA;\
    (B).co2_max   = (A).n_co2 ? fx((A).co2_max, 1.0) : AC_HIST_NODATA;        \
    (B).t_mean    = (A).n_th ? fx((A).t / (A).n_th, 100.0) : AC_HIST_NODATA;  \
    (B).rh_mean   = (A).n_th ? fx((A).rh / (A).n_th, 10.0) : AC_HIST_NODATA;  \
    (B).n = (uint16_t)((A).n_pm + (A).n_voc + (A).n_co2);                     \
} while (0)

void ac_history_push(ac_history_t *h, const ac_sample_t *s)
{
    if (h->fine_start == 0) { h->fine_start = s->t; h->coarse_start = s->t; }

    ACC_ADD(h->acc_fine, s);
    ACC_ADD(h->acc_coarse, s);

    while (s->t - h->fine_start >= (ac_time_ms_t)AC_HIST_FINE_BUCKET_S * 1000u) {
        ACC_CLOSE(h->acc_fine, h->fine[h->fine_head]);
        h->fine_head = (uint16_t)((h->fine_head + 1) % AC_HIST_FINE_N);
        acc_reset(&h->acc_fine);
        h->fine_start += (ac_time_ms_t)AC_HIST_FINE_BUCKET_S * 1000u;
    }
    while (s->t - h->coarse_start >= (ac_time_ms_t)AC_HIST_COARSE_BUCKET_S * 1000u) {
        ACC_CLOSE(h->acc_coarse, h->coarse[h->coarse_head]);
        h->coarse_head = (uint16_t)((h->coarse_head + 1) % AC_HIST_COARSE_N);
        acc_reset(&h->acc_coarse);
        h->coarse_start += (ac_time_ms_t)AC_HIST_COARSE_BUCKET_S * 1000u;
    }
}

void ac_history_flush(ac_history_t *h, ac_time_ms_t now)
{
    (void)now;
    if (h->acc_fine.n_pm || h->acc_fine.n_voc || h->acc_fine.n_co2) {
        ACC_CLOSE(h->acc_fine, h->fine[h->fine_head]);
        h->fine_head = (uint16_t)((h->fine_head + 1) % AC_HIST_FINE_N);
        acc_reset(&h->acc_fine);
    }
    if (h->acc_coarse.n_pm || h->acc_coarse.n_voc || h->acc_coarse.n_co2) {
        ACC_CLOSE(h->acc_coarse, h->coarse[h->coarse_head]);
        h->coarse_head = (uint16_t)((h->coarse_head + 1) % AC_HIST_COARSE_N);
        acc_reset(&h->acc_coarse);
    }
}

static size_t recent(const ac_hist_bucket_t *ring, size_t ring_n, uint16_t head,
                     ac_hist_bucket_t *out, size_t n)
{
    if (n > ring_n) n = ring_n;
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (head + ring_n - n + i) % ring_n;
        if (ring[idx].n == 0 && ring[idx].pm25_mean == 0 &&
            ring[idx].voc_mean == 0) continue;   /* never filled */
        out[w++] = ring[idx];
    }
    return w;
}

size_t ac_history_recent_fine(const ac_history_t *h, ac_hist_bucket_t *out, size_t n)
{
    return recent(h->fine, AC_HIST_FINE_N, h->fine_head, out, n);
}

size_t ac_history_recent_coarse(const ac_history_t *h, ac_hist_bucket_t *out, size_t n)
{
    return recent(h->coarse, AC_HIST_COARSE_N, h->coarse_head, out, n);
}

float ac_history_pm25_trend(const ac_history_t *h, uint32_t minutes)
{
    size_t want = minutes * 60u / AC_HIST_FINE_BUCKET_S;
    if (want < 2) want = 2;
    if (want > AC_HIST_FINE_N) want = AC_HIST_FINE_N;

    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    size_t n = 0;
    for (size_t i = 0; i < want; i++) {
        size_t idx = (h->fine_head + AC_HIST_FINE_N - want + i) % AC_HIST_FINE_N;
        int16_t v = h->fine[idx].pm25_mean;
        if (v == AC_HIST_NODATA || h->fine[idx].n == 0) continue;
        double x = (double)i * (double)AC_HIST_FINE_BUCKET_S / 3600.0;  /* hours */
        double y = (double)v / 10.0;
        sx += x; sy += y; sxx += x * x; sxy += x * y; n++;
    }
    if (n < 2) return 0.0f;
    double d = (double)n * sxx - sx * sx;
    if (fabs(d) < 1e-9) return 0.0f;
    return (float)(((double)n * sxy - sx * sy) / d);
}

size_t ac_history_bytes(void) { return sizeof(ac_history_t); }

static void bucket_vals(const ac_hist_bucket_t *b, ac_channel_t ch,
                        float *mean, float *peak)
{
    *mean = *peak = -1.0f;
    switch (ch) {
    case AC_CH_PM25:
        if (b->pm25_mean != AC_HIST_NODATA) *mean = b->pm25_mean / 10.0f;
        if (b->pm25_max  != AC_HIST_NODATA) *peak = b->pm25_max / 10.0f;
        break;
    case AC_CH_PM10:
        if (b->pm10_mean != AC_HIST_NODATA) *mean = *peak = b->pm10_mean / 10.0f;
        break;
    case AC_CH_CO2:
        if (b->co2_mean != AC_HIST_NODATA) *mean = (float)b->co2_mean;
        if (b->co2_max  != AC_HIST_NODATA) *peak = (float)b->co2_max;
        break;
    case AC_CH_VOC:
        if (b->voc_mean != AC_HIST_NODATA) *mean = (float)b->voc_mean;
        if (b->voc_max  != AC_HIST_NODATA) *peak = (float)b->voc_max;
        break;
    }
}

ac_window_stat_t ac_history_window(const ac_history_t *h, ac_channel_t ch,
                                   uint32_t seconds)
{
    ac_window_stat_t r = { 0.0f, 0.0f, false };
    size_t want = seconds / AC_HIST_FINE_BUCKET_S;
    if (want > AC_HIST_FINE_N) want = AC_HIST_FINE_N;

    double sum = 0.0;
    uint32_t n = 0;
    float peak = -1.0f;
    for (size_t i = 0; i < want; i++) {
        size_t idx = (h->fine_head + AC_HIST_FINE_N - 1 - i) % AC_HIST_FINE_N;
        const ac_hist_bucket_t *b = &h->fine[idx];
        if (b->n == 0) continue;
        float m, p;
        bucket_vals(b, ch, &m, &p);
        if (m >= 0.0f) { sum += m; n++; }
        if (p > peak) peak = p;
    }

    /* the bucket in progress */
    const ac_hist_acc_t *a = &h->acc_fine;
    switch (ch) {
    case AC_CH_PM25:
        if (a->n_pm) { sum += a->pm25 / a->n_pm; n++;
                       if (a->pm25_max > peak) peak = a->pm25_max; }
        break;
    case AC_CH_PM10:
        if (a->n_pm) { float m = (float)(a->pm10 / a->n_pm); sum += m; n++;
                       if (m > peak) peak = m; }
        break;
    case AC_CH_CO2:
        if (a->n_co2) { sum += a->co2 / a->n_co2; n++;
                        if (a->co2_max > peak) peak = a->co2_max; }
        break;
    case AC_CH_VOC:
        if (a->n_voc) { sum += a->voc / a->n_voc; n++;
                        if (a->voc_max > peak) peak = a->voc_max; }
        break;
    }

    if (n == 0) return r;
    r.mean = (float)(sum / n);
    r.peak = peak >= 0.0f ? peak : r.mean;
    r.valid = true;
    return r;
}
