"""
3D Printing AIR CHECK - energy model (v1.3, 6 x AA).

Every number in this file is traceable to a manufacturer datasheet or to a
measurement published by the silicon vendor.  Sources are given inline as
`src=` strings and are reproduced in docs/BATTERY_LIFE.md.  Where a value is an
assumption, its source string says ASSUMPTION.

Since v1.3 the device runs from six AA cells in series, so the model works in
energy (mWh at the cells), not in mAh of a 3.8 V cell.  Power path:

    6 x AA -> fuse -> Pololu S9V11E2A (buck-boost, 3.90 V) -> LM66200 ideal
    diode -> FireBeetle battery input (VSYS) -> TPS62A02 buck -> 3.3 V

3.3 V loads (ESP32-C6, SEN62, SGP40, SHT40) pass both converters; the Sunrise
and the status LED sit directly on VSYS.  Since v1.3.1 a USB-C power socket
feeds the LM66200's second input at 4.20 V; on external power the cells only
lose the regulator's quiescent current and the resistors across them.

Run:  python3 tools/battery_calculator/model.py                  # table
      python3 tools/battery_calculator/model.py --markdown       # BATTERY_LIFE.md body
      python3 tools/battery_calculator/model.py --json           # CI
      python3 tools/battery_calculator/model.py --cell nimh      # another chemistry
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field

# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------

SRC = {
    "sen6x": "Sensirion SEN6x Datasheet v0.92 (Dec 2025), Table 11 (SEN62 column)",
    "sen6x_start": "Sensirion SEN6x Datasheet v0.92, Table 1: 100 ms to I2C, "
                   "typ. 30 s until stable PM; stop_measurement 1400 ms",
    "sunrise": "Senseair TDE7318 (Sunrise 006-0-0008), p.9: 1.60 mC per "
               "32-sample measurement incl. read/write-back",
    "sgp40": "Sensirion SGP40 Datasheet v1.2 (Feb 2022), Table 2: idle 34 uA",
    "sgp40_lp": "Sensirion gas-index-algorithm README: '2 % duty cycle (10 s "
                "interval): < 0.2 mW'",
    "sht40": "Sensirion SHT4x Datasheet v6.6: 320 uA for 8.3 ms (high repeatability), "
             "idle 0.08 uA",
    "sparkfun": "SparkFun SGP40 breakout (SEN-18345) schematic: no regulator; power "
                "LED on a cuttable jumper - cut in this build",
    "grove": "Seeed Grove SHT40 (101021032): 1.2 uA idle per Seeed; no LED; a small "
             "regulator and level shifters are likely (photo) and counted in that figure",
    "c6_icd": "Microamp Home, 'I built a sleeper IKEA smart home sensor' (YouTube "
              "KE7bOYCYETM): ESP32-C6, Matter SIT ICD at a 15 s slow poll, PPK2 "
              "over 1 h: 121.88 uA average, 39.31 uA sleep floor",
    "firebeetle": "DFRobot wiki DFR1075 (FireBeetle 2 ESP32-C6): 36 uA board deep "
                  "sleep (hardware v1.2); minus 7 uA for the chip (ESP32-C6 datasheet)",
    "pololu": "Pololu S9V11E2A (#5719) product page: quiescent current < 0.2 mA for "
              "most input/output combinations; typical efficiency 85-95 %",
    "tps62a02": "TI TPS62A02 datasheet, efficiency 3.8 V -> 3.3 V at 50..100 mA: ~90 %",
    "divider": "pack divider 1 M / 220 k (ADC on GPIO3), permanently across the pack",
    "uvlo": "undervoltage lockout: the regulator's internal 100 k EN pull-up in series "
            "with R11 13 k, permanently across the pack (Pololu: 10 uA/V on VIN)",
    "led": "status LED: 5 mA from VSYS (3.9 V) for a 3 s flash, 6 times a day",
    "dash": "ASSUMPTION: a second Matter controller (the e-ink dashboard) reads the "
            "sensor every 15 min; each read costs ~10 poll-equivalents of radio time",
    "l91": "Energizer L91 datasheet: 3500 mAh rated, ~1.45 V average at light "
           "drain (-> ~5.0 Wh); 20-year storage life (~0.1 %/month)",
    "eneloop_pro": "Panasonic eneloop pro BK-3HCDE: 2500 mAh min x 1.2 V (-> 2.9 Wh); "
                   "85 % after 1 year (~1.25 %/month)",
    "alkaline": "Energizer E91 datasheet: ~3000 mWh at 25 mA to 0.8 V; 10-year "
                "storage (~0.2 %/month); to the 1.0 V/cell lockout ~80 % of that",
    "li15": "ASSUMPTION: 1.5 V Li-ion AA with USB-C: independent tests find "
            "2.3-2.8 Wh against 3000-3500 mWh on the label; their internal buck "
            "converter adds its own quiescent loss (not modelled)",
}

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

# ---- conversion -----------------------------------------------------------
EFF_REG = 0.85        # Pololu S9V11E2A, bottom of the 85-95 % band, src=pololu
EFF_REG_WORST = 0.80  # light-load margin: the product page gives no curve
EFF_BUCK = 0.90       # TPS62A02 on the FireBeetle, src=tps62a02
EFF_3V3 = EFF_REG * EFF_BUCK
REG_IQ_MA = 0.2       # at the regulator input, src=pololu
DIVIDER_OHM = 1_000_000 + 220_000   # src=divider
UVLO_OHM = 100_000 + 13_000         # src=uvlo

# ---- SEN62 (PM only, power-gated between windows) -------------------------
SEN62_V = 3.3
SEN62_I_MA = 75.0         # typ, measurement mode, src=sen6x
SEN62_I_MAX_MA = 90.0     # max, src=sen6x
SEN62_STARTUP_S = 0.1     # src=sen6x_start
SEN62_STOP_S = 1.4        # src=sen6x_start; charged at full current

# ---- Sunrise 006-0-0008 (CO2, single measurement, EN low in between) -----
SUNRISE_V = 3.9           # VBB on VSYS
SUNRISE_MC = 1.60         # per measurement, src=sunrise
SUNRISE_SLEEP_UA = 0.2    # VBB with EN low, src=sunrise (TDE7318 table)

# ---- SGP40 (always powered, low-power sequence) --------------------------
SGP40_V = 3.3
SGP40_LP_MW = 0.2         # at a 10 s interval, idle included, src=sgp40_lp
SGP40_I_CONT_MA = 2.6     # continuous 1 Hz, src=sgp40

# ---- SHT40 ---------------------------------------------------------------
SHT40_UA_S = 320.0 * 0.0083   # per measurement, src=sht40
SHT40_IDLE_UA = 0.08

# ---- MCU + Thread ----------------------------------------------------------
C6_ICD_AVG_UA_15S = 121.88
C6_ICD_FLOOR_UA = 39.31
C6_POLL_CHARGE_UAS = (C6_ICD_AVG_UA_15S - C6_ICD_FLOOR_UA) * 15.0
BOARD_QUIESCENT_UA = 36.0 - 7.0   # src=firebeetle

# ---- LED, dashboard ----------------------------------------------------------
LED_I_MA = 5.0
LED_FLASH_S = 3.0
LED_FLASHES_PER_DAY = 6.0
DASH_READS_PER_DAY = 96.0
DASH_POLLS_PER_READ = 10.0


@dataclass
class Cell:
    key: str
    name: str
    mwh: float                  # per cell, nominal
    usable: float               # fraction above the 1.0 V/cell lockout
    v_avg: float                # average cell voltage under this load
    self_discharge_pct_month: float
    src: str
    recommended: bool = True


CELLS = {
    "lithium": Cell("lithium", "Energizer Ultimate Lithium L91", 5000.0, 0.95, 1.45,
                    0.1, "l91"),
    "nimh": Cell("nimh", "Panasonic eneloop pro (NiMH)", 2900.0, 0.95, 1.20,
                 1.25, "eneloop_pro"),
    "alkaline": Cell("alkaline", "Alkaline (Energizer E91 class)", 3000.0, 0.80, 1.25,
                     0.2, "alkaline"),
    "li15": Cell("li15", "1.5 V Li-ion AA with USB-C", 2500.0, 0.95, 1.50,
                 2.0, "li15", recommended=False),
}
CELL_COUNT = 6
DEFAULT_CELL = "lithium"


# --------------------------------------------------------------------------
# Model
# --------------------------------------------------------------------------

@dataclass
class Profile:
    """Mirrors the C table in firmware/components/ac_core/src/ac_config.c -
    keep the two in sync."""
    name: str
    pm_interval_s: float          # 0 -> continuous
    pm_window_s: float
    voc_interval_s: float         # SHT40 + SGP40
    co2_interval_s: float
    icd_slow_poll_s: float
    note: str = ""


PROFILES = [
    Profile("ECO", 3600, 60, 10, 300, 15,
            note="The default. Particles every hour (60 s window, the first 30 s "
                 "discarded), CO2 every 5 min, temperature, humidity and VOC every "
                 "10 s. A rising VOC index switches to ACTIVE."),
    Profile("NORMAL", 15 * 60, 60, 10, 300, 15,
            note="Particles every 15 min. For a room where printing is routine."),
    Profile("ACTIVE", 2 * 60, 60, 10, 120, 5,
            note="Entered automatically on a detected emission event. Time-limited."),
    Profile("POST_PRINT", 5 * 60, 60, 10, 300, 5,
            note="Recovery tracking after an event."),
    Profile("CONTINUOUS", 0, 60, 1, 60, 5,
            note="Reference / validation mode. USB power expected."),
]


@dataclass
class Budget:
    profile: Profile
    cell: Cell
    lines: dict = field(default_factory=dict)   # name -> mWh/day at the cells

    @property
    def total_mwh_day(self) -> float:
        return sum(self.lines.values())


def budget(p: Profile, cell: Cell, worst: bool = False) -> Budget:
    b = Budget(profile=p, cell=cell)
    eff_reg = EFF_REG_WORST if worst else EFF_REG
    eff_3v3 = eff_reg * EFF_BUCK
    v_pack = cell.v_avg * CELL_COUNT

    def at3v3(uw: float) -> float:          # uW at 3.3 V -> mWh/day at the cells
        return uw * 24.0 / 1000.0 / eff_3v3

    def at4v0(uw: float) -> float:
        return uw * 24.0 / 1000.0 / eff_reg

    # --- particles --------------------------------------------------------
    i = SEN62_I_MAX_MA if worst else SEN62_I_MA
    if p.pm_interval_s == 0:
        b.lines["SEN62 (continuous)"] = at3v3(i * SEN62_V * 1000.0)
    else:
        on_s = SEN62_STARTUP_S + p.pm_window_s + SEN62_STOP_S
        b.lines["SEN62 measurement windows"] = at3v3(
            i * SEN62_V * 1000.0 * on_s / p.pm_interval_s)

    # --- CO2 --------------------------------------------------------------
    b.lines["Sunrise CO2"] = at4v0(
        (SUNRISE_MC * 1000.0 / p.co2_interval_s + SUNRISE_SLEEP_UA) * SUNRISE_V)

    # --- VOC + T/RH --------------------------------------------------------
    if p.voc_interval_s <= 1.0:
        b.lines["SGP40"] = at3v3(SGP40_I_CONT_MA * SGP40_V * 1000.0)
    else:
        b.lines["SGP40"] = at3v3(SGP40_LP_MW * 1000.0 * 10.0 / p.voc_interval_s)
    b.lines["SHT40"] = at3v3((SHT40_UA_S / p.voc_interval_s + SHT40_IDLE_UA) * 3.3)

    # --- MCU, Thread, board ----------------------------------------------
    mcu_ua = C6_ICD_FLOOR_UA + C6_POLL_CHARGE_UAS / p.icd_slow_poll_s
    b.lines["ESP32-C6 + Thread (ICD)"] = at3v3(mcu_ua * 3.3)
    b.lines["dashboard reads (multi-admin)"] = at3v3(
        DASH_READS_PER_DAY * DASH_POLLS_PER_READ * C6_POLL_CHARGE_UAS / 86400.0 * 3.3)
    b.lines["FireBeetle quiescent"] = at3v3(BOARD_QUIESCENT_UA * 3.3)
    b.lines["status LED"] = at4v0(
        LED_I_MA * 1000.0 * 3.9 * LED_FLASH_S * LED_FLASHES_PER_DAY / 86400.0)

    # --- power path -------------------------------------------------------
    b.lines["Pololu regulator quiescent"] = REG_IQ_MA * v_pack * 24.0
    b.lines["pack divider + undervoltage lockout"] = (
        v_pack ** 2 * (1.0 / DIVIDER_OHM + 1.0 / UVLO_OHM) * 1000.0 * 24.0)

    # --- cells ------------------------------------------------------------
    b.lines["cell self-discharge"] = (cell.mwh * CELL_COUNT
                                      * cell.self_discharge_pct_month / 100.0 / 30.44)
    return b


def pack_usable_mwh(cell: Cell) -> float:
    return cell.mwh * CELL_COUNT * cell.usable


def runtime_days(b: Budget, margin: float = 0.0) -> float:
    return pack_usable_mwh(b.cell) / (b.total_mwh_day * (1.0 + margin))


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

MARGIN = 0.25         # 25 % engineering margin, per the project requirement
TARGET_MONTHS = 3.0   # ECO on L91 must clear this; ci/github-actions-ci.yml


def table(cell: Cell) -> list[dict]:
    rows = []
    for p in PROFILES:
        b = budget(p, cell)
        nominal = runtime_days(b)
        with_margin = runtime_days(b, MARGIN)
        worst = runtime_days(budget(p, cell, worst=True), MARGIN)
        rows.append({
            "profile": p.name,
            "pm_interval": ("continuous" if p.pm_interval_s == 0
                            else f"{p.pm_interval_s/60:.0f} min"),
            "co2_interval": f"{p.co2_interval_s/60:g} min",
            "mwh_per_day": round(b.total_mwh_day, 1),
            "days_nominal": round(nominal, 1),
            "days_with_margin": round(with_margin, 1),
            "months_with_margin": round(with_margin / 30.44, 2),
            "days_worst_case": round(worst, 1),
            "months_worst_case": round(worst / 30.44, 2),
            "lines": {k: round(v, 2) for k, v in b.lines.items()},
            "note": p.note,
        })
    return rows


def mains_backup_months(cell: Cell) -> float:
    """How long the cells last as the backup while external power carries the
    device: they only lose the regulator's quiescent current, the resistors
    across them and their own self-discharge."""
    v = cell.v_avg * CELL_COUNT
    mw = REG_IQ_MA * v + v * v * (1.0 / DIVIDER_OHM + 1.0 / UVLO_OHM) * 1000.0
    sd_mw = cell.mwh * CELL_COUNT * cell.self_discharge_pct_month / 100.0 / 30.44 / 24.0
    return pack_usable_mwh(cell) / (mw + sd_mw) / 24.0 / 30.44


def eco_by_cell() -> list[dict]:
    eco = PROFILES[0]
    out = []
    for c in CELLS.values():
        b = budget(eco, c)
        out.append({
            "cell": c.name, "key": c.key, "recommended": c.recommended,
            "pack_wh": round(pack_usable_mwh(c) / 1000.0, 1),
            "months_nominal": round(runtime_days(b) / 30.44, 2),
            "months_with_margin": round(runtime_days(b, MARGIN) / 30.44, 2),
            "months_worst_case": round(
                runtime_days(budget(eco, c, worst=True), MARGIN) / 30.44, 2),
            "months_as_mains_backup": round(mains_backup_months(c), 1),
        })
    return out


def _fmt_days(d: float) -> str:
    if d < 14:
        return f"{d:.1f} d"
    if d < 60:
        return f"{d/7:.1f} weeks"
    return f"{d/30.44:.1f} months"


def print_table(cell: Cell) -> None:
    print(f"Pack: {CELL_COUNT} x {cell.name}, {pack_usable_mwh(cell)/1000:.1f} Wh usable")
    print(f"Engineering margin applied: {MARGIN*100:.0f} %\n")
    hdr = (f"{'profile':<12}{'PM every':>12}{'CO2 every':>11}{'mWh/day':>10}"
           f"{'nominal':>13}{'w/ margin':>13}{'worst case':>13}")
    print(hdr)
    print("-" * len(hdr))
    for r in table(cell):
        print(f"{r['profile']:<12}{r['pm_interval']:>12}{r['co2_interval']:>11}"
              f"{r['mwh_per_day']:>10.1f}{_fmt_days(r['days_nominal']):>13}"
              f"{_fmt_days(r['days_with_margin']):>13}{_fmt_days(r['days_worst_case']):>13}")
    print("\nECO by cell type (with margin):")
    for r in eco_by_cell():
        print(f"  {r['cell']:<34}{r['pack_wh']:>6.1f} Wh {r['months_with_margin']:>6.2f} months"
              f"{'' if r['recommended'] else '   (not recommended)'}")
    print("\nPer-profile breakdown (mWh/day):")
    for r in table(cell):
        print(f"\n  {r['profile']}")
        for k, v in sorted(r["lines"].items(), key=lambda kv: -kv[1]):
            print(f"    {k:<34}{v:>8.2f}")


def markdown(cell: Cell) -> str:
    """The generated part of docs/BATTERY_LIFE.md."""
    rows = table(cell)
    out = []
    w = out.append
    w("<!-- GENERATED by tools/battery_calculator/model.py - do not edit by hand. -->")
    w("")
    w("## Inputs")
    w("")
    w("| quantity | value | source |")
    w("|---|---|---|")
    w(f"| SEN62 measurement current | {SEN62_I_MA:.0f} mA typ, {SEN62_I_MAX_MA:.0f} mA max at {SEN62_V} V | {SRC['sen6x']} |")
    w(f"| SEN62 on-time per window | window + {SEN62_STARTUP_S} s start + {SEN62_STOP_S} s stop | {SRC['sen6x_start']} |")
    w(f"| Sunrise charge per measurement | {SUNRISE_MC:.2f} mC at {SUNRISE_V:.1f} V, {SUNRISE_SLEEP_UA} uA with EN low | {SRC['sunrise']} |")
    w(f"| SGP40 low-power sequence | {SGP40_LP_MW} mW at 10 s | {SRC['sgp40_lp']}; {SRC['sparkfun']} |")
    w(f"| SGP40 continuous | {SGP40_I_CONT_MA} mA | {SRC['sgp40']} |")
    w(f"| SHT40 | {SHT40_UA_S:.2f} uA*s per measurement, {SHT40_IDLE_UA} uA idle | {SRC['sht40']}; {SRC['grove']} |")
    w(f"| ESP32-C6 Matter SIT-ICD | {C6_ICD_AVG_UA_15S} uA avg at 15 s poll, {C6_ICD_FLOOR_UA} uA floor | {SRC['c6_icd']} |")
    w(f"| charge per Thread poll | {C6_POLL_CHARGE_UAS:.0f} uA*s | (avg - floor) x 15 s |")
    w(f"| FireBeetle quiescent | {BOARD_QUIESCENT_UA:.0f} uA | {SRC['firebeetle']} |")
    w(f"| dashboard reads | {DASH_READS_PER_DAY:.0f}/day x {DASH_POLLS_PER_READ:.0f} poll-equivalents | {SRC['dash']} |")
    w(f"| status LED | {LED_I_MA:.0f} mA x {LED_FLASH_S:.0f} s x {LED_FLASHES_PER_DAY:.0f}/day | {SRC['led']} |")
    w(f"| Pololu S9V11E2A | {EFF_REG*100:.0f} % ({EFF_REG_WORST*100:.0f} % worst case), {REG_IQ_MA} mA quiescent at the pack voltage | {SRC['pololu']} |")
    w(f"| FireBeetle buck | {EFF_BUCK*100:.0f} % | {SRC['tps62a02']} |")
    w(f"| pack divider | {DIVIDER_OHM/1e6:.2f} MOhm across the pack | {SRC['divider']} |")
    for c in CELLS.values():
        w(f"| {c.name} | {c.mwh:.0f} mWh/cell, {c.usable*100:.0f} % usable, "
          f"{c.v_avg:.2f} V avg, {c.self_discharge_pct_month} %/month | {SRC[c.src]} |")
    w("")
    w("3.3 V loads are divided by both efficiencies "
      f"({EFF_REG:.2f} x {EFF_BUCK:.2f} = {EFF_3V3:.3f}); the Sunrise and the LED "
      "only by the regulator's. The regulator's quiescent current is charged at "
      "the full pack voltage on top of that, which double-counts some of its "
      "light-load loss - the conservative direction.")
    w("")
    w("## Runtime by cell type (ECO)")
    w("")
    w(f"{CELL_COUNT} cells in series, engineering margin **{MARGIN*100:.0f} %**. "
      "Worst case: SEN62 at its datasheet maximum and the regulator at "
      f"{EFF_REG_WORST*100:.0f} %.")
    w("")
    w("| cells | usable energy | nominal | with margin | worst case, with margin |")
    w("|---|---|---|---|---|")
    for r in eco_by_cell():
        name = r["cell"] + ("" if r["recommended"] else " - not recommended")
        w(f"| {name} | {r['pack_wh']:.1f} Wh | {r['months_nominal']:.1f} months | "
          f"**{r['months_with_margin']:.1f} months** | {r['months_worst_case']:.1f} months |")
    w("")
    w("## On external power")
    w("")
    w("With the USB-C power socket in use the device runs in CONTINUOUS mode from "
      "the charger. Cells left in the holder are the backup: the ideal diode "
      "switches to them without a gap when the power goes. Meanwhile they only "
      "lose the regulator's quiescent current, the lockout and divider "
      "resistors, and their own self-discharge:")
    w("")
    w("| cells | backup still usable after |")
    w("|---|---|")
    for r in eco_by_cell():
        w(f"| {r['cell']} | {r['months_as_mains_backup']:.1f} months |")
    w("")
    w("For permanent mains operation the holder can also stay empty.")
    w("")
    w(f"## All profiles on {cell.name}")
    w("")
    w("| profile | PM every | CO2 every | mWh/day | nominal | with margin | worst case, with margin |")
    w("|---|---|---|---|---|---|---|")
    for r in rows:
        w(f"| {r['profile']} | {r['pm_interval']} | {r['co2_interval']} | "
          f"{r['mwh_per_day']:.0f} | {_fmt_days(r['days_nominal'])} | "
          f"**{_fmt_days(r['days_with_margin'])}** | {_fmt_days(r['days_worst_case'])} |")
    w("")
    w("## Where the energy goes")
    w("")
    for r in rows:
        w(f"### {r['profile']}")
        w("")
        w(r["note"])
        w("")
        w("| consumer | mWh/day | share |")
        w("|---|---|---|")
        tot = r["mwh_per_day"]
        for k, v in sorted(r["lines"].items(), key=lambda kv: -kv[1]):
            w(f"| {k} | {v:.2f} | {100*v/tot:.1f} % |")
        w(f"| **total** | **{tot:.1f}** | |")
        w("")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cell", choices=sorted(CELLS), default=DEFAULT_CELL)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    cell = CELLS[a.cell]
    if a.json:
        print(json.dumps({"cell": cell.name, "cells": CELL_COUNT, "margin": MARGIN,
                          "profiles": table(cell), "eco_by_cell": eco_by_cell()},
                         indent=2))
    elif a.markdown:
        print(markdown(cell))
    else:
        print_table(cell)
    return 0


if __name__ == "__main__":
    sys.exit(main())
