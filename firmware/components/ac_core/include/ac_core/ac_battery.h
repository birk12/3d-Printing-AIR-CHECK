/* Battery state for a pack of AA cells (v1.3).
 *
 * The device runs from six AA cells in series, of whatever chemistry the user
 * puts in: alkaline, NiMH, or lithium primary (Energizer L91 class).  Nothing
 * is ever charged inside the device.  Two estimates, and the lower one wins:
 *
 *  - Voltage: the pack voltage divided by the cell count, looked up in a
 *    resting-voltage curve for the configured chemistry.  Good for alkaline,
 *    usable for NiMH, and nearly useless for lithium primaries until the very
 *    end, because their curve is flat.
 *  - Energy: what the firmware has spent since the pack was fitted, from the
 *    same per-action figures as tools/battery_calculator/model.py, against the
 *    usable energy of that chemistry.  This is what carries the percentage
 *    through the flat part of a lithium pack.
 *
 * A fresh pack is recognised by its voltage jumping up; the energy counter
 * then starts again.
 *
 * Plain C, no ESP-IDF, so it is covered by the host tests.
 */
#ifndef AC_BATTERY_H
#define AC_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AC_CELL_ALKALINE = 0,
    AC_CELL_NIMH,
    AC_CELL_LITHIUM,        /* 1.5 V lithium primary, e.g. Energizer L91 */
    AC_CELL_COUNT,
} ac_cell_type_t;

/* Matter Power Source BatChargeState, same order: 0 Unknown, 1 IsCharging,
 * 2 IsAtFullCharge, 3 IsNotCharging.  An AA pack is never charged here. */
typedef enum {
    AC_CHG_UNKNOWN = 0,
    AC_CHG_CHARGING,
    AC_CHG_FULL,
    AC_CHG_DISCHARGING,
} ac_charge_state_t;

/* A pack this much higher (per cell) than the last reading is a new pack. */
#define AC_BATT_SWAP_V_PER_CELL  0.08f

const char *ac_cell_type_name(ac_cell_type_t t);

/* Resting voltage of one cell -> state of charge, 0..100 %.  Linear
 * interpolation in a generic curve; values outside are clamped. */
float ac_cell_soc(ac_cell_type_t t, float cell_volts);

/* Usable energy of one fresh cell at this device's load (a few mA with
 * 100-200 mA pulses), in mWh.  See docs/BATTERY_LIFE.md for the sources. */
float ac_cell_energy_mwh(ac_cell_type_t t);

typedef struct {
    ac_cell_type_t type;
    uint8_t  cells;
    float    used_mwh;       /* spent since this pack was fitted */
    float    last_cell_v;    /* per-cell voltage at the last reading, <0 none */
    float    pct;            /* last reported value, <0 none */
} ac_pack_t;

void  ac_pack_init(ac_pack_t *p, ac_cell_type_t type, uint8_t cells);
/* Book energy taken from the pack (already including conversion losses). */
void  ac_pack_spend(ac_pack_t *p, float mwh);
/* Feed a pack voltage reading; returns the percentage to report.  The value
 * never climbs back up except when a new pack is detected. */
float ac_pack_update(ac_pack_t *p, float pack_volts);

/* ---- where the power comes from (v1.3.1) -----------------------------
 *
 * Two inputs share the FireBeetle's battery input (VSYS) through the LM66200
 * ideal diode: the AA pack through a regulator set to 3.90 V, and a USB-C
 * power socket through a second regulator set to 4.20 V.  The higher one
 * wins.  With a computer on the FireBeetle's own USB-C its charger holds VSYS
 * at 4.2 V as well.  So VSYS alone tells the two apart:
 *
 *   battery branch   3.82 .. 3.98 V  (3.90 V +-2 %)
 *   mains branch     4.17 .. 4.24 V  (4.20 V trimmed, or the CN3165's 4.2 V +-1 %)
 *
 * AC_EXT_POWER_V sits in the middle, more than the ADC's calibrated error
 * (~40 mV at VSYS) away from both. */
#define AC_EXT_POWER_V    4.08f
/* Below this the holder is empty (six cells at 0.5 V would be long dead). */
#define AC_PACK_ABSENT_V  3.0f

typedef enum {
    AC_SRC_BATTERY = 0,      /* running on the cells                          */
    AC_SRC_MAINS,            /* external power, cells present as the backup  */
    AC_SRC_MAINS_NO_CELLS,   /* external power, holder empty                  */
} ac_power_src_t;

/* usb_host: a computer has enumerated the USB port - external power too,
 * whatever VSYS reads.  vsys_v <= 0: no reading. */
ac_power_src_t ac_power_classify(float pack_v, float vsys_v, bool usb_host);

/* Matter Power Source Status for the battery source: 1 Active, 2 Standby
 * (present but not in use), 3 Unavailable. */
uint8_t ac_power_matter_status(ac_power_src_t s);

#ifdef __cplusplus
}
#endif
#endif
