"""
3D Printing AIR CHECK - electrical design source of truth.

This module *is* the schematic.  It declares every part, every net and every
connection, then runs an electrical rule check over them.  From it we emit:

  * electronics/schematic/aircheck.net   KiCad-compatible flat netlist
  * electronics/schematic/NETLIST.md     human-readable net list + pin map
  * electronics/bom/bom.csv              bill of materials
  * docs/BOM.md                          the same, rendered

Running this file with no arguments runs the ERC and exits non-zero on any
error, which is how the electronics are "build verified".

Pin numbers and electrical limits come from the manufacturer documents cited in
docs/ENGINEERING_DECISIONS.md.  Nothing here is recalled from memory.
"""

from __future__ import annotations

import argparse
import csv
import io
import os
import sys
from dataclasses import dataclass, field
from typing import Iterable

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# --------------------------------------------------------------------------
# Power domains
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Rail:
    name: str
    vmin: float
    vnom: float
    vmax: float
    source: str

RAILS = {
    "VUSB":       Rail("VUSB", 4.75, 5.00, 5.25, "USB-C VBUS on the Feather"),
    "VBAT":       Rail("VBAT", 3.20, 3.80, 4.20, "1S LiPo, protected"),
    "VBOOST_IN":  Rail("VBOOST_IN", 3.20, 3.80, 4.20, "VBAT behind load switch SW1"),
    "+5V":        Rail("+5V", 4.90, 5.00, 5.10, "TPS61023 boost"),
    "+3V3":       Rail("+3V3", 3.20, 3.30, 3.40, "Feather RT9080/AP2112 LDO, always on"),
    "+3V3_SENS":  Rail("+3V3_SENS", 3.20, 3.30, 3.40, "+3V3 behind load switch SW2"),
    "GND":        Rail("GND", 0.0, 0.0, 0.0, "system ground"),
}

# --------------------------------------------------------------------------
# Parts
# --------------------------------------------------------------------------

@dataclass
class Part:
    ref: str
    value: str
    mfr: str
    mpn: str
    footprint: str
    pins: dict          # pin designator -> human name
    why: str
    qty: int = 1
    price_eur: float = 0.0
    supplier: str = ""
    required: bool = True
    # electrical envelope, used by the ERC
    vsupply_min: float | None = None
    vsupply_max: float | None = None
    io_vmax: float | None = None
    i_typ_ma: float | None = None
    i_max_ma: float | None = None
    i2c_addr: int | None = None
    datasheet: str = ""


PARTS: list[Part] = [
    Part(
        ref="M1", value="ESP32-C6 Feather", mfr="Adafruit", mpn="5933",
        footprint="Feather 0.1in headers, 50.8 x 22.9 mm",
        pins={
            "USB": "USB 5V out", "BAT": "battery +", "EN": "3V3 LDO enable",
            "3V": "+3V3 out", "GND": "ground",
            "A0": "GPIO1", "A1": "GPIO4", "A2": "GPIO6", "A3": "GPIO5",
            "A4": "GPIO3", "A5": "GPIO2",
            "SCK": "GPIO21", "MOSI": "GPIO22", "MISO": "GPIO23",
            "RX": "GPIO17", "TX": "GPIO16",
            "SCL": "GPIO18", "SDA": "GPIO19",
            "D5": "GPIO5", "D6": "GPIO6", "D9": "GPIO7", "D10": "GPIO8",
            "D11": "GPIO0", "D12": "GPIO14", "D13": "GPIO15",
            "JST_BAT": "JST PH 2.0 battery input",
        },
        why="ESP32-C6-MINI-1 (4 MB flash, 512 kB SRAM) with native 802.15.4 for Thread, "
            "USB-C, MCP73831 LiPo charger (R_PROG 5.1k -> ~196 mA), RT9080/AP2112 3V3 LDO "
            "and a MAX17048 fuel gauge already on board. Removes the three highest-risk "
            "blocks (charger, fuel gauge, USB-C) from the DIY build. NOTE, from Adafruit's "
            "own schematic: the board's I2C pull-ups (R3, 10k) and its WS2812B sit on "
            "VSENSOR, the second LDO switched by GPIO20. See EDR-11.",
        price_eur=22.0, supplier="Adafruit / Mouser / Digi-Key / Berrybase",
        vsupply_min=3.2, vsupply_max=5.5, io_vmax=3.6,
        datasheet="https://learn.adafruit.com/adafruit-esp32-c6-feather",
    ),
    Part(
        ref="U1", value="SPS30", mfr="Sensirion", mpn="SPS30",
        footprint="41.2 x 41.2 x 12.2 mm module, JST ZHR-5 (1.5 mm)",
        pins={"1": "VDD 5V", "2": "RX / SDA", "3": "TX / SCL", "4": "SEL", "5": "GND"},
        why="Factory-calibrated optical PM sensor with PM1/PM2.5/PM4/PM10 mass and number "
            "concentration, >10 year rated life, documented low-power duty cycling.",
        price_eur=38.0, supplier="Mouser / Digi-Key / Farnell",
        vsupply_min=4.5, vsupply_max=5.5, io_vmax=5.5,
        i_typ_ma=55.0, i_max_ma=80.0,
        datasheet="Sensirion SPS30 Datasheet v2.0, June 2023",
    ),
    Part(
        ref="U2", value="SGP40 breakout", mfr="Adafruit", mpn="4829",
        footprint="STEMMA QT breakout 25.5 x 17.7 mm",
        pins={"VIN": "3.3V in", "GND": "ground", "SDA": "I2C data", "SCL": "I2C clock"},
        why="SGP40 MOX VOC sensor with Sensirion's VOC Index algorithm and on-chip "
            "humidity compensation. Breakout avoids hand-soldering a 2.44 mm DFN.",
        price_eur=14.0, supplier="Adafruit / Berrybase",
        vsupply_min=3.0, vsupply_max=3.6, io_vmax=3.6,
        i_typ_ma=2.6, i_max_ma=3.0, i2c_addr=0x59,
        datasheet="Sensirion SGP40 Datasheet v1.2, February 2022",
    ),
    Part(
        ref="U3", value="SCD41 breakout", mfr="Adafruit", mpn="5190",
        footprint="STEMMA QT breakout 25.5 x 17.7 mm",
        pins={"VIN": "3.3V in", "GND": "ground", "SDA": "I2C data", "SCL": "I2C clock"},
        why="True NDIR CO2 (photoacoustic) plus temperature and humidity, with a "
            "single-shot mode that makes 43 uA average possible at a 1 h cadence.",
        price_eur=52.0, supplier="Adafruit / Berrybase",
        vsupply_min=2.4, vsupply_max=5.5, io_vmax=3.6,
        i_typ_ma=15.0, i_max_ma=205.0, i2c_addr=0x62,
        datasheet="Sensirion SCD4x Datasheet v1.5, July 2023",
    ),
    Part(
        ref="U4", value="TPS61023", mfr="Texas Instruments", mpn="TPS61023DRLR",
        footprint="SOT-563",
        pins={"1": "VIN", "2": "EN", "3": "GND", "4": "FB", "5": "SW", "6": "VOUT"},
        why="3.3 V -> 5 V boost for the SPS30 at ~92 % efficiency at 55 mA, 2 A switch "
            "so the 80 mA fan-start inrush is not close to any limit.",
        price_eur=1.60, supplier="Mouser / Digi-Key",
        vsupply_min=1.8, vsupply_max=5.5, i_max_ma=2000.0,
        datasheet="TI TPS61023 datasheet",
    ),
    Part(
        ref="SW1", value="TPS22918", mfr="Texas Instruments", mpn="TPS22918DBVR",
        footprint="SOT-23-6",
        pins={"1": "VIN", "2": "GND", "3": "ON", "4": "CT", "5": "VOUT", "6": "VOUT"},
        why="Load switch on the *input* of the boost converter, so the boost quiescent "
            "current disappears completely when the SPS30 is not measuring.",
        price_eur=0.85, supplier="Mouser / Digi-Key",
        vsupply_min=0.8, vsupply_max=5.5, i_max_ma=2000.0,
        datasheet="TI TPS22918 datasheet",
    ),
    Part(
        ref="SW2", value="TPS22918", mfr="Texas Instruments", mpn="TPS22918DBVR",
        footprint="SOT-23-6",
        pins={"1": "VIN", "2": "GND", "3": "ON", "4": "CT", "5": "VOUT", "6": "VOUT"},
        why="Hard power cycle for the I2C sensor rail: the only reliable recovery from a "
            "sensor that has hung the bus, and the shutdown path at critical battery.",
        price_eur=0.85, supplier="Mouser / Digi-Key",
        vsupply_min=0.8, vsupply_max=5.5, i_max_ma=2000.0,
    ),
    Part(ref="L1", value="2.2 uH, >=2 A sat", mfr="Murata", mpn="DFE252012F-2R2M",
         footprint="2520 (2.5 x 2.0 mm)", pins={"1": "a", "2": "b"},
         why="TPS61023 reference inductor.", price_eur=0.45, supplier="Mouser"),
    Part(ref="C1", value="22 uF / 10 V X7R", mfr="Murata", mpn="GRM21BR71A226ME44L",
         footprint="0805", pins={"1": "+", "2": "-"},
         why="Boost input bulk.", price_eur=0.25, supplier="Mouser"),
    Part(ref="C2", value="47 uF / 10 V X5R", mfr="Murata", mpn="GRM21BR61A476ME15L",
         footprint="0805", pins={"1": "+", "2": "-"},
         why="Boost output bulk; holds the rail through the SPS30 fan-start step.",
         price_eur=0.35, supplier="Mouser"),
    Part(ref="C3", value="100 uF / 10 V", mfr="Panasonic", mpn="EEE-FT1A101AP",
         footprint="SMD electrolytic 6.3 x 5.8 mm", pins={"1": "+", "2": "-"},
         why="Local reservoir at the SPS30 connector; the sensor is at the end of a "
             "cable and pulls 80 mA for the first 200 ms.",
         price_eur=0.40, supplier="Mouser"),
    Part(ref="C4", value="1 uF / 16 V X7R", mfr="Murata", mpn="GRM188R71C105KA12D",
         footprint="0603", pins={"1": "+", "2": "-"},
         why="+3V3_SENS decoupling.", price_eur=0.10, supplier="Mouser"),
    Part(ref="C6", value="100 nF / 16 V X7R", mfr="Murata", mpn="GRM188R71C104KA01D",
         footprint="0603", pins={"1": "+", "2": "-"},
         why="Button RC debounce with R3.", price_eur=0.05, supplier="Mouser"),
    Part(ref="R1", value="330 R", mfr="Yageo", mpn="RC0603FR-07330RL", footprint="0603",
         pins={"1": "a", "2": "b"},
         why="Series resistor in the SPS30 UART TX line: limits any current injected "
             "into the unpowered sensor and damps the cable.", price_eur=0.02, supplier="Mouser"),
    Part(ref="R2", value="330 R", mfr="Yageo", mpn="RC0603FR-07330RL", footprint="0603",
         pins={"1": "a", "2": "b"},
         why="Same for the SPS30 UART RX line.", price_eur=0.02, supplier="Mouser"),
    Part(ref="R3", value="100 k", mfr="Yageo", mpn="RC0603FR-07100KL", footprint="0603",
         pins={"1": "a", "2": "b"},
         why="Button pull-up to +3V3 (always-on rail, so the button can wake the MCU "
             "out of deep sleep). The button is on GPIO1, an RTC pin.",
         price_eur=0.02, supplier="Mouser"),
    Part(ref="R5", value="1 M", mfr="Yageo", mpn="RC0603FR-071ML", footprint="0603",
         pins={"1": "a", "2": "b"},
         why="Bleeder on +3V3_SENS so the rail actually collapses when SW2 opens.",
         price_eur=0.02, supplier="Mouser"),
    Part(ref="SW4", value="tactile switch 6 x 6 x 5.0 mm, through hole",
         mfr="Alps/Omron", mpn="B3F-4050 (or any 6x6x5.0 mm THT tactile)",
         footprint="THT 6 x 6 mm, 5.0 mm total height",
         pins={"1": "a", "2": "b"},
         why="Single multifunction user button. The 5.0 mm overall height is "
             "what the enclosure is dimensioned around: it puts the stem tip "
             "3.5 mm behind the front face, leaving 0.3 mm of travel under the "
             "printed cap.",
         price_eur=0.30, supplier="Mouser / Reichelt"),
    Part(ref="R6", value="4.7 k", mfr="Yageo", mpn="RC0603FR-074K7L", footprint="0603",
         pins={"1": "a", "2": "b"},
         why="SDA pull-up for the sensor bus, to the *switched* sensor rail: when the "
             "rail is off there is no pull-up left to back-feed the unpowered sensors.",
         price_eur=0.02, supplier="Mouser"),
    Part(ref="R7", value="4.7 k", mfr="Yageo", mpn="RC0603FR-074K7L", footprint="0603",
         pins={"1": "a", "2": "b"},
         why="SCL pull-up for the sensor bus, same reasoning as R6.",
         price_eur=0.02, supplier="Mouser"),
    Part(ref="LED1", value="RGB LED 5 mm diffused, common anode", mfr="Adafruit",
         mpn="159", footprint="THT 5 mm, 4 leads",
         pins={"A": "common anode", "R": "red cathode", "G": "green cathode",
               "B": "blue cathode"},
         why="The only user-facing output now that the sensor has no display: air "
             "quality at a glance, pairing mode, low battery. Anode on VBAT so green "
             "and blue have forward-voltage headroom; the cathodes are sunk by GPIOs. "
             "Shines through a 0.6 mm skin left in the front face, no hole.",
         price_eur=1.20, supplier="Adafruit / Berrybase",
         vsupply_min=0.0, vsupply_max=5.0),
    Part(ref="R8", value="1 k", mfr="Yageo", mpn="RC0603FR-071KL", footprint="0603",
         pins={"1": "a", "2": "b"},
         why="Red: (3.8 V - 2.0 V) / 1k = 1.8 mA.", price_eur=0.02, supplier="Mouser"),
    Part(ref="R9", value="330 R", mfr="Yageo", mpn="RC0603FR-07330RL", footprint="0603",
         pins={"1": "a", "2": "b"},
         why="Green: (3.8 V - 3.1 V) / 330R = 2.1 mA.", price_eur=0.02, supplier="Mouser"),
    Part(ref="R10", value="330 R", mfr="Yageo", mpn="RC0603FR-07330RL", footprint="0603",
         pins={"1": "a", "2": "b"},
         why="Blue: as green.", price_eur=0.02, supplier="Mouser"),
    Part(ref="J1", value="JST ZHR-5 cable", mfr="JST", mpn="ZHR-5 + SZH-002T-P0.5",
         footprint="1.5 mm pitch, 5 way", pins={"1": "VDD", "2": "RX", "3": "TX",
                                                "4": "SEL", "5": "GND"},
         why="Mating connector for the SPS30, per its datasheet.",
         price_eur=1.50, supplier="Mouser / Digi-Key"),
    Part(ref="BT1", value="LiPo 3.7 V 4000 mAh, protected, 606090", mfr="generic",
         mpn="606090 / PL-606090-4000 class",
         footprint="6.0 x 60 x 90 mm + PCM, JST PH 2.0 lead",
         pins={"+": "positive", "-": "negative"},
         why="Largest cell that fits a 98 x 102 mm case upright, with a sane ~24 h charge time "
             "at the Feather's 196 mA charger. Must include a PCM (over-charge, "
             "over-discharge, over-current and short-circuit protection).",
         price_eur=18.0, supplier="Eremit / AKKUparts / Adafruit equivalent",
         vsupply_min=3.0, vsupply_max=4.2),
    Part(ref="PCB1", value="AIR CHECK carrier board (ACC-1)", mfr="JLCPCB/Aisler",
         mpn="ACC-1 rev B", footprint="2-layer, 70 x 35 mm, 1.6 mm FR4",
         pins={}, why="Carries the boost, the two load switches, the button, the RGB "
                      "LED, the sensor-bus pull-ups and the SPS30 connector, and "
                      "holds the Feather.",
         price_eur=8.0, supplier="JLCPCB / Aisler / PCBWay"),
    # ---- optional ---------------------------------------------------------
    Part(ref="X1", value="M2.5 brass heat-set inserts (12x) + M2.5x8 screws",
         mfr="generic", mpn="M2.5 x 4.0 x 4.6 knurled",
         footprint="-", pins={}, why="Serviceable, re-openable enclosure.",
         price_eur=6.0, supplier="Amazon / Ruthex", qty=1),
    Part(ref="X2", value="acoustic foam / EPDM strip 2 mm", mfr="generic", mpn="-",
         footprint="-", pins={}, why="Sensirion recommends decoupling the SPS30 "
                                     "mechanically to stop the fan exciting the case.",
         price_eur=3.0, supplier="local", qty=1),
    Part(ref="X3", value="silicone wire 26 AWG + JST PH 2.0 pigtail", mfr="generic",
         mpn="-", footprint="-", pins={}, why="Battery lead and internal wiring.",
         price_eur=4.0, supplier="local", qty=1),
    Part(ref="X4", value="Nordic PPK II power profiler", mfr="Nordic", mpn="nRF-PPK2",
         footprint="-", pins={}, why="Only way to confirm the battery model on real "
                                     "hardware. Optional but strongly recommended.",
         price_eur=95.0, supplier="Mouser / Digi-Key", required=False),
]

PARTS_BY_REF = {p.ref: p for p in PARTS}

# --------------------------------------------------------------------------
# Nets  (net name -> [(ref, pin), ...])
# --------------------------------------------------------------------------

NETS: dict[str, list[tuple[str, str]]] = {
    # ---- power ------------------------------------------------------------
    "VBAT": [("M1", "BAT"), ("SW1", "1"), ("BT1", "+"), ("LED1", "A")],
    "GND": [("M1", "GND"), ("U1", "5"), ("U2", "GND"), ("U3", "GND"),
            ("U4", "3"), ("SW1", "2"), ("SW2", "2"),
            ("C1", "2"), ("C2", "2"), ("C3", "2"), ("C4", "2"), ("C6", "2"),
            ("R5", "2"), ("SW4", "2"), ("J1", "5"), ("BT1", "-")],
    "VBOOST_IN": [("SW1", "5"), ("SW1", "6"), ("U4", "1"), ("U4", "2"), ("C1", "1")],
    "+5V": [("U4", "6"), ("C2", "1"), ("C3", "1"), ("J1", "1")],
    "SW_NODE": [("U4", "5"), ("L1", "2")],
    "L1_IN": [("L1", "1")],          # tied to VBOOST_IN below, see ERC note
    "+3V3": [("M1", "3V"), ("SW2", "1"), ("R3", "1")],
    "+3V3_SENS": [("SW2", "5"), ("SW2", "6"), ("U2", "VIN"), ("U3", "VIN"),
                  ("C4", "1"), ("R5", "1"), ("R6", "1"), ("R7", "1")],
    # ---- control ----------------------------------------------------------
    "EN_SPS30_5V": [("M1", "A5"), ("SW1", "3")],          # GPIO2, RTC
    "EN_SENS_3V3": [("M1", "A4"), ("SW2", "3")],          # GPIO3, RTC
    # ---- SPS30 over UART --------------------------------------------------
    "SPS30_RX": [("M1", "TX"), ("R1", "1")],              # GPIO16 -> sensor RX
    "SPS30_RX_S": [("R1", "2"), ("J1", "2")],
    "SPS30_TX": [("M1", "RX"), ("R2", "1")],              # GPIO17 <- sensor TX
    "SPS30_TX_S": [("R2", "2"), ("J1", "3")],
    "SPS30_SEL": [("J1", "4")],                           # left floating = UART mode
    # ---- sensor I2C bus: LP_I2C, fixed pads GPIO6/GPIO7 on the C6 ---------
    "SENS_SDA": [("M1", "A2"), ("U2", "SDA"), ("U3", "SDA"), ("R6", "2")],   # GPIO6
    "SENS_SCL": [("M1", "D9"), ("U2", "SCL"), ("U3", "SCL"), ("R7", "2")],   # GPIO7
    # ---- status LED, common anode on VBAT, cathodes sunk by GPIOs ---------
    "LED_R": [("M1", "SCK"), ("R8", "1")],                # GPIO21
    "LED_R_K": [("R8", "2"), ("LED1", "R")],
    "LED_G": [("M1", "MOSI"), ("R9", "1")],               # GPIO22
    "LED_G_K": [("R9", "2"), ("LED1", "G")],
    "LED_B": [("M1", "MISO"), ("R10", "1")],              # GPIO23
    "LED_B_K": [("R10", "2"), ("LED1", "B")],
    # ---- button -----------------------------------------------------------
    "BTN": [("M1", "A0"), ("R3", "2"), ("SW4", "1"), ("C6", "1")],  # GPIO1, RTC wake
}

# A few nets are shorted on the board; declare them so the ERC does not complain.
NET_ALIASES = [("L1_IN", "VBOOST_IN")]

# Which rail each net belongs to (for the voltage-compatibility check)
NET_RAIL = {
    "VBAT": "VBAT", "GND": "GND", "VBOOST_IN": "VBOOST_IN", "+5V": "+5V",
    "+3V3": "+3V3", "+3V3_SENS": "+3V3_SENS",
}

# --------------------------------------------------------------------------
# GPIO map
# --------------------------------------------------------------------------

@dataclass
class GpioUse:
    gpio: int
    feather_pin: str
    signal: str
    direction: str
    rtc_capable: bool
    strapping: bool
    note: str

# ESP32-C6: RTC/LP GPIOs are GPIO0..GPIO7.  Strapping pins are GPIO4, GPIO5,
# GPIO8, GPIO9 and GPIO15 (ESP32-C6 Technical Reference Manual / datasheet).
C6_RTC_GPIO = set(range(0, 8))
C6_STRAPPING = {4, 5, 8, 9, 15}

GPIO_MAP = [
    GpioUse(1,  "A0",   "BTN",         "in",  True,  False, "EXT1 deep-sleep wake, active low"),
    GpioUse(2,  "A5",   "EN_SPS30_5V", "out", True,  False, "held LOW in deep sleep"),
    GpioUse(3,  "A4",   "EN_SENS_3V3", "out", True,  False, "held HIGH in deep sleep"),
    GpioUse(6,  "A2/D6", "SENS_SDA",   "bidir", True, False,
            "LP_I2C SDA - a fixed IO_MUX pad on the C6, not remappable"),
    GpioUse(7,  "D9",   "SENS_SCL",    "bidir", True, False,
            "LP_I2C SCL - a fixed IO_MUX pad on the C6, not remappable"),
    GpioUse(16, "TX",   "SPS30_RX",    "out", False, False, "UART1 TX, Hi-Z in deep sleep"),
    GpioUse(17, "RX",   "SPS30_TX",    "in",  False, False, "UART1 RX, pull-up disabled"),
    GpioUse(18, "SCL",  "GAUGE_SCL",   "bidir", False, False,
            "Feather-internal bus to the MAX17048; pull-up only while GPIO20 is high"),
    GpioUse(19, "SDA",  "GAUGE_SDA",   "bidir", False, False,
            "Feather-internal bus to the MAX17048; pull-up only while GPIO20 is high"),
    GpioUse(20, "-",    "I2C_PWR",     "out", False, False,
            "Feather VSENSOR LDO: gauge-bus pull-ups AND the WS2812B. Pulsed high "
            "for ~50 ms per battery read, low otherwise"),
    GpioUse(21, "SCK",  "LED_R",       "out", False, False, "sink, active low"),
    GpioUse(22, "MOSI", "LED_G",       "out", False, False, "sink, active low"),
    GpioUse(23, "MISO", "LED_B",       "out", False, False, "sink, active low"),
]

RESERVED_GPIO = {
    0:  "free (was EPD_BUSY in v1.0)",
    14: "free (was EPD_CS in v1.0)",
    4:  "ESP32-C6 strapping pin (A1) - left unconnected",
    5:  "ESP32-C6 strapping pin (A3/D5) - left unconnected",
    8:  "ESP32-C6 strapping pin (D10) - left unconnected",
    9:  "ESP32-C6 strapping pin, BOOT button - not used by the application",
    15: "ESP32-C6 strapping pin, red LED (D13) - not used by the application",
    12: "native USB D-",
    13: "native USB D+",
}

# Two buses since v1.1 - see EDR-11.
I2C_BUS = {0x59: "SGP40 (sensor bus, LP_I2C, GPIO6/7)",
           0x62: "SCD41 (sensor bus, LP_I2C, GPIO6/7)"}
GAUGE_BUS = {0x36: "MAX17048 fuel gauge (Feather bus, GPIO19/18)",
             0x38: "AHT20, if fitted - it is on Adafruit's schematic (Feather bus)"}


# --------------------------------------------------------------------------
# ERC
# --------------------------------------------------------------------------

class Erc:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []
        self.checks = 0

    def check(self, ok: bool, msg: str, warn: bool = False) -> None:
        self.checks += 1
        if not ok:
            (self.warnings if warn else self.errors).append(msg)


def run_erc() -> Erc:
    e = Erc()
    alias = {a: b for a, b in NET_ALIASES}

    # 1. every pin referenced by a net must exist on that part
    for net, conns in NETS.items():
        for ref, pin in conns:
            e.check(ref in PARTS_BY_REF, f"net {net}: unknown part {ref}")
            p = PARTS_BY_REF.get(ref)
            if p and p.pins:
                e.check(pin in p.pins,
                        f"net {net}: {ref} has no pin '{pin}' (has {sorted(p.pins)})")

    # 2. no net may be left with a single connection (except documented stubs)
    single_ok = {"SPS30_SEL"}
    for net, conns in NETS.items():
        n = len(conns) + (1 if net in alias else 0)
        e.check(n >= 2 or net in single_ok,
                f"net {net} has only {len(conns)} connection(s)")

    # 3. no pin may appear on two different nets
    seen: dict[tuple[str, str], str] = {}
    for net, conns in NETS.items():
        for c in conns:
            prev = seen.get(c)
            e.check(prev is None,
                    f"pin {c[0]}.{c[1]} is on both '{prev}' and '{net}'")
            seen[c] = net

    # 4. supply-voltage compatibility for every powered part
    supply_of = {
        "U1": "+5V", "U2": "+3V3_SENS", "U3": "+3V3_SENS",
        "M1": "VBAT",
    }
    for ref, railname in supply_of.items():
        p = PARTS_BY_REF[ref]
        r = RAILS[railname]
        if p.vsupply_min is not None:
            e.check(p.vsupply_min <= r.vmin,
                    f"{ref} needs >= {p.vsupply_min} V but {railname} can fall to {r.vmin} V")
            e.check(p.vsupply_max >= r.vmax,
                    f"{ref} max supply {p.vsupply_max} V but {railname} can reach {r.vmax} V")

    # 5. logic-level compatibility on every signal that crosses a domain
    #    MCU drives 3.3 V CMOS.  SPS30 VIH(min) = 2.31 V, VOH(typ) = 3.3 V.
    e.check(2.31 <= 3.3 * 0.9,
            "SPS30 VIH(min) 2.31 V must be below the MCU VOH at 3.3 V")
    e.check(PARTS_BY_REF["U1"].io_vmax >= 3.6,
            "SPS30 I/O must tolerate the 3.3 V MCU rail")
    for ref in ("U2", "U3"):
        e.check(PARTS_BY_REF[ref].io_vmax >= 3.4,
                f"{ref} I/O must tolerate the +3V3 rail at its maximum")

    # 6. I2C address uniqueness, per bus
    addrs = [p.i2c_addr for p in PARTS if p.i2c_addr is not None]
    e.check(len(addrs) == len(set(addrs)), f"duplicate I2C address in {addrs}")
    e.check(set(addrs) == set(I2C_BUS), "sensor-bus table and parts disagree")
    for a in list(I2C_BUS) + list(GAUGE_BUS):
        e.check(0x08 <= a <= 0x77, f"I2C address 0x{a:02x} outside the 7-bit range")

    # 7. I2C pull-ups: exactly one pair per bus, and on the rail that powers
    #    the devices on that bus, so a switched-off rail leaves no pull-up
    #    feeding unpowered sensors.
    sda = NETS["SENS_SDA"]; scl = NETS["SENS_SCL"]
    pu_sda = [r for r, _ in sda if r.startswith("R")]
    pu_scl = [r for r, _ in scl if r.startswith("R")]
    e.check(len(pu_sda) == 1 and len(pu_scl) == 1,
            f"sensor bus needs exactly one pull-up per line, has {pu_sda} / {pu_scl}")
    for r in pu_sda + pu_scl:
        other = [n for n, c in NETS.items() if (r, "1") in c]
        e.check(other == ["+3V3_SENS"],
                f"{r} must pull up to +3V3_SENS, not {other}")
    # the C6's LP_I2C is hard-wired to GPIO6 (SDA) and GPIO7 (SCL)
    gm = {g.signal: g.gpio for g in GPIO_MAP}
    e.check(gm.get("SENS_SDA") == 6 and gm.get("SENS_SCL") == 7,
            "LP_I2C on the ESP32-C6 only exists on GPIO6 (SDA) / GPIO7 (SCL)")

    # 8. GPIO usage
    used = {}
    for g in GPIO_MAP:
        e.check(g.gpio not in used,
                f"GPIO{g.gpio} used twice: {used.get(g.gpio)} and {g.signal}")
        used[g.gpio] = g.signal
        e.check(g.gpio not in C6_STRAPPING,
                f"GPIO{g.gpio} ({g.signal}) is an ESP32-C6 strapping pin")
        e.check(g.rtc_capable == (g.gpio in C6_RTC_GPIO),
                f"GPIO{g.gpio} RTC capability declared {g.rtc_capable} but "
                f"ESP32-C6 RTC GPIOs are 0..7")
    for g in GPIO_MAP:
        if "held" in g.note or "wake" in g.note:
            e.check(g.gpio in C6_RTC_GPIO,
                    f"GPIO{g.gpio} ({g.signal}) must be an RTC GPIO to hold/wake in deep sleep")
    for gpio in RESERVED_GPIO:
        e.check(gpio not in used,
                f"GPIO{gpio} is reserved ({RESERVED_GPIO[gpio]}) but used for {used.get(gpio)}")

    # 9. the boost must be able to start the SPS30 fan
    boost_max = PARTS_BY_REF["U4"].i_max_ma
    e.check(boost_max >= 3 * PARTS_BY_REF["U1"].i_max_ma,
            f"boost {boost_max} mA is not comfortably above the SPS30 fan start "
            f"{PARTS_BY_REF['U1'].i_max_ma} mA")

    # 10. charger vs cell: C-rate must be safe
    i_charge_ma = 1000.0 / 5.1e3 * 1e3      # MCP73831: I = 1000 V / R_PROG
    cell_mah = 4000.0
    e.check(i_charge_ma <= cell_mah,        # <= 1C
            f"charge current {i_charge_ma:.0f} mA exceeds 1C for a {cell_mah:.0f} mAh cell")
    e.check(i_charge_ma >= cell_mah / 40,
            f"charge current {i_charge_ma:.0f} mA gives an unusable charge time", warn=True)

    # 11. every load switch is driven by an RTC GPIO so it holds state in sleep
    for sw, net in (("SW1", "EN_SPS30_5V"), ("SW2", "EN_SENS_3V3")):
        gp = [g for g in GPIO_MAP if g.signal == net]
        e.check(len(gp) == 1 and gp[0].gpio in C6_RTC_GPIO,
                f"{sw} enable net {net} is not on an RTC GPIO")

    # 12. everything on a switched rail must be reachable by a switch
    for ref in ("U2", "U3"):
        e.check(("+3V3_SENS" in [n for n, c in NETS.items()
                                 if (ref, "VIN") in c]),
                f"{ref} is not on the switched sensor rail")

    return e


# --------------------------------------------------------------------------
# Emitters
# --------------------------------------------------------------------------

def kicad_netlist() -> str:
    out = io.StringIO()
    out.write("(export (version D)\n")
    out.write('  (design (source "electronics/schematic/design.py")\n')
    out.write('          (tool "AIR CHECK design.py"))\n')
    out.write("  (components\n")
    for p in PARTS:
        if not p.pins:
            continue
        out.write(f'    (comp (ref "{p.ref}") (value "{p.value}")\n')
        out.write(f'      (footprint "{p.footprint}")\n')
        out.write(f'      (fields (field (name "MPN") "{p.mpn}") '
                  f'(field (name "MFR") "{p.mfr}")))\n')
    out.write("  )\n  (nets\n")
    for i, (net, conns) in enumerate(NETS.items(), start=1):
        out.write(f'    (net (code "{i}") (name "{net}")\n')
        for ref, pin in conns:
            out.write(f'      (node (ref "{ref}") (pin "{pin}"))\n')
        out.write("    )\n")
    out.write("  )\n)\n")
    return out.getvalue()


def netlist_markdown() -> str:
    o = []
    w = o.append
    w("<!-- GENERATED by electronics/schematic/design.py -->")
    w("")
    w("# AIR CHECK - net list and pin map")
    w("")
    w("## Power tree")
    w("")
    w("```")
    w("USB-C (Feather)")
    w("  |-- MCP73831T-2ACI/OT  R_PROG = 5k1 -> 196 mA charge")
    w("  |     `--> VBAT  ---- JST PH 2.0 ---- 1S LiPo 4000 mAh with PCM")
    w("  `-- RT9080/AP2112 LDO --> +3V3 (always on)")
    w("")
    w("VBAT --[SW1 TPS22918, GPIO2]--> VBOOST_IN --[TPS61023 + L1]--> +5V --> SPS30")
    w("+3V3 --[SW2 TPS22918, GPIO3]--> +3V3_SENS --> SGP40, SCD41, sensor-bus pull-ups")
    w("+3V3 ------------------------->  ESP32-C6, button pull-up")
    w("+3V3 --[Feather LDO U5, GPIO20]--> VSENSOR --> gauge-bus pull-ups, WS2812B (Feather)")
    w("VBAT ------------------------->  MAX17048 (Feather), status LED anode")
    w("```")
    w("")
    w("## Rails")
    w("")
    w("| rail | min | nom | max | source |")
    w("|---|---|---|---|---|")
    for r in RAILS.values():
        w(f"| {r.name} | {r.vmin:.2f} V | {r.vnom:.2f} V | {r.vmax:.2f} V | {r.source} |")
    w("")
    w("## GPIO map (ESP32-C6 on the Adafruit Feather)")
    w("")
    w("| GPIO | Feather pin | signal | dir | RTC | note |")
    w("|---|---|---|---|---|---|")
    for g in sorted(GPIO_MAP, key=lambda x: x.gpio):
        w(f"| {g.gpio} | {g.feather_pin} | {g.signal} | {g.direction} | "
          f"{'yes' if g.rtc_capable else 'no'} | {g.note} |")
    w("")
    w("Reserved / not used by the application:")
    w("")
    w("| GPIO | reason |")
    w("|---|---|")
    for gpio, why in sorted(RESERVED_GPIO.items()):
        w(f"| {gpio} | {why} |")
    w("")
    w("## I2C buses")
    w("")
    w("**Sensor bus** - LP_I2C, GPIO6 = SDA, GPIO7 = SCL (fixed pads), 4.7k pull-ups "
      "to +3V3_SENS on the carrier, 100 kHz:")
    w("")
    w("| address | device |")
    w("|---|---|")
    for a, d in sorted(I2C_BUS.items()):
        w(f"| 0x{a:02X} | {d} |")
    w("")
    w("**Gauge bus** - HP I2C, GPIO19 = SDA, GPIO18 = SCL, the Feather's own 10k "
      "pull-ups on VSENSOR. Only usable while GPIO20 is high:")
    w("")
    w("| address | device |")
    w("|---|---|")
    for a, d in sorted(GAUGE_BUS.items()):
        w(f"| 0x{a:02X} | {d} |")
    w("")
    w("Why two buses: on the Feather, the I2C pull-ups and the WS2812B share one "
      "switched LDO. Keeping it on for the sensors would also keep the WS2812B "
      "powered, and a WS2812B idles at around a milliamp - more than the whole "
      "radio. So the sensors get their own bus with their own pull-ups, and the "
      "Feather's LDO is only switched on for the few milliseconds a battery read "
      "takes.")
    w("")
    w("The SPS30 is **not** on this bus. It uses its UART (SHDLC) interface, which "
      "Sensirion recommends for cabled connections, and which leaves no pull-up "
      "path into the sensor while its 5 V rail is switched off.")
    w("")
    w("## Nets")
    w("")
    w("| net | connections |")
    w("|---|---|")
    for net, conns in NETS.items():
        s = ", ".join(f"{r}.{p}" for r, p in conns)
        w(f"| `{net}` | {s} |")
    w("")
    return "\n".join(o)


def bom_rows(include_optional: bool = True) -> list[dict]:
    rows = []
    for p in PARTS:
        if not include_optional and not p.required:
            continue
        rows.append({
            "ref": p.ref, "qty": p.qty, "value": p.value,
            "manufacturer": p.mfr, "mpn": p.mpn, "footprint": p.footprint,
            "unit_price_eur": f"{p.price_eur:.2f}",
            "line_total_eur": f"{p.price_eur * p.qty:.2f}",
            "supplier": p.supplier,
            "required": "required" if p.required else "optional",
            "reason": p.why.replace("\n", " "),
        })
    return rows


def total_cost(required_only: bool = True) -> float:
    return sum(p.price_eur * p.qty for p in PARTS
               if p.required or not required_only)


# --------------------------------------------------------------------------
# Build variants
# --------------------------------------------------------------------------

@dataclass
class Variant:
    key: str
    title: str
    blurb: str
    swaps: dict          # ref -> (value, mpn, price) or None to drop the part
    extra: list          # list of (description, mpn, price)

VARIANTS = [
    Variant(
        "budget", "BUDGET BUILD - no CO2",
        "Everything except the SCD41. PM, VOC, temperature and humidity still work, "
        "and the firmware runs unchanged (the CO2 endpoint is simply not created). "
        "This is the only way the electronics land under EUR 150 without cutting "
        "into sensor quality, so that is the compromise we make: fewer sensors, not "
        "cheaper ones. Temperature and humidity come from the SGP40's companion "
        "readings being unavailable, so a EUR 5 SHT40 is added instead.",
        swaps={"U3": None, "X4": None},
        extra=[("SHT40 temperature/humidity breakout (Adafruit 4885)", "4885", 8.0)],
    ),
    Variant(
        "recommended", "RECOMMENDED BUILD",
        "Breakout modules on a carrier board. No fine-pitch soldering anywhere: the "
        "smallest thing you hand-solder is a 0603 resistor, and even those are "
        "optional if you order the carrier board assembled. This is the build the "
        "assembly guide and the CAD are drawn around.",
        swaps={"X4": None},
        extra=[],
    ),
    Variant(
        "custom_pcb", "CUSTOM PCB BUILD",
        "Bare sensors reflowed onto the carrier board instead of breakouts. About "
        "8 mm thinner and EUR 25 cheaper, at the price of needing a stencil, paste "
        "and a hot plate. Same schematic, same firmware, same enclosure - the "
        "enclosure's sensor bay has the clearance for both.",
        swaps={
            "U2": ("SGP40 (bare DFN)", "SGP40-D-R4", 7.5),
            "U3": ("SCD41 (bare LGA)", "SCD41-D-R2", 32.0),
            "X4": None,
        },
        extra=[("SMD stencil for ACC-1", "-", 8.0)],
    ),
]


def variant_bom(v: Variant) -> tuple[list[dict], float]:
    rows, total = [], 0.0
    for p in PARTS:
        if p.ref in v.swaps and v.swaps[p.ref] is None:
            continue
        value, mpn, price = p.value, p.mpn, p.price_eur
        if p.ref in v.swaps and v.swaps[p.ref]:
            value, mpn, price = v.swaps[p.ref]
        if not p.required and p.ref not in v.swaps:
            continue
        rows.append({"ref": p.ref, "qty": p.qty, "value": value, "mpn": mpn,
                     "mfr": p.mfr, "price": price, "supplier": p.supplier,
                     "reason": p.why})
        total += price * p.qty
    for desc, mpn, price in v.extra:
        rows.append({"ref": "-", "qty": 1, "value": desc, "mpn": mpn, "mfr": "-",
                     "price": price, "supplier": "-", "reason": "variant-specific"})
        total += price
    return rows, total


def bom_markdown() -> str:
    o = []
    w = o.append
    w("<!-- GENERATED by electronics/schematic/design.py - do not edit by hand. -->")
    w("")
    w("# Bill of materials")
    w("")
    w("Prices are single-unit European retail including VAT, rounded, as of "
      "September 2026. They will drift; treat them as an order of magnitude, not a "
      "quotation. Every line names a real manufacturer part number - nothing here "
      "is a generic placeholder.")
    w("")
    w("## Cost summary")
    w("")
    w("| build | electronics total | what you give up |")
    w("|---|---|---|")
    for v in VARIANTS:
        _, t = variant_bom(v)
        give = {"budget": "CO2 measurement",
                "recommended": "nothing; ~8 mm thicker than the SMD build",
                "custom_pcb": "needs reflow equipment"}[v.key]
        w(f"| {v.title} | **EUR {t:.2f}** | {give} |")
    w("")
    w("### On the EUR 150 target")
    w("")
    w("The brief asks for roughly EUR 150 of electronics. With all four sensing")
    w("channels that target is not reachable at single-unit retail: the SPS30 alone")
    w("is about EUR 38 and the SCD41 about EUR 32-52 depending on how you buy it.")
    w("Rather than substitute a worse particle or CO2 sensor - which the brief")
    w("explicitly forbids - the BUDGET build drops the CO2 channel entirely and")
    w("keeps every remaining part at full quality. If you want CO2, the honest")
    w("number is around EUR 165 (custom PCB) to EUR 190 (breakouts).")
    w("")
    for v in VARIANTS:
        rows, t = variant_bom(v)
        w(f"## {v.title}")
        w("")
        w(v.blurb)
        w("")
        w("| ref | qty | part | MPN | supplier | EUR | why this part |")
        w("|---|---|---|---|---|---|---|")
        for r in rows:
            w(f"| {r['ref']} | {r['qty']} | {r['value']} | `{r['mpn']}` | "
              f"{r['supplier']} | {r['price']:.2f} | {r['reason']} |")
        w(f"| | | | | | **{t:.2f}** | |")
        w("")
    w("## Optional but recommended")
    w("")
    w("| part | MPN | EUR | why |")
    w("|---|---|---|---|")
    for p in PARTS:
        if not p.required:
            w(f"| {p.value} | `{p.mpn}` | {p.price_eur:.2f} | {p.why} |")
    w("")
    w("## Printed parts and hardware")
    w("")
    w("See `manufacturing/print-settings.md` for filament quantities. You additionally")
    w("need 12 M2.5 brass heat-set inserts, 12 M2.5 x 8 mm screws and a short length")
    w("of 2 mm EPDM or acoustic foam.")
    w("")
    return "\n".join(o)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit", action="store_true", help="write the generated files")
    a = ap.parse_args()

    e = run_erc()
    print(f"ERC: {e.checks} checks, {len(e.errors)} error(s), {len(e.warnings)} warning(s)")
    for w in e.warnings:
        print(f"  WARN  {w}")
    for err in e.errors:
        print(f"  ERROR {err}")

    if a.emit and not e.errors:
        with open(os.path.join(ROOT, "electronics/schematic/aircheck.net"), "w") as f:
            f.write(kicad_netlist())
        with open(os.path.join(ROOT, "electronics/schematic/NETLIST.md"), "w") as f:
            f.write(netlist_markdown())
        with open(os.path.join(ROOT, "electronics/bom/bom.csv"), "w", newline="") as f:
            rows = bom_rows()
            wtr = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            wtr.writeheader()
            wtr.writerows(rows)
        with open(os.path.join(ROOT, "docs/BOM.md"), "w") as f:
            f.write(bom_markdown())
        print("wrote aircheck.net, NETLIST.md, bom.csv, docs/BOM.md")
        print(f"required BOM total: EUR {total_cost(True):.2f}")

    return 1 if e.errors else 0


if __name__ == "__main__":
    sys.exit(main())
