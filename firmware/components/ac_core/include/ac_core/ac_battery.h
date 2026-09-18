/* Battery state from the cell voltage.
 *
 * Since v1.2 the board (DFRobot FireBeetle 2 ESP32-C6) has no fuel gauge, only
 * a 1M/1M divider from the cell to GPIO0.  A LiPo's resting voltage says
 * where it is on its discharge curve, but the middle of that curve is flat:
 * expect an error of roughly +-10 % state of charge there, less near full and
 * near empty.  The two things the firmware actually decides on - "low" at
 * 20 % and "critical" at 5 % - sit on the steep end, where the voltage is a
 * good guide.
 *
 * Plain C, no ESP-IDF, so it is covered by the host tests.
 */
#ifndef AC_BATTERY_H
#define AC_BATTERY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The charger (CN3165) terminates at 4.2 V; above this we call the cell full
 * while USB is connected. */
#define AC_BATT_FULL_V        4.15f
/* A reading this far above the tracked value is a different (charged) cell,
 * not noise. */
#define AC_BATT_SWAP_PCT      15.0f

typedef enum {
    AC_CHG_UNKNOWN = 0,
    AC_CHG_CHARGING,
    AC_CHG_FULL,
    AC_CHG_DISCHARGING,
} ac_charge_state_t;

/* Resting cell voltage -> state of charge, 0..100 %.  A generic 1S LiPo curve,
 * linearly interpolated; values outside the table are clamped. */
float ac_battery_soc(float volts);

ac_charge_state_t ac_battery_charge_state(float volts, bool usb_present);

/* Reported percentage that never climbs while on battery.  Temperature and
 * load move the voltage by tens of millivolts, which is several percent in
 * the flat middle of the curve; a gauge that goes up and down again looks
 * broken.  So on battery the value may only fall, unless it jumps by
 * AC_BATT_SWAP_PCT or more (a freshly charged cell was fitted). */
typedef struct {
    float pct;
    bool  valid;
} ac_batt_track_t;

void  ac_batt_track_init(ac_batt_track_t *t);
float ac_batt_track_update(ac_batt_track_t *t, float volts, bool usb_present);

#ifdef __cplusplus
}
#endif
#endif
