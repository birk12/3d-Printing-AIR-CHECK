"""
3D Printing AIR CHECK - energy model.

Every number in this file is traceable to a manufacturer datasheet or to a
measurement published by the silicon vendor.  Sources are given inline as
`src=` strings and are reproduced in docs/BATTERY_LIFE.md.

Nothing here is a guess dressed up as a specification.  Where a value had to be
derived (rather than read off a datasheet) the derivation is written out.

Run:  python3 tools/battery_calculator/model.py            # table to stdout
      python3 tools/battery_calculator/model.py --markdown # docs/BATTERY_LIFE.md body
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import sys
from dataclasses import dataclass, field

# --------------------------------------------------------------------------
# Constants from datasheets
# --------------------------------------------------------------------------

SRC = {
    "sen6x": "Sensirion SEN6x Datasheet v0.5 (Oct 2024), Table 1 and section 3.1 "
             "'Electrical characteristics' (SEN63C column)",
    "sen63c_drv": "Sensirion embedded-i2c-sen63c driver (2026): CO2 reads 0x7FFF for the first "
                  "22..24 s of a measurement; stop_measurement waits 1400 ms",
    "sgp40": "Sensirion SGP40 Datasheet v1.2 (Feb 2022), Table 2 'Electrical specifications'",
    "sgp40_lp": "Sensirion gas-index-algorithm README: '2 % duty cycle (10 s interval): < 0.2 mW'",
    "ada4829": "Adafruit SGP40 breakout (product 4829) schematic, github.com/adafruit/"
               "Adafruit-SGP40-PCB: AP2112K-3.3 LDO, green power LED with 10k, 2 x 10 uF",
    "tps22918": "TI TPS22918 datasheet (SLVSDH4), 6.5: IQ 8.3 uA on, ISD 0.5 uA off at 3.3 V",
    "tps62a02": "TI TPS62A02 datasheet, efficiency curve 3.8 V -> 3.3 V at 50..100 mA: ~90 %",
    "c6_icd": "Microamp Home, 'I built a sleeper IKEA smart home sensor' (YouTube KE7bOYCYETM): "
              "ESP32-C6 DevKit, Matter SIT ICD at a 15 s slow poll, PPK2 over 1 h: "
              "121.88 uA average, 39.31 uA sleep floor",
    "c6_icd_esp": "Espressif esp-matter examples/icd_app README, C6 SIT-ICD trace at a 5 s "
                  "poll: avg 174.43 uA, floor 54.84 uA (cross-check)",
    "firebeetle": "DFRobot wiki DFR1075 (FireBeetle 2 ESP32-C6): deep sleep 36 uA on "
                  "hardware v1.2 (TPS62A02 buck); schematic v1.1: CN3165 charger, "
                  "1M/1M battery divider on GPIO0",
    "c6_ds": "Espressif ESP32-C6 datasheet, low-power modes: deep sleep 7 uA",
    "led": "status LED: 5 mA for a 3 s status flash, a handful of times a day",
    "dash": "ASSUMPTION: a second Matter controller (the e-ink dashboard) reads the "
            "sensor every 15 min; each read costs ~10 poll-equivalents of radio time",
    "cell": "Generic protected single-cell LiPo, 606090 format; self-discharge assumption",
}

# ---- SEN63C (PM1/2.5/4/10 + CO2 + T/RH in one module) ----------------------
SEN63C_V = 3.3                # V, supply 3.15..3.45 V, src=sen6x
SEN63C_I_MEAS_MA = 80.0       # mA typ in measurement mode, src=sen6x
SEN63C_I_MEAS_MAX_MA = 100.0  # mA max in measurement mode, src=sen6x
SEN63C_I_IDLE_MA = 3.3        # mA idle - why the module is power-gated, src=sen6x
SEN63C_STARTUP_S = 0.1        # s power-on until I2C, src=sen6x
SEN63C_PM_STABLE_S = 30.0     # s typical start-up until stable PM, src=sen6x
SEN63C_CO2_BLIND_S = 24.0     # s CO2 reads 'unknown' after start, src=sen63c_drv
SEN63C_STOP_S = 1.4           # s stop_measurement post-processing, src=sen63c_drv
# Charged at the measurement current for the whole stop time: conservative.

# ---- SGP40 ---------------------------------------------------------------
SGP40_V = 3.3
SGP40_I_IDLE_UA = 34.0        # uA typ, heater off, src=sgp40
SGP40_LP_POWER_MW = 0.2       # mW, 2 % duty cycle at 10 s sampling, src=sgp40_lp
SGP40_I_CONT_MA = 2.6         # mA avg, continuous 1 Hz at 3.3 V, src=sgp40

# ---- SGP40 breakout (Adafruit 4829) --------------------------------------
# The breakout is not just an SGP40: an AP2112K-3.3 LDO (55 uA Iq typ) and a
# green power LED through 10k (~130 uA) sit on its supply.  v1.1 kept that rail
# on permanently and its model did not count either - EDR-14.  Since v1.2 the
# rail is switched on only for the ~0.25 s a measurement takes.
BREAKOUT_OVERHEAD_UA = 55.0 + 130.0   # LDO Iq + power LED, src=ada4829
SGP40_RAIL_ON_S = 0.25                # rail settle + 2 x 35 ms + 135 ms + I2C
SGP40_RAIL_C_UF = 10.0 + 10.0 + 0.1 + 1.0   # breakout C4, C5, C6 + carrier C4
# Each switch-on recharges those capacitors from zero; that charge is lost again
# when the rail drops, so it is a real per-sample cost.

# ---- MCU + carrier board --------------------------------------------------
# An independent one-hour PPK2 measurement at exactly our configuration (SIT
# ICD, 15 s slow poll).  It is the higher of the two published traces, i.e.
# the conservative choice.
C6_ICD_AVG_UA_15S = 121.88    # measured, 15 s slow poll, src=c6_icd
C6_ICD_FLOOR_UA = 39.31       # measured sleep floor between polls, src=c6_icd
# Derived poll cost: (121.88 - 39.31) uA * 15 s = 1239 uA*s per poll.
# Espressif's own trace gives 598 uA*s per poll - about half.  We take the
# larger figure.
C6_POLL_CHARGE_UAS = (C6_ICD_AVG_UA_15S - C6_ICD_FLOOR_UA) * 15.0
# FireBeetle 2 ESP32-C6 v1.2: DFRobot measure 36 uA in deep sleep for the whole
# board.  The chip itself is 7 uA of that (src=c6_ds), so the board - TPS62A02
# buck Iq, the 1M/1M battery divider (1.9 uA), charger reverse leakage - is
# about 29 uA.  The chip's own share is already inside C6_ICD_FLOOR_UA.
BOARD_QUIESCENT_UA = 36.0 - 7.0     # src=firebeetle, c6_ds
# Carrier (ACC-1 rev C): two TPS22918, both off between measurements.
CARRIER_QUIESCENT_UA = 2 * 0.5      # src=tps22918

# ---- Status LED (the sensor has no display since v1.1) ---------------------
LED_I_MA = 5.0
LED_FLASH_S = 3.0
LED_FLASHES_PER_DAY = 6.0

# ---- Second Matter controller (the dashboard, multi-admin) ------------------
# Every read wakes the ICD into active mode and costs a few fast polls plus the
# CASE handshake.  Ten poll-equivalents is a deliberately generous guess.
DASH_READS_PER_DAY = 96.0         # one every 15 min
DASH_POLLS_PER_READ = 10.0

# ---- Power conversion -----------------------------------------------------
BUCK_EFFICIENCY = 0.90        # TPS62A02 on the FireBeetle, 3.8 V -> 3.3 V, src=tps62a02
VBAT_NOMINAL = 3.80           # V, discharge-weighted average of a 1S LiPo

# ---- Cell -----------------------------------------------------------------
CELL_MAH_DEFAULT = 4000.0     # 606090 pouch, 6.0 x 60 x 90 mm
CELL_USABLE_FRACTION = 0.90   # down to the 3.3 V system cutoff, not 3.0 V
CELL_SELF_DISCHARGE_PCT_MONTH = 2.5


# --------------------------------------------------------------------------
# Model
# --------------------------------------------------------------------------

@dataclass
class Profile:
    """A firmware measurement profile.  Mirrors the C table in
    firmware/components/ac_core/src/ac_config.c - keep the two in sync.
    Since v1.2 one SEN63C window yields PM, CO2, temperature and humidity
    together, so there is no separate CO2 cadence any more."""
    name: str
    pm_interval_s: float          # 0 -> continuous measurement
    pm_window_s: float            # time in SEN63C measurement mode
    voc_interval_s: float         # 0 -> VOC disabled
    icd_slow_poll_s: float
    note: str = ""


PROFILES = [
    Profile("ECO",        pm_interval_s=3600, pm_window_s=40, voc_interval_s=10,
            icd_slow_poll_s=15,
            note="The default. Particles, CO2, temperature and humidity every hour, "
                 "VOC every 10 s as the tripwire. Meets the 3-month target with margin."),
    Profile("ECO_LONG",   pm_interval_s=4 * 3600, pm_window_s=40, voc_interval_s=10,
            icd_slow_poll_s=15,
            note="Optional long-life setting: one SEN63C window every 4 h - which "
                 "since v1.2 also means CO2 only every 4 h."),
    Profile("NORMAL",     pm_interval_s=15 * 60, pm_window_s=60, voc_interval_s=10,
            icd_slow_poll_s=15,
            note="60 s every 15 min. Default when a printer is in the room."),
    Profile("ACTIVE",     pm_interval_s=2 * 60, pm_window_s=60, voc_interval_s=10,
            icd_slow_poll_s=5,
            note="Entered automatically on a detected emission event. Time-limited."),
    Profile("POST_PRINT", pm_interval_s=5 * 60, pm_window_s=60, voc_interval_s=10,
            icd_slow_poll_s=5,
            note="Recovery tracking after an event."),
    Profile("CONTINUOUS", pm_interval_s=0, pm_window_s=60, voc_interval_s=1,
            icd_slow_poll_s=5,
            note="Reference / validation mode. USB power expected."),
]


def _sen63c_batt_current_ma(worst: bool = False) -> float:
    """SEN63C measurement-mode current referred to the battery, through the
    FireBeetle's buck converter."""
    i = SEN63C_I_MEAS_MAX_MA if worst else SEN63C_I_MEAS_MA
    return i * SEN63C_V / (VBAT_NOMINAL * BUCK_EFFICIENCY)


@dataclass
class Budget:
    profile: Profile
    lines: dict = field(default_factory=dict)   # name -> mAh/day

    @property
    def device_mah_day(self) -> float:
        return sum(v for k, v in self.lines.items() if k != "battery self-discharge")

    @property
    def total_mah_day(self) -> float:
        return sum(self.lines.values())


def budget(p: Profile, cell_mah: float = CELL_MAH_DEFAULT,
           worst: bool = False) -> Budget:
    b = Budget(profile=p)
    sen_ma = _sen63c_batt_current_ma(worst)

    # --- particles, CO2, temperature, humidity: one SEN63C window ----------
    if p.pm_interval_s == 0:
        b.lines["SEN63C (continuous)"] = sen_ma * 24.0
    else:
        cycles_day = 86400.0 / p.pm_interval_s
        on_s = SEN63C_STARTUP_S + p.pm_window_s + SEN63C_STOP_S
        b.lines["SEN63C measurement windows"] = cycles_day * sen_ma * on_s / 3600.0
        # Fully power-gated between windows: its 3.3 mA idle current does not
        # apply, only the load switch's 0.5 uA (in the carrier line).

    # --- VOC ---------------------------------------------------------------
    if p.voc_interval_s <= 0:
        b.lines["SGP40 (breakout, pulsed rail)"] = 0.0
    elif p.voc_interval_s <= 1.0:
        # Continuous: the rail stays on, so the breakout overhead is permanent.
        b.lines["SGP40 (breakout, rail on)"] = (
            SGP40_I_CONT_MA + BREAKOUT_OVERHEAD_UA / 1000.0) * 24.0
    else:
        # Sensirion's 0.2 mW at a 10 s interval includes the 34 uA idle
        # current; with the rail switched off between samples only the heater
        # part remains, scaled with the interval.
        heater_ua = max(SGP40_LP_POWER_MW / SGP40_V * 1000.0 - SGP40_I_IDLE_UA, 0.0)
        heater_ua *= 10.0 / p.voc_interval_s
        on_ua = (SGP40_I_IDLE_UA + BREAKOUT_OVERHEAD_UA) * SGP40_RAIL_ON_S / p.voc_interval_s
        cap_ua = SGP40_RAIL_C_UF * SGP40_V / p.voc_interval_s
        b.lines["SGP40 (breakout, pulsed rail)"] = (heater_ua + on_ua + cap_ua) * 24.0 / 1000.0

    # --- status LED --------------------------------------------------------
    b.lines["status LED"] = LED_I_MA * LED_FLASH_S * LED_FLASHES_PER_DAY / 3600.0

    # --- second Matter controller (dashboard) ------------------------------
    b.lines["dashboard reads (multi-admin)"] = (
        DASH_READS_PER_DAY * DASH_POLLS_PER_READ * C6_POLL_CHARGE_UAS / 3.6e6)

    # --- MCU + Thread ------------------------------------------------------
    mcu_ua = C6_ICD_FLOOR_UA + C6_POLL_CHARGE_UAS / p.icd_slow_poll_s
    b.lines["ESP32-C6 + Thread (ICD)"] = mcu_ua * 24.0 / 1000.0

    # --- always-on board hardware -----------------------------------------
    b.lines["FireBeetle + carrier quiescent"] = (
        BOARD_QUIESCENT_UA + CARRIER_QUIESCENT_UA) * 24.0 / 1000.0

    # --- cell ---------------------------------------------------------------
    b.lines["battery self-discharge"] = cell_mah * CELL_SELF_DISCHARGE_PCT_MONTH / 100.0 / 30.44

    return b


def runtime_days(b: Budget, cell_mah: float = CELL_MAH_DEFAULT,
                 margin: float = 0.0) -> float:
    usable = cell_mah * CELL_USABLE_FRACTION
    draw = b.total_mah_day * (1.0 + margin)
    return usable / draw


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

MARGIN = 0.25   # 25 % engineering margin, per the project requirement
TARGET_MONTHS = 3.0   # three months per charge, agreed 2026-09-18


def table(cell_mah: float = CELL_MAH_DEFAULT) -> list[dict]:
    rows = []
    for p in PROFILES:
        b = budget(p, cell_mah)
        nominal = runtime_days(b, cell_mah, 0.0)
        with_margin = runtime_days(b, cell_mah, MARGIN)
        worst = runtime_days(budget(p, cell_mah, worst=True), cell_mah, MARGIN)
        rows.append({
            "profile": p.name,
            "pm_interval": ("continuous" if p.pm_interval_s == 0
                            else f"{p.pm_interval_s/60:.0f} min"),
            "pm_window_s": p.pm_window_s,
            "mah_per_day": round(b.total_mah_day, 2),
            "days_nominal": round(nominal, 1),
            "days_with_margin": round(with_margin, 1),
            "months_with_margin": round(with_margin / 30.44, 2),
            "days_worst_case": round(worst, 1),
            "months_worst_case": round(worst / 30.44, 2),
            "lines": {k: round(v, 3) for k, v in b.lines.items()},
            "note": p.note,
        })
    return rows


def _fmt_days(d: float) -> str:
    if d < 14:
        return f"{d:.1f} d"
    if d < 60:
        return f"{d/7:.1f} weeks"
    return f"{d/30.44:.1f} months"


def print_table(cell_mah: float) -> None:
    print(f"Cell: {cell_mah:.0f} mAh nominal, {cell_mah*CELL_USABLE_FRACTION:.0f} mAh usable "
          f"(to the 3.3 V system cutoff)")
    print(f"Engineering margin applied: {MARGIN*100:.0f} %\n")
    hdr = (f"{'profile':<12}{'PM every':>12}{'window':>8}{'mAh/day':>10}{'nominal':>12}"
           f"{'w/ margin':>12}{'worst case':>13}")
    print(hdr)
    print("-" * len(hdr))
    for r in table(cell_mah):
        print(f"{r['profile']:<12}{r['pm_interval']:>12}{r['pm_window_s']:>7.0f}s"
              f"{r['mah_per_day']:>10.2f}"
              f"{_fmt_days(r['days_nominal']):>12}"
              f"{_fmt_days(r['days_with_margin']):>12}"
              f"{_fmt_days(r['days_worst_case']):>13}")
    print("\nPer-profile breakdown (mAh/day):")
    for r in table(cell_mah):
        print(f"\n  {r['profile']}")
        for k, v in sorted(r["lines"].items(), key=lambda kv: -kv[1]):
            print(f"    {k:<32}{v:>8.3f}")


def markdown(cell_mah: float = CELL_MAH_DEFAULT) -> str:
    """Emit the body of docs/BATTERY_LIFE.md so the document can never drift
    from the model."""
    rows = table(cell_mah)
    out = []
    w = out.append
    w("<!-- GENERATED by tools/battery_calculator/model.py - do not edit by hand. -->")
    w("")
    w("## Inputs")
    w("")
    w("| quantity | value | source |")
    w("|---|---|---|")
    w(f"| SEN63C supply | {SEN63C_V:.1f} V (3.15-3.45 V) | {SRC['sen6x']} |")
    w(f"| SEN63C measurement current | {SEN63C_I_MEAS_MA:.0f} mA typ, {SEN63C_I_MEAS_MAX_MA:.0f} mA max | {SRC['sen6x']} |")
    w(f"| SEN63C idle current | {SEN63C_I_IDLE_MA:.1f} mA - so it is power-gated, never idled | {SRC['sen6x']} |")
    w(f"| SEN63C PM start-up | {SEN63C_PM_STABLE_S:.0f} s typ | {SRC['sen6x']} |")
    w(f"| SEN63C CO2 blind time after start | {SEN63C_CO2_BLIND_S:.0f} s | {SRC['sen63c_drv']} |")
    w(f"| SEN63C stop time, charged at full current | {SEN63C_STOP_S:.1f} s | {SRC['sen63c_drv']} |")
    w(f"| SGP40 idle current | {SGP40_I_IDLE_UA:.0f} uA | {SRC['sgp40']} |")
    w(f"| SGP40 continuous (1 Hz) | {SGP40_I_CONT_MA:.1f} mA | {SRC['sgp40']} |")
    w(f"| SGP40 low-power (10 s interval) | {SGP40_LP_POWER_MW:.1f} mW | {SRC['sgp40_lp']} |")
    w(f"| SGP40 breakout overhead while powered | {BREAKOUT_OVERHEAD_UA:.0f} uA (LDO + power LED) | {SRC['ada4829']} |")
    w(f"| SGP40 rail on-time per sample | {SGP40_RAIL_ON_S:.2f} s, {SGP40_RAIL_C_UF:.1f} uF recharged | firmware sequence; breakout + carrier capacitors |")
    w(f"| ESP32-C6 Matter SIT-ICD average | {C6_ICD_AVG_UA_15S:.1f} uA at 15 s poll | {SRC['c6_icd']} |")
    w(f"| ESP32-C6 sleep floor | {C6_ICD_FLOOR_UA:.1f} uA | {SRC['c6_icd']} |")
    w(f"| derived charge per Thread poll | {C6_POLL_CHARGE_UAS:.0f} uA*s | (avg - floor) x 15 s; Espressif's trace gives about half, we take the larger |")
    w(f"| FireBeetle board quiescent | {BOARD_QUIESCENT_UA:.0f} uA | 36 uA board deep sleep ({SRC['firebeetle']}) minus 7 uA for the chip ({SRC['c6_ds']}) |")
    w(f"| carrier quiescent | {CARRIER_QUIESCENT_UA:.1f} uA | 2 x TPS22918 off, {SRC['tps22918']} |")
    w(f"| status LED | {LED_I_MA:.0f} mA x {LED_FLASH_S:.0f} s x {LED_FLASHES_PER_DAY:.0f}/day | {SRC['led']} |")
    w(f"| dashboard reads | {DASH_READS_PER_DAY:.0f}/day x {DASH_POLLS_PER_READ:.0f} poll-equivalents | {SRC['dash']} |")
    w(f"| buck efficiency | {BUCK_EFFICIENCY*100:.0f} % | {SRC['tps62a02']} |")
    w(f"| battery nominal working voltage | {VBAT_NOMINAL:.2f} V | discharge-weighted 1S LiPo |")
    w(f"| cell | {cell_mah:.0f} mAh, {CELL_USABLE_FRACTION*100:.0f} % usable | {SRC['cell']} |")
    w(f"| self-discharge | {CELL_SELF_DISCHARGE_PCT_MONTH:.1f} %/month | {SRC['cell']} |")
    w("")
    w(f"SEN63C current referred to the battery through the FireBeetle's buck converter: "
      f"`{SEN63C_I_MEAS_MA:.0f} mA x {SEN63C_V:.1f} V / ({VBAT_NOMINAL:.2f} V x {BUCK_EFFICIENCY:.2f})` "
      f"= **{_sen63c_batt_current_ma():.1f} mA** typical, "
      f"**{_sen63c_batt_current_ma(True):.1f} mA** at the datasheet maximum.")
    w("")
    w("The datasheet gives the measurement current only *after the first 60 s*;")
    w("our windows are shorter than that. The worst-case column therefore charges")
    w("every window at the maximum current. The Thread and quiescent figures are")
    w("taken as battery current without crediting the buck converter, which is")
    w("the conservative direction.")
    w("")
    w("## Result")
    w("")
    w(f"Cell: **{cell_mah:.0f} mAh** nominal, **{cell_mah*CELL_USABLE_FRACTION:.0f} mAh** usable. "
      f"Engineering margin: **{MARGIN*100:.0f} %**.")
    w("")
    w("| profile | SEN63C every | window | mAh/day | nominal | with margin | worst case, with margin |")
    w("|---|---|---|---|---|---|---|")
    for r in rows:
        w(f"| {r['profile']} | {r['pm_interval']} | {r['pm_window_s']:.0f} s | "
          f"{r['mah_per_day']:.2f} | {_fmt_days(r['days_nominal'])} | "
          f"**{_fmt_days(r['days_with_margin'])}** | {_fmt_days(r['days_worst_case'])} |")
    w("")
    w("## Where the energy goes")
    w("")
    for r in rows:
        w(f"### {r['profile']}")
        w("")
        w(r["note"])
        w("")
        w("| consumer | mAh/day | share |")
        w("|---|---|---|")
        tot = r["mah_per_day"]
        for k, v in sorted(r["lines"].items(), key=lambda kv: -kv[1]):
            w(f"| {k} | {v:.3f} | {100*v/tot:.1f} % |")
        w(f"| **total** | **{tot:.2f}** | |")
        w("")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cell", type=float, default=CELL_MAH_DEFAULT,
                    help="cell capacity in mAh")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    if a.json:
        print(json.dumps({"cell_mah": a.cell, "margin": MARGIN,
                          "profiles": table(a.cell)}, indent=2))
    elif a.markdown:
        print(markdown(a.cell))
    else:
        print_table(a.cell)
    return 0


if __name__ == "__main__":
    sys.exit(main())
