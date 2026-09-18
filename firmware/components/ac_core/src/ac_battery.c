#include "ac_core/ac_battery.h"

/* Resting voltage of a generic single-cell LiPo against state of charge.
 * It is not from a cell datasheet - the 606090 cells this device uses do not
 * publish one - and it is only as good as "generic".  See ac_battery.h. */
static const struct { float v, pct; } k_ocv[] = {
    { 3.27f,   0.0f }, { 3.61f,   5.0f }, { 3.69f,  10.0f }, { 3.71f,  15.0f },
    { 3.73f,  20.0f }, { 3.75f,  25.0f }, { 3.77f,  30.0f }, { 3.79f,  35.0f },
    { 3.80f,  40.0f }, { 3.82f,  45.0f }, { 3.84f,  50.0f }, { 3.85f,  55.0f },
    { 3.87f,  60.0f }, { 3.91f,  65.0f }, { 3.95f,  70.0f }, { 3.98f,  75.0f },
    { 4.02f,  80.0f }, { 4.08f,  85.0f }, { 4.11f,  90.0f }, { 4.15f,  95.0f },
    { 4.20f, 100.0f },
};
#define N_OCV (sizeof(k_ocv) / sizeof(k_ocv[0]))

float ac_battery_soc(float v)
{
    if (!(v > k_ocv[0].v)) return 0.0f;             /* also catches NaN */
    if (v >= k_ocv[N_OCV - 1].v) return 100.0f;
    for (unsigned i = 1; i < N_OCV; i++) {
        if (v < k_ocv[i].v) {
            float f = (v - k_ocv[i - 1].v) / (k_ocv[i].v - k_ocv[i - 1].v);
            return k_ocv[i - 1].pct + f * (k_ocv[i].pct - k_ocv[i - 1].pct);
        }
    }
    return 100.0f;
}

ac_charge_state_t ac_battery_charge_state(float v, bool usb)
{
    if (!(v > 0.5f)) return AC_CHG_UNKNOWN;
    if (!usb) return AC_CHG_DISCHARGING;
    return v >= AC_BATT_FULL_V ? AC_CHG_FULL : AC_CHG_CHARGING;
}

void ac_batt_track_init(ac_batt_track_t *t)
{
    t->pct = -1.0f;
    t->valid = false;
}

float ac_batt_track_update(ac_batt_track_t *t, float v, bool usb)
{
    float now = ac_battery_soc(v);
    if (!t->valid || usb || now < t->pct || now >= t->pct + AC_BATT_SWAP_PCT) {
        t->pct = now;
        t->valid = true;
    }
    return t->pct;
}
