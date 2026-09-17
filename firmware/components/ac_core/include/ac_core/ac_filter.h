/* Signal conditioning.
 *
 * Design constraint from the brief: filter hard enough to stop the device
 * chattering on noise, but never so hard that a real printer emission event
 * is smoothed away.  The EMA time constants are therefore short relative to
 * the event we are looking for (minutes) and long relative to the sampling
 * jitter (seconds), and every consumer sees the raw value as well. */
#ifndef AC_FILTER_H
#define AC_FILTER_H

#include "ac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float value;        /* current filtered value */
    float tau_s;        /* time constant */
    ac_time_ms_t last;
    bool primed;
} ac_ema_t;

void  ac_ema_init(ac_ema_t *f, float tau_s);
/* Time-aware EMA: correct even when samples arrive at an irregular cadence,
 * which they do here because the mode changes the interval. */
float ac_ema_update(ac_ema_t *f, float x, ac_time_ms_t t);
void  ac_ema_reset(ac_ema_t *f);

#define AC_RATE_HIST 8

typedef struct {
    float v[AC_RATE_HIST];
    ac_time_ms_t t[AC_RATE_HIST];
    uint8_t n, head;
} ac_rate_t;

void  ac_rate_init(ac_rate_t *r);
void  ac_rate_push(ac_rate_t *r, float x, ac_time_ms_t t);
/* Least-squares slope in units per minute.  Returns 0 with fewer than two
 * points or when they all share a timestamp. */
float ac_rate_per_min(const ac_rate_t *r);

typedef struct {
    float mean, m2;
    uint32_t n;
} ac_stats_t;

void  ac_stats_init(ac_stats_t *s);
void  ac_stats_push(ac_stats_t *s, float x);
float ac_stats_mean(const ac_stats_t *s);
float ac_stats_sd(const ac_stats_t *s);
/* True when x is further than k standard deviations from the running mean and
 * we have enough samples for that to mean anything. */
bool  ac_stats_is_spike(const ac_stats_t *s, float x, float k);

#ifdef __cplusplus
}
#endif
#endif
