#include "ac_core/ac_filter.h"
#include <math.h>
#include <string.h>

void ac_ema_init(ac_ema_t *f, float tau_s)
{
    memset(f, 0, sizeof(*f));
    f->tau_s = tau_s > 0.0f ? tau_s : 1.0f;
}

void ac_ema_reset(ac_ema_t *f)
{
    f->primed = false;
    f->value = 0.0f;
    f->last = 0;
}

float ac_ema_update(ac_ema_t *f, float x, ac_time_ms_t t)
{
    if (!f->primed) {
        f->value = x;
        f->last = t;
        f->primed = true;
        return f->value;
    }
    float dt = (t > f->last) ? (float)(t - f->last) / 1000.0f : 0.0f;
    f->last = t;
    /* alpha = 1 - exp(-dt/tau): the discrete equivalent of a first-order lag,
     * so the filter behaves the same whether samples arrive every 10 s or
     * every 4 h. */
    float a = 1.0f - expf(-dt / f->tau_s);
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    f->value += a * (x - f->value);
    return f->value;
}

void ac_rate_init(ac_rate_t *r) { memset(r, 0, sizeof(*r)); }

void ac_rate_push(ac_rate_t *r, float x, ac_time_ms_t t)
{
    r->v[r->head] = x;
    r->t[r->head] = t;
    r->head = (uint8_t)((r->head + 1) % AC_RATE_HIST);
    if (r->n < AC_RATE_HIST) r->n++;
}

float ac_rate_per_min(const ac_rate_t *r)
{
    if (r->n < 2) return 0.0f;
    /* index 0 of the window is the oldest retained sample */
    uint8_t first = (uint8_t)((r->head + AC_RATE_HIST - r->n) % AC_RATE_HIST);
    double t0 = (double)r->t[first];
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (uint8_t i = 0; i < r->n; i++) {
        uint8_t k = (uint8_t)((first + i) % AC_RATE_HIST);
        double x = ((double)r->t[k] - t0) / 60000.0;   /* minutes */
        double y = r->v[k];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double d = (double)r->n * sxx - sx * sx;
    if (fabs(d) < 1e-9) return 0.0f;
    return (float)(((double)r->n * sxy - sx * sy) / d);
}

void ac_stats_init(ac_stats_t *s) { memset(s, 0, sizeof(*s)); }

void ac_stats_push(ac_stats_t *s, float x)
{
    s->n++;
    float d = x - s->mean;
    s->mean += d / (float)s->n;
    s->m2 += d * (x - s->mean);
}

float ac_stats_mean(const ac_stats_t *s) { return s->n ? s->mean : 0.0f; }

float ac_stats_sd(const ac_stats_t *s)
{
    if (s->n < 2) return 0.0f;
    return sqrtf(s->m2 / (float)(s->n - 1));
}

bool ac_stats_is_spike(const ac_stats_t *s, float x, float k)
{
    if (s->n < 8) return false;
    float sd = ac_stats_sd(s);
    if (sd <= 0.0f) return false;
    return fabsf(x - s->mean) > k * sd;
}
