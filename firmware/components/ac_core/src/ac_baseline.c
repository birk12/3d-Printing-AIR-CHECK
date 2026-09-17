#include "ac_core/ac_baseline.h"
#include <math.h>
#include <string.h>

/* An EMA with weight alpha per update has a time constant of
 * update_interval / alpha.  Working in tau keeps ac_ema_update correct when
 * the sampling cadence changes with the mode. */
static float tau_from(uint32_t update_s, float alpha)
{
    if (alpha <= 0.0f) alpha = 0.02f;
    if (alpha > 1.0f)  alpha = 1.0f;
    return (float)update_s / alpha;
}

void ac_baseline_init(ac_baseline_t *b, uint32_t update_s, float alpha)
{
    memset(b, 0, sizeof(*b));
    b->update_s = update_s ? update_s : 900;
    float tau = tau_from(b->update_s, alpha);
    ac_ema_init(&b->e_pm25, tau);
    ac_ema_init(&b->e_pm10, tau);
    ac_ema_init(&b->e_pm1, tau);
    ac_ema_init(&b->e_co2, tau);
    ac_ema_init(&b->e_voc, tau);
    b->s.magic = AC_BASELINE_MAGIC;
    b->s.valid = false;
}

bool ac_baseline_restore(ac_baseline_t *b, const ac_baseline_store_t *st)
{
    if (!st || st->magic != AC_BASELINE_MAGIC || !st->valid) return false;
    if (!(st->pm25 >= 0.0f && st->pm25 < 2000.0f)) return false;
    if (!(st->co2 >= 0.0f && st->co2 < 40000.0f)) return false;
    if (st->voc < 0 || st->voc > 500) return false;
    b->s = *st;
    b->e_pm25.value = st->pm25; b->e_pm25.primed = true;
    b->e_pm10.value = st->pm10; b->e_pm10.primed = true;
    b->e_pm1.value  = st->pm1;  b->e_pm1.primed  = true;
    b->e_co2.value  = st->co2;  b->e_co2.primed  = true;
    b->e_voc.value  = (float)st->voc; b->e_voc.primed = true;
    return true;
}

const ac_baseline_store_t *ac_baseline_snapshot(const ac_baseline_t *b)
{
    return &b->s;
}

static void sync_store(ac_baseline_t *b)
{
    b->s.pm25 = b->e_pm25.value;
    b->s.pm10 = b->e_pm10.value;
    b->s.pm1  = b->e_pm1.value;
    b->s.co2  = b->e_co2.value;
    b->s.voc  = (int32_t)lrintf(b->e_voc.value);
    b->s.valid = b->e_pm25.primed || b->e_voc.primed;
}

void ac_baseline_update(ac_baseline_t *b, const ac_sample_t *s)
{
    if (b->frozen) return;
    if (b->last_update != 0 &&
        s->t < b->last_update + (ac_time_ms_t)b->update_s * 1000u) {
        /* honour the configured cadence; the PM channel is much slower than
         * the VOC channel and we do not want the fast one to dominate */
        if (!s->voc_fresh) return;
    }
    if (s->pm_fresh) {
        ac_ema_update(&b->e_pm25, s->pm25, s->t);
        ac_ema_update(&b->e_pm10, s->pm10, s->t);
        ac_ema_update(&b->e_pm1,  s->pm1,  s->t);
    }
    if (s->voc_fresh && s->voc_index >= 0)
        ac_ema_update(&b->e_voc, (float)s->voc_index, s->t);
    if (s->co2_fresh && s->co2 > 0.0f)
        ac_ema_update(&b->e_co2, s->co2, s->t);
    b->last_update = s->t;
    b->s.samples++;
    sync_store(b);
}

void ac_baseline_freeze(ac_baseline_t *b, bool frozen) { b->frozen = frozen; }
bool ac_baseline_frozen(const ac_baseline_t *b) { return b->frozen; }

void ac_baseline_reset(ac_baseline_t *b, const ac_sample_t *now)
{
    ac_ema_reset(&b->e_pm25); ac_ema_reset(&b->e_pm10);
    ac_ema_reset(&b->e_pm1);  ac_ema_reset(&b->e_co2);
    ac_ema_reset(&b->e_voc);
    b->s.samples = 0;
    b->s.valid = false;
    b->frozen = false;
    b->last_update = 0;
    if (now) {
        if (now->pm_fresh) {
            ac_ema_update(&b->e_pm25, now->pm25, now->t);
            ac_ema_update(&b->e_pm10, now->pm10, now->t);
            ac_ema_update(&b->e_pm1,  now->pm1,  now->t);
        }
        if (now->voc_fresh && now->voc_index >= 0)
            ac_ema_update(&b->e_voc, (float)now->voc_index, now->t);
        if (now->co2_fresh && now->co2 > 0.0f)
            ac_ema_update(&b->e_co2, now->co2, now->t);
        b->last_update = now->t;
        b->s.samples = 1;
        sync_store(b);
    }
}

bool  ac_baseline_valid(const ac_baseline_t *b) { return b->s.valid; }
float ac_baseline_pm25(const ac_baseline_t *b) { return b->e_pm25.primed ? b->e_pm25.value : 0.0f; }
float ac_baseline_pm10(const ac_baseline_t *b) { return b->e_pm10.primed ? b->e_pm10.value : 0.0f; }
float ac_baseline_co2 (const ac_baseline_t *b) { return b->e_co2.primed  ? b->e_co2.value  : 0.0f; }
int32_t ac_baseline_voc(const ac_baseline_t *b)
{
    return b->e_voc.primed ? (int32_t)lrintf(b->e_voc.value) : 0;
}

float ac_baseline_d_pm25(const ac_baseline_t *b, const ac_sample_t *s)
{
    if (!b->e_pm25.primed) return 0.0f;
    return s->pm25 - b->e_pm25.value;
}

int32_t ac_baseline_d_voc(const ac_baseline_t *b, const ac_sample_t *s)
{
    if (!b->e_voc.primed || s->voc_index < 0) return 0;
    return s->voc_index - (int32_t)lrintf(b->e_voc.value);
}

float ac_baseline_d_co2(const ac_baseline_t *b, const ac_sample_t *s)
{
    if (!b->e_co2.primed) return 0.0f;
    return s->co2 - b->e_co2.value;
}
