/* pwr_std – firmware side of the LFP power modules A/B/C (Power-Standard v1.0).
 *
 * The module (Adafruit #6091 / TI BQ25185, 1S LiFePO4) reports on the PWR-7
 * connector:
 *   VBAT_S  cell voltage / 2            (ADC, ADC_ATTEN_DB_6)
 *   EXT     USB power present           (digital, high = present)
 *   CHG_N   STAT2, low = charging       (open drain, GPIO pull-up on)
 *   FLT_N   STAT1, low = fault          (open drain, GPIO pull-up on)
 *   CE      output: high = pause charging, input/high-Z = charging allowed
 *
 * This file turns those raw readings into what Matter's Power Source cluster
 * wants, and decides when to pause charging on permanent USB (module C).
 * Plain C99, no ESP-IDF, so it builds and tests on the host.
 */
#ifndef PWR_STD_H
#define PWR_STD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- LFP thresholds (cell volts, measured at rest or light load) ------
 * Sources: TI SLUAAR1 OCV curve, BQ25185 datasheet SLUSF65B. */
#define PWR_V_ABSENT    1.00f  /* below: no cell (module B, or holder empty) */
#define PWR_V_FULL      3.40f  /* at or above after charge end: full         */
#define PWR_V_NEWPACK   3.35f  /* jump from below WARN to above: fresh cells */
#define PWR_V_RECHARGE  3.30f  /* charge-hold released below this            */
#define PWR_V_WARN      3.20f  /* ~10 % left: "battery low"                  */
#define PWR_V_CRIT      3.10f  /* ~6 % left: stop radio, deep sleep          */
#define PWR_V_HW_CUTOFF 3.00f  /* BQ25185 BUVLO switches VSYS off (info)     */
#define PWR_V_HYST      0.05f  /* level hysteresis                           */

#define PWR_HOLD_MAX_S  (30u * 24u * 3600u)  /* re-charge at least monthly  */

/* Raw readings, one sample.  Read CHG_N/FLT_N twice ~50 ms apart and pass
 * both: STAT2 toggles when the charger has no cell. */
typedef struct {
    float vbat;        /* cell volts (= 2 * VBAT_S), <0 if not read          */
    bool  ext;         /* EXT pin high                                        */
    bool  chg_low[2];  /* CHG_N low, two samples                              */
    bool  flt_low[2];  /* FLT_N low, two samples                              */
} pwr_raw_t;

/* Matter Power Source BatChargeState, same numbers. */
typedef enum {
    PWR_CHG_UNKNOWN = 0,
    PWR_CHG_CHARGING = 1,
    PWR_CHG_FULL = 2,
    PWR_CHG_NOT_CHARGING = 3,
} pwr_chg_t;

/* Matter Power Source BatChargeLevel, same numbers. */
typedef enum {
    PWR_LVL_OK = 0,
    PWR_LVL_WARNING = 1,
    PWR_LVL_CRITICAL = 2,
} pwr_lvl_t;

typedef enum {
    PWR_SRC_BATTERY = 0,       /* running on the cell                    */
    PWR_SRC_EXTERNAL,          /* USB, cell present (module C / A+svc)   */
    PWR_SRC_EXTERNAL_NO_CELL,  /* USB, no cell (module B)                */
} pwr_src_t;

typedef enum {
    PWR_FLT_NONE = 0,
    PWR_FLT_RECOVERABLE,       /* STAT1 low, STAT2 high: TS hot/cold, OVP */
    PWR_FLT_LATCHED,           /* both low: safety timer, ISET, BATOCP    */
} pwr_fault_t;

typedef struct {
    pwr_src_t   src;
    pwr_chg_t   chg;
    pwr_lvl_t   lvl;
    pwr_fault_t fault;
    uint8_t     pct;           /* coarse, 0..100, from the OCV table      */
    bool        new_pack;      /* cells were swapped/recharged externally */
} pwr_state_t;

/* Persistent between calls (keep in RTC memory across deep sleep). */
typedef struct {
    pwr_lvl_t lvl;             /* for hysteresis                          */
    float     last_vbat;       /* <0: none                                */
    bool      was_charging;    /* seen CHARGING since USB came            */
    bool      hold;            /* charge pause active (CE high)           */
    uint32_t  hold_since_s;
    bool      retried;         /* safety-timer retry used this USB session */
} pwr_ctx_t;

void pwr_init(pwr_ctx_t *c);

/* Evaluate one set of readings. */
pwr_state_t pwr_update(pwr_ctx_t *c, const pwr_raw_t *r);

/* Module C on permanent USB: returns true when CE must be driven high
 * (pause charging).  now_s: monotonic seconds (RTC).  Fail-safe: the caller
 * releases CE (input / high-Z) whenever this returns false. */
bool pwr_hold_update(pwr_ctx_t *c, const pwr_state_t *s, float vbat, uint32_t now_s);

/* Large packs (capacity / charge current > ~5 h, e.g. 1S4P at 1 A): the
 * BQ25185's fixed 6 h safety timer can expire before the pack is full.
 * Returns true exactly once per USB session when the caller should pulse CE
 * high for >= 100 ms (clears the latched timer fault, TI SLUSF65B 6.3.7.7):
 * USB present, latched fault, VBAT < PWR_V_FULL.  A second latched fault in
 * the same session stays latched and must be reported. */
bool pwr_timer_retry(pwr_ctx_t *c, const pwr_state_t *s, float vbat);

/* Coarse state of charge from a resting LFP voltage (TI SLUAAR1). Flat
 * between 3.26 and 3.33 V: trust it only for "full" and "almost empty". */
uint8_t pwr_lfp_pct(float vbat);

/* ---- compact interface PWR-K (devices with one free ADC pin) ---------
 * EXT, CHG_N and FLT_N share one node: VBUS -100k- node -150k- GND,
 * CHG_N via BAT43 + 150k, FLT_N via BAT43 + 33k.  ADC_ATTEN_DB_12.
 * Worst case over VBUS 4.75..5.25 V, BAT43 Vf 0.15..0.35 V AND the STAT pins'
 * own VOL (TI SLUSF65B: up to 0.4 V) - the earlier numbers left VOL out:
 *   no USB 0 V | fault 1.02..1.60 V | charging 2.09..2.46 V | idle 2.85..3.15 V
 * Thresholds sit in the middle of the gaps, each >= 180 mV from a band edge,
 * which covers the C6 ADC's +-40 mV at 12 dB.
 * Recoverable and latched faults overlap there; both decode as a fault. */
#define PWR_K_USB_MIN   0.60f
#define PWR_K_CHG_MIN   1.85f
#define PWR_K_IDLE_MIN  2.65f

typedef struct { bool ext; bool chg_low; bool flt_low; } pwr_k_t;
pwr_k_t pwr_k_decode(float pin_volts);

/* ---- one-point calibration of the VBAT_S divider ---------------------
 * The ADC dominates the error (+-23 mV at the pin = +-46 mV at the cell,
 * more than the 1 % divider contributes), so better resistors only fix half
 * of it.  Measure the cell with a multimeter once, hand both values to
 * pwr_cal_factor(), store the result wherever the project keeps settings
 * (NVS, file, ...) and multiply every later reading with it.  pwr_std itself
 * stays free of storage.
 *
 * Outside 0.95..1.05 the deviation no longer comes from the ADC (+-46 mV at
 * the cell) or from 1 % resistors: it means a wrong or badly soldered
 * divider.  The function then returns 1.0 - uncalibrated - instead of
 * clamping, because a half-corrected reading hides the fault. */
#define PWR_CAL_MIN 0.95f
#define PWR_CAL_MAX 1.05f

typedef enum {
    PWR_CAL_OK = 0,        /* factor usable                                   */
    PWR_CAL_NO_CELL,       /* measured or reference implausible (< 1.0 V)     */
    PWR_CAL_OUT_OF_RANGE,  /* > 5 % off: check the divider, do not calibrate  */
} pwr_cal_status_t;

/* reference: multimeter at the cell, measured: what the HAL read.  Returns
 * the factor, or 1.0 with a status != OK.  'st' may be NULL. */
float pwr_cal_factor(float measured_v, float reference_v, pwr_cal_status_t *st);

static inline float pwr_cal_apply(float v, float k)
{
    return (k >= PWR_CAL_MIN && k <= PWR_CAL_MAX) ? v * k : v;
}

/* Matter BatPercentRemaining is in half percent. */
static inline uint8_t pwr_matter_pct(uint8_t pct) { return (uint8_t)(pct * 2u); }

#ifdef __cplusplus
}
#endif
#endif
