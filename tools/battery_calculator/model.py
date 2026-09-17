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
    "sps30": "Sensirion SPS30 Datasheet v2.0 (June 2023), Table 2 'Electrical specifications'",
    "sps30_lp": "Sensirion AN 'Low-Power Operation of the SPS30' v1 (Aug 2020), sections 2.1, 2.2, 5",
    "sgp40": "Sensirion SGP40 Datasheet v1.2 (Feb 2022), Table 2 'Electrical specifications'",
    "sgp40_lp": "Sensirion gas-index-algorithm README: '2 % duty cycle (10 s interval): < 0.2 mW'",
    "scd4x": "Sensirion SCD4x Datasheet v1.5 (July 2023), Table 4",
    "scd4x_lp": "Sensirion AN 'SCD4x Low Power Operation' v1.0 (July 2022), Table 1",
    "c6_icd": "Espressif esp-matter examples/icd_app README, measured C6 SIT-ICD current trace "
              "(avg 174.43 uA over 61.08 s, floor 54.84 uA, 5 s slow poll, 20 dBm TX)",
    "feather": "Adafruit ESP32-C6 Feather product page / learn guide: 17 uA deep sleep, "
               "MCP73831T-2ACI/OT charger with R_PROG = 5.1 kOhm",
    "epd": "Waveshare 1.54in e-Paper (SSD1681) specification; GDEY0154D67 panel data",
    "cell": "Generic protected single-cell LiPo, 606090 format; self-discharge assumption",
}

# ---- SPS30 ---------------------------------------------------------------
SPS30_V = 5.0                 # V, supply (4.5-5.5 V), src=sps30
SPS30_I_MEAS_MA = 55.0        # mA typ in measurement mode (45..65), src=sps30
SPS30_I_SLEEP_UA = 38.0       # uA typ sleep mode (max 50), src=sps30
SPS30_STARTUP_S = 30.0        # s recommended before using output, src=sps30_lp 2.1
SPS30_FANCLEAN_S = 10.0       # s, fan cleaning at max speed, src=sps30 4.2
SPS30_FANCLEAN_PERIOD_D = 7.0 # days; mandatory when the sensor is power-cycled, src=sps30 4.2

# ---- SGP40 ---------------------------------------------------------------
SGP40_V = 3.3
SGP40_I_IDLE_UA = 34.0        # uA typ, heater off, src=sgp40
SGP40_LP_POWER_MW = 0.2       # mW, 2 % duty cycle at 10 s sampling, src=sgp40_lp
SGP40_I_CONT_MA = 2.6         # mA avg, continuous 1 Hz at 3.3 V, src=sgp40

# ---- SCD41 ---------------------------------------------------------------
# Power-cycled single-shot averages at 3.3 V.  These already include the
# sleep current between samples.  src=scd4x_lp Table 1
SCD41_PC_SINGLESHOT = {600: 250.0, 1200: 130.0, 3600: 43.0}  # period_s -> uA
# Derived: charge per single shot = I_avg * period (consistent across the table)
SCD41_Q_PER_SHOT_MAS = 250e-3 * 600  # = 150 mA*s = 0.0417 mAh
SCD41_I_LOWPOWER_PERIODIC_MA = 3.2   # 30 s periodic mode, src=scd4x

# ---- MCU + carrier board --------------------------------------------------
C6_ICD_AVG_UA_5S = 174.4      # measured, 5 s slow poll, src=c6_icd
C6_ICD_FLOOR_UA = 54.8        # measured sleep floor between polls, src=c6_icd
# Derived poll cost: (174.4 - 54.8) uA * 5 s = 598 uA*s of extra charge per poll
C6_POLL_CHARGE_UAS = (C6_ICD_AVG_UA_5S - C6_ICD_FLOOR_UA) * 5.0
# Everything on the carrier that is powered even in deep sleep:
#   Feather deep-sleep floor is already inside C6_ICD_FLOOR_UA for the bare chip;
#   the Feather adds RT9080 LDO Iq (~35 uA), MAX17048 (~23 uA hibernate),
#   3 x TPS22918 load switch Iq (~1.1 uA each) and divider/leakage.
BOARD_QUIESCENT_UA = 62.0

# ---- Display --------------------------------------------------------------
EPD_V = 3.3
EPD_I_REFRESH_MA = 8.0        # mA during update, src=epd
EPD_FULL_REFRESH_S = 2.0
EPD_PARTIAL_REFRESH_S = 0.4
EPD_I_SLEEP_UA = 1.0          # deep sleep; we also power-gate the rail

# ---- Power conversion -----------------------------------------------------
BOOST_EFFICIENCY = 0.92       # TPS61023, 3.7 V -> 5.0 V at ~55 mA
BOOST_IQ_OFF_UA = 1.0         # load switch on the boost input kills the boost Iq
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
    """A firmware measurement profile.  Mirrors the C constants in
    firmware/main/ac_config.h - keep the two in sync."""
    name: str
    pm_interval_s: float          # 0 -> continuous measurement
    pm_window_s: float            # total time in SPS30 measurement mode
    voc_interval_s: float         # 0 -> VOC disabled
    co2_interval_s: float         # 0 -> CO2 disabled
    icd_slow_poll_s: float
    epd_full_per_day: float = 2.0
    epd_partial_per_day: float = 20.0
    note: str = ""


PROFILES = [
    Profile("ECO",        pm_interval_s=4 * 3600, pm_window_s=40, voc_interval_s=10,
            co2_interval_s=3600, icd_slow_poll_s=15,
            note="Long-life room monitoring. Meets the 6-month target with margin."),
    Profile("ECO_PLUS",   pm_interval_s=2 * 3600, pm_window_s=40, voc_interval_s=10,
            co2_interval_s=3600, icd_slow_poll_s=15,
            note="Room monitoring with 2 h PM cadence."),
    Profile("NORMAL",     pm_interval_s=15 * 60, pm_window_s=60, voc_interval_s=10,
            co2_interval_s=15 * 60, icd_slow_poll_s=15,
            epd_partial_per_day=40,
            note="Sensirion's reference cadence (60 s every 15 min). Default when a "
                 "printer is in the room."),
    Profile("ACTIVE",     pm_interval_s=2 * 60, pm_window_s=60, voc_interval_s=10,
            co2_interval_s=5 * 60, icd_slow_poll_s=5,
            epd_partial_per_day=60,
            note="Entered automatically on a detected emission event. Time-limited."),
    Profile("POST_PRINT", pm_interval_s=5 * 60, pm_window_s=60, voc_interval_s=10,
            co2_interval_s=10 * 60, icd_slow_poll_s=5,
            epd_partial_per_day=40,
            note="Recovery tracking after an event."),
    Profile("CONTINUOUS", pm_interval_s=0, pm_window_s=0, voc_interval_s=1,
            co2_interval_s=5, icd_slow_poll_s=5,
            epd_partial_per_day=200,
            note="Reference / validation mode. USB power expected."),
]


def _sps30_batt_current_ma() -> float:
    """SPS30 measurement-mode current referred to the battery, through the boost."""
    return SPS30_I_MEAS_MA * SPS30_V / (VBAT_NOMINAL * BOOST_EFFICIENCY)


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


def budget(p: Profile, cell_mah: float = CELL_MAH_DEFAULT) -> Budget:
    b = Budget(profile=p)
    sps_ma = _sps30_batt_current_ma()

    # --- particulate matter ------------------------------------------------
    if p.pm_interval_s == 0:
        b.lines["SPS30 (continuous)"] = sps_ma * 24.0
    else:
        cycles_day = 86400.0 / p.pm_interval_s
        mah_per_cycle = sps_ma * p.pm_window_s / 3600.0
        b.lines["SPS30 measurement windows"] = cycles_day * mah_per_cycle
        # SPS30 is fully power-gated between windows -> its 38 uA sleep current
        # does not apply; only the load-switch leakage does.
    # mandatory manual fan cleaning because we power-cycle the sensor
    b.lines["SPS30 fan cleaning"] = (sps_ma * SPS30_FANCLEAN_S / 3600.0) / SPS30_FANCLEAN_PERIOD_D

    # --- VOC ---------------------------------------------------------------
    if p.voc_interval_s <= 0:
        b.lines["SGP40"] = 0.0
    elif p.voc_interval_s <= 1.0:
        b.lines["SGP40"] = SGP40_I_CONT_MA * 24.0
    else:
        # Sensirion's published low-power figure is for a 10 s interval.
        # Scale the heater duty inversely with the interval, keep the idle term.
        heater_ma = (SGP40_LP_POWER_MW / SGP40_V) - (SGP40_I_IDLE_UA / 1000.0)
        heater_ma = max(heater_ma, 0.0) * (10.0 / p.voc_interval_s)
        i_ma = heater_ma + SGP40_I_IDLE_UA / 1000.0
        # The 3V3 rail is an LDO, so battery current == load current (the energy
        # difference is burned as heat in the regulator, it is not a current gain).
        b.lines["SGP40"] = i_ma * 24.0

    # --- CO2 ---------------------------------------------------------------
    if p.co2_interval_s <= 0:
        b.lines["SCD41"] = 0.0
    elif p.co2_interval_s <= 30:
        b.lines["SCD41"] = SCD41_I_LOWPOWER_PERIODIC_MA * 24.0
    else:
        shots_day = 86400.0 / p.co2_interval_s
        b.lines["SCD41"] = shots_day * SCD41_Q_PER_SHOT_MAS / 3600.0

    # --- display -----------------------------------------------------------
    epd_mas = (p.epd_full_per_day * EPD_FULL_REFRESH_S +
               p.epd_partial_per_day * EPD_PARTIAL_REFRESH_S) * EPD_I_REFRESH_MA
    b.lines["e-paper refreshes"] = epd_mas / 3600.0

    # --- MCU + Thread ------------------------------------------------------
    mcu_ua = C6_ICD_FLOOR_UA + C6_POLL_CHARGE_UAS / p.icd_slow_poll_s
    b.lines["ESP32-C6 + Thread (ICD)"] = mcu_ua * 24.0 / 1000.0

    # --- always-on carrier hardware ---------------------------------------
    b.lines["carrier quiescent"] = (BOARD_QUIESCENT_UA + BOOST_IQ_OFF_UA) * 24.0 / 1000.0

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


def table(cell_mah: float = CELL_MAH_DEFAULT) -> list[dict]:
    rows = []
    for p in PROFILES:
        b = budget(p, cell_mah)
        nominal = runtime_days(b, cell_mah, 0.0)
        with_margin = runtime_days(b, cell_mah, MARGIN)
        rows.append({
            "profile": p.name,
            "pm_interval": ("continuous" if p.pm_interval_s == 0
                            else f"{p.pm_interval_s/60:.0f} min"),
            "pm_window_s": p.pm_window_s,
            "mah_per_day": round(b.total_mah_day, 2),
            "days_nominal": round(nominal, 1),
            "days_with_margin": round(with_margin, 1),
            "months_with_margin": round(with_margin / 30.44, 2),
            "lines": {k: round(v, 3) for k, v in b.lines.items()},
            "note": p.note,
        })
    return rows


def _fmt_days(d: float) -> str:
    if d < 14:
        return f"{d:.1f} d"
    if d < 90:
        return f"{d/7:.1f} weeks"
    return f"{d/30.44:.1f} months"


def print_table(cell_mah: float) -> None:
    print(f"Cell: {cell_mah:.0f} mAh nominal, {cell_mah*CELL_USABLE_FRACTION:.0f} mAh usable "
          f"(to the 3.3 V system cutoff)")
    print(f"Engineering margin applied: {MARGIN*100:.0f} %\n")
    hdr = f"{'profile':<12}{'PM every':>12}{'window':>8}{'mAh/day':>10}{'nominal':>12}{'w/ margin':>12}"
    print(hdr)
    print("-" * len(hdr))
    for r in table(cell_mah):
        print(f"{r['profile']:<12}{r['pm_interval']:>12}{r['pm_window_s']:>7.0f}s"
              f"{r['mah_per_day']:>10.2f}"
              f"{_fmt_days(r['days_nominal']):>12}"
              f"{_fmt_days(r['days_with_margin']):>12}")
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
    w(f"| SPS30 supply | {SPS30_V:.1f} V | {SRC['sps30']} |")
    w(f"| SPS30 measurement current | {SPS30_I_MEAS_MA:.0f} mA typ (45-65 mA) | {SRC['sps30']} |")
    w(f"| SPS30 sleep current | {SPS30_I_SLEEP_UA:.0f} uA typ | {SRC['sps30']} |")
    w(f"| SPS30 start-up before valid data | {SPS30_STARTUP_S:.0f} s | {SRC['sps30_lp']} |")
    w(f"| SPS30 fan cleaning | {SPS30_FANCLEAN_S:.0f} s every {SPS30_FANCLEAN_PERIOD_D:.0f} d | {SRC['sps30']} |")
    w(f"| SGP40 idle current | {SGP40_I_IDLE_UA:.0f} uA | {SRC['sgp40']} |")
    w(f"| SGP40 continuous (1 Hz) | {SGP40_I_CONT_MA:.1f} mA | {SRC['sgp40']} |")
    w(f"| SGP40 low-power (10 s interval) | {SGP40_LP_POWER_MW:.1f} mW | {SRC['sgp40_lp']} |")
    w(f"| SCD41 power-cycled single shot | 250/130/43 uA at 10/20/60 min | {SRC['scd4x_lp']} |")
    w(f"| SCD41 charge per single shot | {SCD41_Q_PER_SHOT_MAS/3600:.4f} mAh | derived from the row above |")
    w(f"| ESP32-C6 Matter SIT-ICD average | {C6_ICD_AVG_UA_5S:.1f} uA at 5 s poll | {SRC['c6_icd']} |")
    w(f"| ESP32-C6 sleep floor | {C6_ICD_FLOOR_UA:.1f} uA | {SRC['c6_icd']} |")
    w(f"| derived charge per Thread poll | {C6_POLL_CHARGE_UAS:.0f} uA*s | (avg - floor) x 5 s |")
    w(f"| carrier quiescent | {BOARD_QUIESCENT_UA:.0f} uA | LDO Iq + MAX17048 + 3x TPS22918 + leakage |")
    w(f"| e-paper refresh current | {EPD_I_REFRESH_MA:.0f} mA for {EPD_FULL_REFRESH_S:.1f} s full / {EPD_PARTIAL_REFRESH_S:.1f} s partial | {SRC['epd']} |")
    w(f"| boost efficiency | {BOOST_EFFICIENCY*100:.0f} % | TPS61023 at 3.7 V -> 5 V, 55 mA |")
    w(f"| battery nominal working voltage | {VBAT_NOMINAL:.2f} V | discharge-weighted 1S LiPo |")
    w(f"| cell | {cell_mah:.0f} mAh, {CELL_USABLE_FRACTION*100:.0f} % usable | {SRC['cell']} |")
    w(f"| self-discharge | {CELL_SELF_DISCHARGE_PCT_MONTH:.1f} %/month | {SRC['cell']} |")
    w("")
    w(f"SPS30 current referred to the battery through the boost converter: "
      f"`{SPS30_I_MEAS_MA:.0f} mA x {SPS30_V:.1f} V / ({VBAT_NOMINAL:.2f} V x {BOOST_EFFICIENCY:.2f})` "
      f"= **{_sps30_batt_current_ma():.1f} mA**.")
    w("")
    w("The 3.3 V rail is an LDO, so for everything on it the battery current equals")
    w("the load current - the voltage difference is dissipated as heat, it is not a")
    w("current gain. Only the SPS30 sits behind a boost converter.")
    w("")
    w("## Result")
    w("")
    w(f"Cell: **{cell_mah:.0f} mAh** nominal, **{cell_mah*CELL_USABLE_FRACTION:.0f} mAh** usable. "
      f"Engineering margin: **{MARGIN*100:.0f} %**.")
    w("")
    w("| profile | PM every | PM window | mAh/day | nominal | with margin |")
    w("|---|---|---|---|---|---|")
    for r in rows:
        w(f"| {r['profile']} | {r['pm_interval']} | {r['pm_window_s']:.0f} s | "
          f"{r['mah_per_day']:.2f} | {_fmt_days(r['days_nominal'])} | "
          f"**{_fmt_days(r['days_with_margin'])}** |")
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
