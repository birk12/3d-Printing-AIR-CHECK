#include "ac_core/ac_battery.h"

#include <stddef.h>

/* Per-cell resting voltage against state of charge.  These are generic curves
 * read off manufacturer discharge graphs at light load (Energizer E91 and L91
 * datasheets, Panasonic eneloop pro).  They are good enough to say "a quarter
 * left" and "nearly empty"; they are not a fuel gauge. */
typedef struct { float v, pct; } pt_t;

static const pt_t k_alk[] = {
    { 0.90f, 0 }, { 1.00f, 5 }, { 1.10f, 15 }, { 1.15f, 25 }, { 1.20f, 40 },
    { 1.25f, 55 }, { 1.30f, 70 }, { 1.35f, 80 }, { 1.40f, 88 }, { 1.45f, 94 },
    { 1.55f, 100 },
};
static const pt_t k_nimh[] = {
    { 1.00f, 0 }, { 1.10f, 5 }, { 1.15f, 10 }, { 1.18f, 20 }, { 1.20f, 35 },
    { 1.22f, 50 }, { 1.24f, 65 }, { 1.26f, 80 }, { 1.28f, 90 }, { 1.32f, 97 },
    { 1.40f, 100 },
};
/* Flat for most of its life: the energy counter does the work there. */
static const pt_t k_li[] = {
    { 0.90f, 0 }, { 1.20f, 3 }, { 1.30f, 6 }, { 1.38f, 12 }, { 1.42f, 25 },
    { 1.45f, 45 }, { 1.47f, 65 }, { 1.50f, 85 }, { 1.55f, 95 }, { 1.70f, 100 },
};

#define N(a) (sizeof(a) / sizeof((a)[0]))

static float lookup(const pt_t *t, size_t n, float v)
{
    if (!(v > t[0].v)) return 0.0f;                  /* also catches NaN */
    if (v >= t[n - 1].v) return 100.0f;
    for (size_t i = 1; i < n; i++)
        if (v < t[i].v) {
            float f = (v - t[i - 1].v) / (t[i].v - t[i - 1].v);
            return t[i - 1].pct + f * (t[i].pct - t[i - 1].pct);
        }
    return 100.0f;
}

const char *ac_cell_type_name(ac_cell_type_t t)
{
    switch (t) {
    case AC_CELL_ALKALINE: return "alkaline";
    case AC_CELL_NIMH:     return "nimh";
    case AC_CELL_LITHIUM:  return "lithium";
    default:               return "?";
    }
}

float ac_cell_soc(ac_cell_type_t t, float v)
{
    switch (t) {
    case AC_CELL_NIMH:    return lookup(k_nimh, N(k_nimh), v);
    case AC_CELL_LITHIUM: return lookup(k_li, N(k_li), v);
    default:              return lookup(k_alk, N(k_alk), v);
    }
}

float ac_cell_energy_mwh(ac_cell_type_t t)
{
    /* Mirrors tools/battery_calculator/model.py CELL_WH. */
    switch (t) {
    case AC_CELL_NIMH:    return 2900.0f;   /* eneloop pro 2500 mAh x 1.2 V */
    case AC_CELL_LITHIUM: return 5000.0f;   /* L91, ~3500 mAh x ~1.45 V */
    default:              return 3000.0f;   /* E91 at 25 mA to 0.8 V */
    }
}

void ac_pack_init(ac_pack_t *p, ac_cell_type_t type, uint8_t cells)
{
    p->type = type < AC_CELL_COUNT ? type : AC_CELL_ALKALINE;
    p->cells = cells ? cells : 6;
    p->used_mwh = 0.0f;
    p->last_cell_v = -1.0f;
    p->pct = -1.0f;
}

void ac_pack_spend(ac_pack_t *p, float mwh)
{
    if (mwh > 0.0f) p->used_mwh += mwh;
}

float ac_pack_update(ac_pack_t *p, float pack_v)
{
    float cell_v = pack_v / (float)p->cells;
    if (!(cell_v > 0.3f)) return p->pct;             /* no reading */

    bool fresh = p->last_cell_v > 0.0f &&
                 cell_v >= p->last_cell_v + AC_BATT_SWAP_V_PER_CELL;
    if (fresh) p->used_mwh = 0.0f;
    p->last_cell_v = cell_v;

    float by_v = ac_cell_soc(p->type, cell_v);
    float cap = ac_cell_energy_mwh(p->type) * (float)p->cells;
    float by_e = 100.0f * (1.0f - p->used_mwh / cap);
    if (by_e < 0.0f) by_e = 0.0f;
    float now = by_v < by_e ? by_v : by_e;

    if (p->pct < 0.0f || fresh || now < p->pct) p->pct = now;
    return p->pct;
}
