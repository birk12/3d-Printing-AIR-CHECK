"""
3D Printing AIR CHECK - energy model (v1.4, LiFePO4 power module C).

Every number in this file is traceable to a manufacturer datasheet or to a
measurement published by the silicon vendor.  Sources are given inline as
`src=` strings and are reproduced in docs/BATTERY_LIFE.md.  Where a value is an
assumption, its source string says ASSUMPTION.

Since v1.4 the device runs from the Power-Standard's module C: a 1S4P pack of
LiFePO4 cells, charged in the device from USB-C.  Power path (EDR-21):

    cells (3.0-3.65 V) -> PICO fuse each -> HY2112 BMS -> Adafruit #6091
    (TI BQ25185) -> LOAD -> Pololu S9V11E2A (buck-boost, 3.90 V) -> LM66200
    -> FireBeetle battery input (VSYS) -> TPS62A02 buck -> 3.3 V

On USB-C the module feeds LOAD from the charger (power path) and the cells
only lose the module's quiescent currents; this model is for running on the
cells.

Run:  python3 tools/battery_calculator/model.py                  # table
      python3 tools/battery_calculator/model.py --markdown       # BATTERY_LIFE.md body
      python3 tools/battery_calculator/model.py --json           # CI
      python3 tools/battery_calculator/model.py --cell 1s1p      # another pack
      python3 tools/battery_calculator/model.py --r3-6091 fitted-unfavourable
                                          # as if #6091-R3 had not been desoldered
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
    "vbat_s": "Power-Standard PWR-K: cell voltage / 2 through 470 k + 470 k, permanently "
              "across the cells",
    "c3": "Panasonic FR series (EEU-FR0J471): leakage <= 0.01CV = 30 uA after 2 min, "
          "the worst case; C3 buffers the SEN62's switch-on step (audit NC-05)",
    "bq25185": "TI BQ25185 datasheet SLUSF65B: 4 uA from the battery with no input; "
               "BUVLO 3.0 V",
    "hy2112": "HY2112 datasheet: 3 uA typical; the eremit board's own figure is "
              "unverified (Power-Standard open point O1)",
    "led": "status LED: 5 mA from VSYS (3.9 V) for a 3 s flash, 6 times a day",
    "dash": "ASSUMPTION: a second Matter controller (the e-ink dashboard) reads the "
            "sensor every 15 min; each read costs ~10 poll-equivalents of radio time",
    "aer18650": "Lithium Werks AER18650m2A2: 1.8 Ah typical, 1.7 Ah minimum, 3.2-3.3 V "
                "plateau; charged to 3.65 V, BUVLO at 3.0 V leaves ~4 % "
                "(Power-Standard README section 2)",
    "lfp_sd": "ASSUMPTION: LFP self-discharge 3 %/month - no manufacturer figure for the "
              "AER18650m2A2 (Power-Standard open point O5)",
    "r3_6091": "Adafruit bq25185 Breakout rev B1.sch (commit e73b39b), traced by the "
               "Power-Standard (README 2a, 2026-09-26): SYS - green VSYSOK LED - "
               "#6091-R3 1 k - GND, no jumper. On the cells SYS is the cell voltage, "
               "I = (V_cell - Vf) / 1 k with Vf undocumented; the standard's budget "
               "values on the LFP plateau are 0.50 mA (Vf 2.7 V at 3.20 V) and 1.45 mA "
               "(Vf 1.9 V at 3.35 V). Desoldered in this build (ASSEMBLY 4.1, EDR-24)",
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
VBAT_S_OHM = 470_000 + 470_000      # src=vbat_s
C3_LEAK_UA = 30.0     # src=c3
CHARGER_IQ_UA = 4.0                 # src=bq25185
BMS_IQ_UA = 3.0                     # src=hy2112

# ---- #6091-R3: the charger board's green VSYSOK LED (EDR-24) ---------------
# The LED and its 1 k series resistor R3 sit permanently between the #6091's
# SYS (= LOAD+) and GND, with no jumper.  On the cells it draws straight from
# them, upstream of the regulator, so no efficiency applies: I x V_cell.  This
# build desolders #6091-R3 (ASSEMBLY 4.1, TESTING PS-1.9), so the default is
# 0 mA; the two "fitted" states keep the cost of forgetting it visible.  They
# are the Power-Standard's fixed budget currents for every project, not
# (V_cell - Vf) / 1 k at v_avg - the same numbers in every project's budget.
# src=r3_6091.  electronics/schematic/design.py checks that this default
# matches the schematic (part R3_6091, declared removed).
R3_6091_LED_MA = {
    "removed": 0.0,                 # as built
    "fitted-favourable": 0.50,      # Vf 2.7 V (InGaN green) at 3.20 V
    "fitted-unfavourable": 1.45,    # Vf 1.9 V (GaP yellow-green) at 3.35 V
}
R3_6091_STATE = "removed"

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
    """A 1S pack of LiFePO4 cells in parallel."""
    key: str
    name: str
    parallel: int
    mah: float                  # per cell, minimum
    usable: float               # fraction above the 3.0 V BUVLO
    v_avg: float                # average cell voltage on the discharge plateau
    self_discharge_pct_month: float
    src: str
    recommended: bool = True


def _lfp(n: int, rec: bool) -> Cell:
    return Cell(f"1s{n}p", f"{n} x AER18650m2A2 LiFePO4 (1S{n}P)", n, 1700.0, 0.96, 3.25,
                3.0, "aer18650", recommended=rec)


CELLS = {"1s4p": _lfp(4, True), "1s2p": _lfp(2, False), "1s1p": _lfp(1, False)}
DEFAULT_CELL = "1s4p"


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


def budget(p: Profile, cell: Cell, worst: bool = False, r3: str | None = None) -> Budget:
    b = Budget(profile=p, cell=cell)
    r3 = r3 or R3_6091_STATE
    eff_reg = EFF_REG_WORST if worst else EFF_REG
    eff_3v3 = eff_reg * EFF_BUCK
    v_pack = cell.v_avg

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
    b.lines["C3 leakage (+3V3)"] = at3v3(C3_LEAK_UA * 3.3)
    b.lines["status LED"] = at4v0(
        LED_I_MA * 1000.0 * 3.9 * LED_FLASH_S * LED_FLASHES_PER_DAY / 86400.0)

    # --- power path -------------------------------------------------------
    b.lines["Pololu regulator quiescent"] = REG_IQ_MA * v_pack * 24.0
    b.lines["charger + BMS quiescent"] = (CHARGER_IQ_UA + BMS_IQ_UA) * v_pack * 24.0 / 1000.0
    b.lines["cell voltage divider (VBAT_S)"] = v_pack ** 2 / VBAT_S_OHM * 1000.0 * 24.0
    # straight from the cells, no efficiency: src=r3_6091
    b.lines["#6091-R3 VSYSOK LED"] = R3_6091_LED_MA[r3] * v_pack * 24.0

    # --- cells ------------------------------------------------------------
    b.lines["cell self-discharge"] = (pack_total_mwh(cell)
                                      * cell.self_discharge_pct_month / 100.0 / 30.44)
    return b


def pack_total_mwh(cell: Cell) -> float:
    return cell.parallel * cell.mah * cell.v_avg


def pack_usable_mwh(cell: Cell) -> float:
    return pack_total_mwh(cell) * cell.usable


def runtime_days(b: Budget, margin: float = 0.0) -> float:
    return pack_usable_mwh(b.cell) / (b.total_mwh_day * (1.0 + margin))


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

MARGIN = 0.25         # 25 % engineering margin, per the project requirement
TARGET_MONTHS = 2.5   # ECO on 1S4P must clear this; ci/github-actions-ci.yml


def table(cell: Cell, r3: str | None = None) -> list[dict]:
    rows = []
    for p in PROFILES:
        b = budget(p, cell, r3=r3)
        nominal = runtime_days(b)
        with_margin = runtime_days(b, MARGIN)
        worst = runtime_days(budget(p, cell, worst=True, r3=r3), MARGIN)
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


def eco_by_cell(r3: str | None = None) -> list[dict]:
    eco = PROFILES[0]
    out = []
    for c in CELLS.values():
        b = budget(eco, c, r3=r3)
        out.append({
            "cell": c.name, "key": c.key, "recommended": c.recommended,
            "pack_wh": round(pack_usable_mwh(c) / 1000.0, 1),
            "months_nominal": round(runtime_days(b) / 30.44, 2),
            "months_with_margin": round(runtime_days(b, MARGIN) / 30.44, 2),
            "months_worst_case": round(
                runtime_days(budget(eco, c, worst=True, r3=r3), MARGIN) / 30.44, 2),
        })
    return out


def r3_comparison(cell: Cell) -> list[dict]:
    """Every profile with #6091-R3 removed (as built) and still fitted (EDR-24)."""
    rows = []
    for p in PROFILES:
        for state, ma in R3_6091_LED_MA.items():
            b = budget(p, cell, r3=state)
            rows.append({
                "profile": p.name, "r3_6091": state, "led_ma": ma,
                "mwh_per_day": round(b.total_mwh_day, 1),
                "days_nominal": round(runtime_days(b), 1),
                "days_with_margin": round(runtime_days(b, MARGIN), 1),
                "days_worst_case": round(
                    runtime_days(budget(p, cell, worst=True, r3=state), MARGIN), 1),
            })
    return rows


def _fmt_days(d: float) -> str:
    if d < 14:
        return f"{d:.1f} d"
    if d < 60:
        return f"{d/7:.1f} weeks"
    return f"{d/30.44:.1f} months"


R3_LABEL = {
    "removed": "removed (as built)",
    "fitted-favourable": "fitted, Vf 2.7 V",
    "fitted-unfavourable": "fitted, Vf 1.9 V",
}


def print_table(cell: Cell, r3: str | None = None) -> None:
    r3 = r3 or R3_6091_STATE
    print(f"Pack: {cell.name}, {pack_usable_mwh(cell)/1000:.1f} Wh usable")
    print(f"Engineering margin applied: {MARGIN*100:.0f} %")
    print(f"#6091-R3 (VSYSOK LED): {R3_LABEL[r3]}, {R3_6091_LED_MA[r3]:.2f} mA from the cells\n")
    hdr = (f"{'profile':<12}{'PM every':>12}{'CO2 every':>11}{'mWh/day':>10}"
           f"{'nominal':>13}{'w/ margin':>13}{'worst case':>13}")
    print(hdr)
    print("-" * len(hdr))
    for r in table(cell, r3):
        print(f"{r['profile']:<12}{r['pm_interval']:>12}{r['co2_interval']:>11}"
              f"{r['mwh_per_day']:>10.1f}{_fmt_days(r['days_nominal']):>13}"
              f"{_fmt_days(r['days_with_margin']):>13}{_fmt_days(r['days_worst_case']):>13}")
    print("\nECO by pack (with margin):")
    for r in eco_by_cell(r3):
        print(f"  {r['cell']:<34}{r['pack_wh']:>6.1f} Wh {r['months_with_margin']:>6.2f} months"
              f"{'' if r['recommended'] else '   (for comparison)'}")
    print(f"\n#6091-R3 removed vs. still fitted, {cell.name} (EDR-24):")
    hdr = (f"  {'profile':<12}{'#6091-R3':<20}{'LED mA':>7}{'mWh/day':>10}"
           f"{'nominal':>13}{'w/ margin':>13}{'worst case':>13}")
    print(hdr)
    print("  " + "-" * (len(hdr) - 2))
    for r in r3_comparison(cell):
        print(f"  {r['profile']:<12}{R3_LABEL[r['r3_6091']]:<20}{r['led_ma']:>7.2f}"
              f"{r['mwh_per_day']:>10.1f}{_fmt_days(r['days_nominal']):>13}"
              f"{_fmt_days(r['days_with_margin']):>13}{_fmt_days(r['days_worst_case']):>13}")
    print("\nPer-profile breakdown (mWh/day):")
    for r in table(cell, r3):
        print(f"\n  {r['profile']}")
        for k, v in sorted(r["lines"].items(), key=lambda kv: -kv[1]):
            print(f"    {k:<34}{v:>8.2f}")


def markdown(cell: Cell, r3: str | None = None) -> str:
    """The generated part of docs/BATTERY_LIFE.md."""
    r3 = r3 or R3_6091_STATE
    rows = table(cell, r3)
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
    w(f"| cell voltage divider | {VBAT_S_OHM/1e3:.0f} kOhm across the cells | {SRC['vbat_s']} |")
    w(f"| charger + BMS quiescent | {CHARGER_IQ_UA:.0f} + {BMS_IQ_UA:.0f} uA | {SRC['bq25185']}; {SRC['hy2112']} |")
    w(f"| C3 bulk capacitor | {C3_LEAK_UA:.0f} uA leakage at 3.3 V | {SRC['c3']} |")
    w(f"| #6091-R3 (VSYSOK LED) | {R3_LABEL[r3]}: {R3_6091_LED_MA[r3]:.2f} mA from the cells "
      f"(fitted: {R3_6091_LED_MA['fitted-favourable']:.2f}-"
      f"{R3_6091_LED_MA['fitted-unfavourable']:.2f} mA) | {SRC['r3_6091']} |")
    c = CELLS[DEFAULT_CELL]
    w(f"| cell | AER18650m2A2, {c.mah:.0f} mAh min, {c.v_avg:.2f} V avg, "
      f"{c.usable*100:.0f} % usable | {SRC['aer18650']} |")
    w(f"| self-discharge | {c.self_discharge_pct_month} %/month | {SRC['lfp_sd']} |")
    w("")
    w("3.3 V loads are divided by both efficiencies "
      f"({EFF_REG:.2f} x {EFF_BUCK:.2f} = {EFF_3V3:.3f}); the Sunrise and the LED "
      "only by the regulator's. The regulator's quiescent current is charged at "
      "the cell voltage on top of that, which double-counts some of its "
      "light-load loss - the conservative direction. The regulator boosts the "
      "3.0-3.65 V of the cells to 3.90 V; its 85 % is the bottom of Pololu's band "
      "and has not been measured at this operating point.")
    w("")
    w("## Runtime on the cells by pack (ECO)")
    w("")
    w(f"Engineering margin **{MARGIN*100:.0f} %**. "
      "Worst case: SEN62 at its datasheet maximum and the regulator at "
      f"{EFF_REG_WORST*100:.0f} %.")
    w("")
    w("| pack | usable energy | nominal | with margin | worst case, with margin |")
    w("|---|---|---|---|---|")
    for r in eco_by_cell(r3):
        name = r["cell"] + (" - **fitted**" if r["recommended"] else " - for comparison")
        w(f"| {name} | {r['pack_wh']:.1f} Wh | {r['months_nominal']:.1f} months | "
          f"**{r['months_with_margin']:.1f} months** | {r['months_worst_case']:.1f} months |")
    w("")
    w("## On USB-C")
    w("")
    w("On USB-C the module feeds the device from the charger (power path); the "
      "cells lose only the charger's and the BMS's quiescent current, the cell "
      "voltage divider and their own self-discharge. After a full charge the "
      "firmware pauses charging (CE) until the cells drop below 3.30 V, were "
      "used, or 30 days have passed - so on permanent USB-C the cells are "
      "topped up about once a month and are always close to full when the "
      "power goes.")
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
    w("## If #6091-R3 is still fitted")
    w("")
    w("The #6091's green VSYSOK LED and its 1 k resistor R3 hang permanently "
      "between SYS and GND. On the cells they draw from them directly, ahead of "
      "the regulator, so the current counts at the cell voltage with no "
      "efficiency on top. This build desolders #6091-R3 (ASSEMBLY 4.1, TESTING "
      "PS-1.9, EDR-24); the rows below show what forgetting it costs, with the "
      "Power-Standard's budget currents for the two forward voltages the LED "
      "may have.")
    w("")
    w(f"| profile | #6091-R3 | LED current | mWh/day | nominal | with margin | "
      f"worst case, with margin |")
    w("|---|---|---|---|---|---|---|")
    for r in r3_comparison(cell):
        mark = "**" if r["r3_6091"] == "removed" else ""
        w(f"| {r['profile']} | {mark}{R3_LABEL[r['r3_6091']]}{mark} | {r['led_ma']:.2f} mA | "
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
    ap.add_argument("--r3-6091", choices=list(R3_6091_LED_MA), default=R3_6091_STATE,
                    help="state of the #6091's VSYSOK LED resistor (EDR-24); "
                         "as built: removed")
    a = ap.parse_args()
    cell = CELLS[a.cell]
    r3 = a.r3_6091
    if a.json:
        print(json.dumps({"cell": cell.name, "cells": cell.parallel, "margin": MARGIN,
                          "r3_6091": r3, "r3_6091_led_ma": R3_6091_LED_MA[r3],
                          "profiles": table(cell, r3), "eco_by_cell": eco_by_cell(r3),
                          "r3_6091_comparison": r3_comparison(cell)},
                         indent=2))
    elif a.markdown:
        print(markdown(cell, r3))
    else:
        print_table(cell, r3)
    return 0


if __name__ == "__main__":
    sys.exit(main())
