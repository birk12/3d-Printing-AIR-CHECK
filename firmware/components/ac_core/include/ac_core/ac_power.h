/* Power source for AIR CHECK v1.4: the LFP module C of the Power-Standard.
 *
 *   USB-C socket -> Adafruit #6091 (TI BQ25185, LFP 3.65 V, 1 A) <-> 1S4P
 *   LiFePO4 (4 x AER18650m2A2) -> LOAD -> Pololu S9V11E2A 3.90 V -> LM66200
 *   -> FireBeetle battery input.
 *
 * The module reports over the compact interface PWR-K (one ADC node carries
 * EXT, CHG_N and FLT_N), plus the cell voltage / 2 on its own ADC pin and CE
 * on a GPIO.  pwr_std (the Power-Standard's own C99 module, copied unchanged
 * into components/pwr_std) turns that into Matter's view and decides the
 * charge pause on permanent USB.  This file adds what is specific to AIR
 * CHECK:
 *
 *  - PWR-K cannot tell a latched fault (both STAT pins low: the 6 h safety
 *    timer) from a recoverable one (STAT1 only: NTC hot/cold, OVP).  A 1S4P
 *    pack of 6.8-7.2 Ah takes longer than 6 h at 1 A, so the timer is an
 *    expected event here.  A deliberate heuristic, approved by the battery
 *    session: a fault that appears after at least AC_TIMER_SUSPECT_S of time
 *    with CHG_N "charging" in the current USB session is taken for the timer,
 *    and CE is pulsed once per session, only below 3.40 V
 *    (pwr_timer_retry).  A recoverable fault that gets pulsed by mistake is
 *    harmless: the charger stays paused by its own NTC or OVP logic.
 *  - "External power" for the measurement engine also includes a computer on
 *    the FireBeetle's own USB-C, which does not reach the power module.
 *
 * Plain C, no ESP-IDF: covered by the host tests. */
#ifndef AC_POWER_H
#define AC_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "pwr_std.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AC_TIMER_SUSPECT_S   (330u * 60u)   /* 5.5 h of charging, the timer is 6 h */
#define AC_CE_PULSE_MS       150u           /* >= 100 ms, per the battery session */

typedef enum {
    AC_CE_RELEASE = 0,      /* GPIO as input: charging allowed (fail-safe)  */
    AC_CE_HOLD,             /* drive high: charge pause on permanent USB    */
    AC_CE_PULSE,            /* high for AC_CE_PULSE_MS, then release        */
} ac_ce_t;

typedef struct {
    pwr_ctx_t pwr;
    uint32_t  chg_accum_s;  /* seconds with CHG_N "charging" in this USB session */
    uint32_t  last_s;       /* time of the previous update, 0: none          */
    bool      was_charging; /* the previous update saw "charging"            */
} ac_power_t;

typedef struct {
    pwr_state_t st;         /* pwr_std's view: source, charge, level, fault */
    bool        ext;        /* external power for the engine (module or PC) */
    bool        charging;   /* the module is charging the cells right now   */
    ac_ce_t     ce;
} ac_power_out_t;

/* Transmit power for 802.15.4, in dBm.
 *
 * ESP-IDF does not pick a modest default: it takes the maximum of the chip's
 * power table, +20 dBm, which the ESP32-C6 datasheet (v1.5 Table 5-9) puts
 * at a 305 mA peak - and nothing in a build says so out loud. The Sleeper
 * Frame found that while chasing its own rail.
 *
 * The burst matters here because the regulator draws it from the cells as
 * constant power: the emptier the pack, the more current, and the BQ25185
 * disconnects the battery once its BAT pin stays below BUVLO (3.0 V typical
 * and with no min/max in SLUSF65B) for 60 us. So the radio asks for full
 * power only while the cells have headroom, and backs off to +12 dBm
 * (187 mA) once the level leaves OK. The level comes from pwr_std and brings
 * its hysteresis with it, so this does not chatter around a threshold.
 * design.py computes the resulting BAT-pin margin. */
#define AC_TX_DBM_FULL  20
#define AC_TX_DBM_LOW   12
int8_t ac_power_tx_dbm(const ac_power_out_t *o);

void ac_power_init(ac_power_t *p);

/* Before the radio starts: true when the device runs on the cells and they
 * are at or below the critical level (plus hysteresis) - go straight back to
 * deep sleep instead of loading them again (boot loop -> deep discharge). */
bool ac_power_boot_should_sleep(float vbat, const float ladder_v[2]);

/* vbat: cell volts (<0: no reading); ladder_v: two PWR-K readings ~50 ms
 * apart (STAT2 toggles without a cell); usb_host: a computer enumerated the
 * FireBeetle's USB; now_s: monotonic seconds. */
ac_power_out_t ac_power_update(ac_power_t *p, float vbat, const float ladder_v[2],
                               bool usb_host, uint32_t now_s);

#ifdef __cplusplus
}
#endif
#endif
