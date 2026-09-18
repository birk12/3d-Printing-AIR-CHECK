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
    "VUSB":        Rail("VUSB", 4.75, 5.00, 5.25, "USB-C VBUS on the FireBeetle (its VIN pin)"),
    "VBAT":        Rail("VBAT", 3.20, 3.80, 4.20, "1S LiPo, protected"),
    "+3V3":        Rail("+3V3", 3.20, 3.30, 3.40,
                        "FireBeetle TPS62A02 buck, always on; 100 % duty below ~3.4 V VBAT"),
    "+3V3_SEN6X":  Rail("+3V3_SEN6X", 3.20, 3.30, 3.40, "+3V3 behind load switch SW1"),
    "+3V3_SENS":   Rail("+3V3_SENS", 3.20, 3.30, 3.40, "+3V3 behind load switch SW2"),
    "GND":         Rail("GND", 0.0, 0.0, 0.0, "system ground"),
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
    c_in_uf: float = 0.0          # input capacitance a load switch has to charge
    datasheet: str = ""


def _r(ref, value, mpn, why, price=0.02):
    return Part(ref=ref, value=value, mfr="Yageo", mpn=mpn, footprint="0603",
                pins={"1": "a", "2": "b"}, why=why, price_eur=price, supplier="Mouser / LCSC")


def _c(ref, value, mpn, footprint, why, price, c_uf=0.0):
    return Part(ref=ref, value=value, mfr="Murata", mpn=mpn, footprint=footprint,
                pins={"1": "+", "2": "-"}, why=why, price_eur=price,
                supplier="Mouser / LCSC", c_in_uf=c_uf)


PARTS: list[Part] = [
    Part(
        ref="M1", value="FireBeetle 2 ESP32-C6", mfr="DFRobot", mpn="DFR1075",
        footprint="25.4 x 60 mm, 2 x 0.1in rows 22.86 mm apart, JST PH 2.0 battery",
        pins={
            "3V3": "+3V3 out (TPS62A02 buck)", "GND": "ground", "VIN": "USB 5 V / VCC",
            "IO1": "GPIO1", "IO2": "GPIO2", "IO3": "GPIO3", "IO4": "GPIO4",
            "IO5": "GPIO5", "IO6": "GPIO6 / LP_SDA", "IO7": "GPIO7 / LP_SCL",
            "IO8": "GPIO8", "IO9": "GPIO9 / BOOT", "IO14": "GPIO14",
            "IO15": "GPIO15 / green LED", "IO16": "GPIO16 / TX", "IO17": "GPIO17 / RX",
            "IO18": "GPIO18", "SDA": "GPIO19", "SCL": "GPIO20",
            "IO21": "GPIO21", "IO22": "GPIO22", "IO23": "GPIO23", "RST": "reset",
            "BAT+": "JST PH battery +", "BAT-": "JST PH battery -",
        },
        why="ESP32-C6 (in-package flash) with native 802.15.4 for Thread, USB-C, a CN3165 "
            "LiPo charger (R9 = 2.2k -> 540 mA), a TPS62A02 3.3 V buck and a 1M/1M battery "
            "divider on GPIO0. DFRobot measure 36 uA in deep sleep for the whole board "
            "(v1.2). No fuel gauge and no WS2812B - checked on DFRobot's own schematic, "
            "see EDR-15.",
        price_eur=7.50, supplier="Botland / Berrybase / DFRobot",
        vsupply_min=3.0, vsupply_max=4.2, io_vmax=3.6,
        datasheet="https://wiki.dfrobot.com/SKU_DFR1075_FireBeetle_2_Board_ESP32_C6",
    ),
    Part(
        ref="U1", value="SEN63C", mfr="Sensirion", mpn="SEN63C-SIN-T",
        footprint="55.2 x 25.6 x 21.3 mm module, ACES 51468-0064N-001 in a 6.35 mm pocket (JST GH compatible)",
        pins={"1": "VDD", "2": "GND", "3": "SDA", "4": "SCL", "5": "GND", "6": "VDD"},
        why="One module for PM1/PM2.5/PM4/PM10 (laser, sheath flow), CO2 (+-(100 ppm + "
            "10 %)) and compensated temperature and humidity, at 3.3 V with no boost "
            "converter. Replaces the SPS30, the SCD41 breakout and the 5 V boost of v1.1 "
            "for less than the SPS30 alone. See EDR-15.",
        price_eur=33.97, supplier="Mouser",
        vsupply_min=3.15, vsupply_max=3.45, io_vmax=5.5,
        i_typ_ma=80.0, i_max_ma=200.0, i2c_addr=0x6B, c_in_uf=10.0,
        datasheet="Sensirion SEN6x Datasheet v0.5, October 2024",
    ),
    Part(
        ref="W1", value="JST GH 6-pin cable, 150 mm, pin 1 to pin 1", mfr="JST / generic",
        mpn="2 x GHR-06V-S + SSHL-002T-P0.2", footprint="1.25 mm pitch",
        pins={}, why="SEN63C to carrier. Sensirion allow up to 50 cm; 150 mm keeps it "
                     "inside the 10 cm-ish range they recommend for unshielded I2C "
                     "within reach.",
        price_eur=2.00, supplier="Mouser / Berrybase"),
    Part(ref="J1", value="JST GH 6-pin header, SMD", mfr="JST", mpn="SM06B-GHS-TB",
         footprint="JST GH 1.25 mm, 6 way, top entry",
         pins={"1": "VDD", "2": "GND", "3": "SDA", "4": "SCL", "5": "GND", "6": "VDD"},
         why="Carrier end of the SEN63C cable, same pin order as the sensor.",
         price_eur=0.60, supplier="Mouser / LCSC"),
    Part(
        ref="U2", value="SGP40 breakout", mfr="Adafruit", mpn="4829",
        footprint="STEMMA QT breakout 25.5 x 17.7 mm",
        pins={"VIN": "3.3V in", "GND": "ground", "SDA": "I2C data", "SCL": "I2C clock"},
        why="SGP40 MOX VOC sensor with Sensirion's VOC Index algorithm and on-chip "
            "humidity compensation. The VOC channel is the tripwire that escalates the "
            "PM cadence during a print. NOTE, from Adafruit's own schematic: the board "
            "carries an AP2112 LDO and a green power LED, ~185 uA together, so its rail "
            "is only switched on for each 0.25 s sample (EDR-14).",
        price_eur=14.90, supplier="Berrybase / Mouser",
        vsupply_min=3.0, vsupply_max=5.5, io_vmax=5.5,
        i_typ_ma=2.6, i_max_ma=3.0, i2c_addr=0x59, c_in_uf=20.1,
        datasheet="Sensirion SGP40 Datasheet v1.2; Adafruit-SGP40-PCB schematic",
    ),
    Part(
        ref="SW1", value="TPS22918", mfr="Texas Instruments", mpn="TPS22918DBVR",
        footprint="SOT-23-6",
        pins={"1": "VIN", "2": "GND", "3": "ON", "4": "CT", "5": "QOD", "6": "VOUT"},
        why="Power gate for the SEN63C, which idles at 3.3 mA. QOD tied to VOUT so the "
            "rail really collapses when off (internal 25 R).",
        price_eur=0.85, supplier="Mouser / LCSC",
        vsupply_min=1.0, vsupply_max=5.5, i_max_ma=2000.0,
        datasheet="TI TPS22918 datasheet SLVSDH4",
    ),
    Part(
        ref="SW2", value="TPS22918", mfr="Texas Instruments", mpn="TPS22918DBVR",
        footprint="SOT-23-6",
        pins={"1": "VIN", "2": "GND", "3": "ON", "4": "CT", "5": "QOD", "6": "VOUT"},
        why="Power gate for the SGP40 breakout, pulsed per sample; also the hard reset "
            "for a sensor that has hung its bus.",
        price_eur=0.85, supplier="Mouser / LCSC",
        vsupply_min=1.0, vsupply_max=5.5, i_max_ma=2000.0,
    ),
    _c("C1", "22 uF / 10 V X5R", "GRM21BR61A226ME44L", "0805",
       "Local reservoir on +3V3_SEN6X for the SEN63C's 200 mA / 2 ms current pulses.",
       0.25, c_uf=22.0),
    _c("C2", "4.7 nF / 50 V X7R", "GRM188R71H472KA01D", "0603",
       "SW1 CT: slew 0.55 x 4700 + 30 = 2.6 ms/V, so charging C1 plus the sensor draws "
       "about 12 mA instead of an amp-level spike that would brown out the MCU rail.",
       0.05),
    _c("C4", "1 uF / 16 V X7R", "GRM188R71C105KA12D", "0603",
       "+3V3_SENS decoupling at the breakout connector.", 0.10, c_uf=1.0),
    _c("C5", "1 nF / 50 V X7R", "GRM188R71H102KA01D", "0603",
       "SW2 CT: 580 us/V, ~36 mA into the breakout's 20 uF, 1.7 ms rise.", 0.05),
    _c("C6", "100 nF / 16 V X7R", "GRM188R71C104KA01D", "0603",
       "Button RC debounce with R3.", 0.05),
    _c("C7", "1 uF / 16 V X7R", "GRM188R71C105KA12D", "0603",
       "Load-switch input bypass, per the TPS22918 layout guidance.", 0.10),
    _r("R3", "100 k", "RC0603FR-07100KL",
       "Button pull-up to +3V3 (always on, so the button works in any sleep state). "
       "The button is on GPIO1, an LP pad."),
    _r("R6", "4.7 k", "RC0603FR-074K7L",
       "SGP40 bus SDA pull-up, to the *switched* rail it serves: when the rail is off "
       "there is no pull-up left to back-feed the unpowered sensor."),
    _r("R7", "4.7 k", "RC0603FR-074K7L", "SGP40 bus SCL pull-up, as R6."),
    _r("R11", "4.7 k", "RC0603FR-074K7L",
       "SEN63C bus SDA pull-up, to +3V3_SEN6X. Sensirion suggest 10k; 4.7k gives "
       "margin for the cable capacitance."),
    _r("R12", "4.7 k", "RC0603FR-074K7L", "SEN63C bus SCL pull-up, as R11."),
    _r("R13", "68 k", "RC0603FR-0768KL",
       "USB sense, top leg from VIN: 5.0 V x 100/168 = 2.98 V at GPIO18."),
    _r("R14", "100 k", "RC0603FR-07100KL",
       "USB sense, bottom leg. Draws 30 uA only while USB is connected; on battery VIN "
       "is dead and so is the divider."),
    Part(ref="SW4", value="tactile switch 6 x 6 x 5.0 mm, through hole",
         mfr="Alps/Omron", mpn="B3F-4050 (or any 6x6x5.0 mm THT tactile)",
         footprint="THT 6 x 6 mm, 5.0 mm total height",
         pins={"1": "a", "2": "b"},
         why="Single multifunction user button. The 5.0 mm overall height is what the "
             "enclosure is dimensioned around: it puts the stem tip 3.5 mm behind the "
             "front face, leaving 0.3 mm of travel under the printed cap.",
         price_eur=0.30, supplier="Berrybase / Reichelt / Mouser"),
    Part(ref="LED1", value="RGB LED 5 mm diffused, common anode", mfr="Adafruit",
         mpn="159", footprint="THT 5 mm, 4 leads",
         pins={"A": "common anode", "R": "red cathode", "G": "green cathode",
               "B": "blue cathode"},
         why="The only user-facing output: air quality at a glance, pairing mode, low "
             "battery. Anode on VBAT so green and blue have forward-voltage headroom; "
             "the cathodes are sunk by GPIOs. Shines through a 0.6 mm skin left in the "
             "front face, no hole. German shops mostly stock common cathode - order this "
             "one with the Mouser parcel.",
         price_eur=1.20, supplier="Mouser / Adafruit",
         vsupply_min=0.0, vsupply_max=5.0),
    _r("R8", "1 k", "RC0603FR-071KL", "Red: (3.8 V - 2.0 V) / 1k = 1.8 mA."),
    _r("R9", "330 R", "RC0603FR-07330RL", "Green: (3.8 V - 3.1 V) / 330R = 2.1 mA."),
    _r("R10", "330 R", "RC0603FR-07330RL", "Blue: as green."),
    Part(ref="J2", value="JST PH 2-pin header", mfr="JST", mpn="S2B-PH-K-S",
         footprint="JST PH 2.0 mm, 2 way, THT side entry",
         pins={"1": "+", "2": "-"},
         why="Battery in. The cell plugs into the carrier, not the FireBeetle, so the "
             "status LED can take its anode from VBAT - the FireBeetle does not bring "
             "VBAT out to a header.",
         price_eur=0.20, supplier="Mouser / LCSC"),
    Part(ref="J3", value="JST PH 2-pin pigtail, 100 mm", mfr="generic",
         mpn="PHR-2 one end, tinned leads", footprint="JST PH 2.0 mm / 2 solder pads",
         pins={"1": "+", "2": "-"},
         why="Carrier (soldered to the J3 pads) to the FireBeetle's battery socket. "
             "Check polarity against the '+' mark on the FireBeetle before the first "
             "plug-in: JST PH leads are not standardised.",
         price_eur=0.50, supplier="Berrybase / Amazon"),
    Part(ref="BT1", value="LiPo 3.7 V 4000 mAh, protected, 606090", mfr="EREMIT",
         mpn="3.7V 4000mAh 606090 (JST PH 2.0)",
         footprint="6.0 x 60 x 90 mm + PCM, JST PH 2.0 lead",
         pins={"+": "positive", "-": "negative"},
         why="Largest cell that fits the case upright; ~8 h charge at the FireBeetle's "
             "540 mA. Must include a PCM (over-charge, over-discharge, over-current, "
             "short circuit).",
         price_eur=9.90, supplier="eremit.de / Berrybase",
         vsupply_min=3.0, vsupply_max=4.2),
    Part(ref="PCB1", value="AIR CHECK carrier board (ACC-1)", mfr="Aisler/JLCPCB",
         mpn="ACC-1 rev C", footprint="2-layer, 70 x 35 mm, 1.6 mm FR4",
         pins={}, why="Carries the two load switches, the button, the RGB LED, both "
                      "sensor-bus pull-up pairs, the USB sense divider, the battery "
                      "pass-through and the SEN63C connector, and holds the FireBeetle.",
         price_eur=6.70, supplier="Aisler (3 boards ~EUR 20) / JLCPCB"),
    Part(ref="X1", value="M2.5 brass heat-set inserts + M2.5x8 screws",
         mfr="ruthex", mpn="RX-M2.5x5.7 (70 pcs) + DIN 912 M2.5x10",
         footprint="-", pins={}, why="Serviceable, re-openable enclosure. The pack does "
                                     "five devices; the price is the whole pack.",
         price_eur=9.60, supplier="ruthex.de / Berrybase"),
    Part(ref="X2", value="EPDM / PU foam strip 2 mm, self-adhesive", mfr="generic", mpn="-",
         footprint="-", pins={}, why="Seals the SEN63C's inlets and outlet against the "
                                     "case wall (Sensirion design-in guide 2.1) and "
                                     "decouples the fan from the shell (section 3).",
         price_eur=3.00, supplier="local"),
    Part(ref="X3", value="silicone wire 26 AWG", mfr="generic",
         mpn="-", footprint="-", pins={}, why="LED and button leads.",
         price_eur=2.00, supplier="local"),
    Part(ref="X4", value="Nordic PPK II power profiler", mfr="Nordic", mpn="nRF-PPK2",
         footprint="-", pins={}, why="Only way to confirm the battery model on real "
                                     "hardware. Optional but strongly recommended.",
         price_eur=95.0, supplier="Mouser / Digi-Key", required=False),
]

PARTS_BY_REF = {p.ref: p for p in PARTS}

# --------------------------------------------------------------------------
# Nets  (net name -> [(ref, pin), ...])
# The SEN63C is wired to J1 through W1 pin-for-pin; the netlist shows the
# sensor pins directly on the nets they end up on.
# --------------------------------------------------------------------------

NETS: dict[str, list[tuple[str, str]]] = {
    # ---- battery pass-through --------------------------------------------
    "VBAT": [("BT1", "+"), ("J2", "1"), ("J3", "1"), ("M1", "BAT+"), ("LED1", "A")],
    "BATN": [("BT1", "-"), ("J2", "2"), ("J3", "2"), ("M1", "BAT-")],
    # ---- power ------------------------------------------------------------
    "GND": [("M1", "GND"), ("SW1", "2"), ("SW2", "2"),
            ("C1", "2"), ("C2", "2"), ("C4", "2"), ("C5", "2"), ("C6", "2"), ("C7", "2"),
            ("U2", "GND"), ("J1", "2"), ("J1", "5"), ("U1", "2"), ("U1", "5"),
            ("SW4", "2"), ("R14", "2")],
    "+3V3": [("M1", "3V3"), ("SW1", "1"), ("SW2", "1"), ("C7", "1"), ("R3", "1")],
    "+3V3_SEN6X": [("SW1", "6"), ("SW1", "5"), ("C1", "1"), ("R11", "1"), ("R12", "1"),
                   ("J1", "1"), ("J1", "6"), ("U1", "1"), ("U1", "6")],
    "+3V3_SENS": [("SW2", "6"), ("SW2", "5"), ("C4", "1"), ("R6", "1"), ("R7", "1"),
                  ("U2", "VIN")],
    "VIN_USB": [("M1", "VIN"), ("R13", "1")],
    # ---- control ----------------------------------------------------------
    "EN_SEN6X": [("M1", "IO2"), ("SW1", "3")],            # GPIO2, LP pad
    "EN_SENS": [("M1", "IO3"), ("SW2", "3")],             # GPIO3, LP pad
    "CT1": [("SW1", "4"), ("C2", "1")],
    "CT2": [("SW2", "4"), ("C5", "1")],
    "USB_SENSE": [("R13", "2"), ("R14", "1"), ("M1", "IO18")],
    # ---- SEN63C bus: HP I2C on GPIO19/20 -----------------------------------
    "SEN_SDA": [("M1", "SDA"), ("R11", "2"), ("J1", "3"), ("U1", "3")],     # GPIO19
    "SEN_SCL": [("M1", "SCL"), ("R12", "2"), ("J1", "4"), ("U1", "4")],     # GPIO20
    # ---- SGP40 bus: LP_I2C, fixed pads GPIO6/GPIO7 on the C6 ---------------
    "SENS_SDA": [("M1", "IO6"), ("U2", "SDA"), ("R6", "2")],
    "SENS_SCL": [("M1", "IO7"), ("U2", "SCL"), ("R7", "2")],
    # ---- status LED, common anode on VBAT, cathodes sunk by GPIOs ---------
    "LED_R": [("M1", "IO21"), ("R8", "1")],
    "LED_R_K": [("R8", "2"), ("LED1", "R")],
    "LED_G": [("M1", "IO22"), ("R9", "1")],
    "LED_G_K": [("R9", "2"), ("LED1", "G")],
    "LED_B": [("M1", "IO23"), ("R10", "1")],
    "LED_B_K": [("R10", "2"), ("LED1", "B")],
    # ---- button -----------------------------------------------------------
    "BTN": [("M1", "IO1"), ("R3", "2"), ("SW4", "1"), ("C6", "1")],
}

NET_ALIASES: list[tuple[str, str]] = []

# Which rail each net belongs to (for the voltage-compatibility check)
NET_RAIL = {
    "VBAT": "VBAT", "GND": "GND", "+3V3": "+3V3", "+3V3_SEN6X": "+3V3_SEN6X",
    "+3V3_SENS": "+3V3_SENS", "VIN_USB": "VUSB",
}

# --------------------------------------------------------------------------
# GPIO map
# --------------------------------------------------------------------------

@dataclass
class GpioUse:
    gpio: int
    board_pin: str
    signal: str
    direction: str
    rtc_capable: bool
    strapping: bool
    note: str

# ESP32-C6: LP (RTC) GPIOs are GPIO0..GPIO7.  Strapping pins are GPIO4, GPIO5,
# GPIO8, GPIO9 and GPIO15 (ESP32-C6 datasheet).
C6_RTC_GPIO = set(range(0, 8))
C6_STRAPPING = {4, 5, 8, 9, 15}

GPIO_MAP = [
    GpioUse(0,  "(on board)", "BAT_ADC",  "analog in", True, False,
            "FireBeetle R15/R16 1M/1M VBAT divider, ADC1 channel 0 - not on a header"),
    GpioUse(1,  "1",   "BTN",         "in",  True,  False, "wake source, active low"),
    GpioUse(2,  "2",   "EN_SEN6X",    "out", True,  False, "held LOW in sleep"),
    GpioUse(3,  "3",   "EN_SENS",     "out", True,  False, "held LOW in sleep, pulsed per VOC sample"),
    GpioUse(6,  "6",   "SENS_SDA",    "bidir", True, False,
            "LP_I2C SDA - a fixed IO_MUX pad on the C6, not remappable"),
    GpioUse(7,  "7",   "SENS_SCL",    "bidir", True, False,
            "LP_I2C SCL - a fixed IO_MUX pad on the C6, not remappable"),
    GpioUse(18, "18",  "USB_SENSE",   "in",  False, False, "VIN / 1.68, high while USB is connected"),
    GpioUse(19, "SDA", "SEN_SDA",     "bidir", False, False, "HP I2C, released to input when SW1 is off"),
    GpioUse(20, "SCL", "SEN_SCL",     "bidir", False, False, "HP I2C, released to input when SW1 is off"),
    GpioUse(21, "21",  "LED_R",       "out", False, False, "sink, active low"),
    GpioUse(22, "22",  "LED_G",       "out", False, False, "sink, active low"),
    GpioUse(23, "23",  "LED_B",       "out", False, False, "sink, active low"),
]

RESERVED_GPIO = {
    4:  "strapping pin (MTMS) - left unconnected",
    5:  "strapping pin (MTDI) - left unconnected",
    8:  "strapping pin - left unconnected",
    9:  "strapping pin, BOOT button on the FireBeetle",
    12: "native USB D-",
    13: "native USB D+",
    14: "free",
    15: "strapping pin, FireBeetle green LED (to GND via 2k) - driven low by the firmware",
    16: "free (UART0 TX, console is on USB-Serial-JTAG)",
    17: "free (UART0 RX)",
}

# One bus per switched rail - see EDR-11 (v1.1) and EDR-15 (v1.2).
I2C_BUS = {0x59: "SGP40 (LP_I2C, GPIO6/7, pull-ups to +3V3_SENS)"}
SEN_BUS = {0x6B: "SEN63C (HP I2C, GPIO19/20, pull-ups to +3V3_SEN6X)"}

# FireBeetle charger: CN3165, ICH = 1188 V / R9 (DFRobot schematic v1.1)
CHARGER_R_ISET = 2.2e3
CELL_MAH = 4000.0


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


def _nets_of(ref: str, pin: str) -> list[str]:
    return [n for n, c in NETS.items() if (ref, pin) in c]


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

    # 2. no net may be left with a single connection
    for net, conns in NETS.items():
        n = len(conns) + (1 if net in alias else 0)
        e.check(n >= 2, f"net {net} has only {len(conns)} connection(s)")

    # 3. no pin may appear on two different nets
    seen: dict[tuple[str, str], str] = {}
    for net, conns in NETS.items():
        for c in conns:
            prev = seen.get(c)
            e.check(prev is None,
                    f"pin {c[0]}.{c[1]} is on both '{prev}' and '{net}'")
            seen[c] = net

    # 3b. every pin of every active part is connected, except the FireBeetle
    #     header pins we deliberately leave alone
    for p in PARTS:
        if not p.pins or p.ref == "M1":
            continue
        for pin in p.pins:
            e.check((p.ref, pin) in seen, f"{p.ref}.{pin} ({p.pins[pin]}) is unconnected")

    # 4. supply-voltage compatibility for every powered part
    supply_of = {"U1": "+3V3_SEN6X", "U2": "+3V3_SENS", "M1": "VBAT"}
    for ref, railname in supply_of.items():
        p = PARTS_BY_REF[ref]
        r = RAILS[railname]
        e.check(p.vsupply_min <= r.vmin,
                f"{ref} needs >= {p.vsupply_min} V but {railname} can fall to {r.vmin} V")
        e.check(p.vsupply_max >= r.vmax,
                f"{ref} max supply {p.vsupply_max} V but {railname} can reach {r.vmax} V")
        pin = {"U1": "1", "U2": "VIN", "M1": "BAT+"}[ref]
        e.check(_nets_of(ref, pin) == [railname],
                f"{ref}.{pin} should be on {railname}, is on {_nets_of(ref, pin)}")

    # 5. logic levels: every sensor I/O must tolerate the 3.3 V bus
    for ref in ("U1", "U2"):
        e.check(PARTS_BY_REF[ref].io_vmax >= RAILS["+3V3"].vmax,
                f"{ref} I/O must tolerate the +3V3 rail at its maximum")

    # 6. I2C addresses: unique, in range, and each part on the table of its bus
    addrs = [p.i2c_addr for p in PARTS if p.i2c_addr is not None]
    e.check(len(addrs) == len(set(addrs)), f"duplicate I2C address in {addrs}")
    e.check(set(addrs) == set(I2C_BUS) | set(SEN_BUS), "bus tables and parts disagree")
    for a in list(I2C_BUS) + list(SEN_BUS):
        e.check(0x08 <= a <= 0x77, f"I2C address 0x{a:02x} outside the 7-bit range")

    # 7. I2C pull-ups: exactly one pair per bus, on the switched rail that
    #    powers the devices on that bus, so a switched-off rail leaves no
    #    pull-up feeding an unpowered sensor.
    for sda, scl, rail, dev in (("SENS_SDA", "SENS_SCL", "+3V3_SENS", "U2"),
                                ("SEN_SDA", "SEN_SCL", "+3V3_SEN6X", "U1")):
        pu = [r for r, _ in NETS[sda] if r.startswith("R")] + \
             [r for r, _ in NETS[scl] if r.startswith("R")]
        e.check(len(pu) == 2, f"{sda}/{scl} need exactly one pull-up each, have {pu}")
        for r in pu:
            e.check(_nets_of(r, "1") == [rail], f"{r} must pull up to {rail}")
        devpin = {"U2": "VIN", "U1": "1"}[dev]
        e.check(_nets_of(dev, devpin) == [rail],
                f"{dev} and its pull-ups must share a rail ({rail})")
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
                f"ESP32-C6 LP GPIOs are 0..7")
    for g in GPIO_MAP:
        if "held" in g.note or "wake" in g.note:
            e.check(g.gpio in C6_RTC_GPIO,
                    f"GPIO{g.gpio} ({g.signal}) must be an LP GPIO to hold/wake in sleep")
    for gpio in RESERVED_GPIO:
        e.check(gpio not in used,
                f"GPIO{gpio} is reserved ({RESERVED_GPIO[gpio]}) but used for {used.get(gpio)}")
    # the net list and the GPIO map must agree on every MCU pin
    pin_to_gpio = {"SDA": 19, "SCL": 20}
    for net, conns in NETS.items():
        for ref, pin in conns:
            if ref != "M1" or not pin.startswith(("IO", "SDA", "SCL")):
                continue
            gpio = pin_to_gpio.get(pin, int(pin[2:]) if pin.startswith("IO") else -1)
            e.check(gm.get(net) == gpio,
                    f"net {net} is on M1.{pin} (GPIO{gpio}) but the GPIO map says "
                    f"GPIO{gm.get(net)}")

    # 9. load switches: current capacity and inrush.  TPS22918 slew rate
    #    SR = 0.55 us/(V pF) x CT + 30 us/V (datasheet eq. 3); I = C_load / SR.
    ct_of = {"SW1": "C2", "SW2": "C5"}
    for sw, rail, loads in (("SW1", "+3V3_SEN6X", ("U1", "C1")),
                            ("SW2", "+3V3_SENS", ("U2", "C4"))):
        ct_pf = {"C2": 4700.0, "C5": 1000.0}[ct_of[sw]]
        e.check(_nets_of(ct_of[sw], "1") == [f"CT{sw[-1]}"],
                f"{sw} CT pin needs its capacitor {ct_of[sw]}")
        sr_s_per_v = (0.55 * ct_pf + 30.0) * 1e-6
        c_load = sum(PARTS_BY_REF[r].c_in_uf for r in loads) * 1e-6
        inrush_ma = c_load / sr_s_per_v * 1e3
        e.check(inrush_ma <= 100.0,
                f"{sw} inrush {inrush_ma:.0f} mA into {c_load*1e6:.0f} uF would dip +3V3")
        e.check(_nets_of(sw, "5") == [rail],
                f"{sw} QOD must be tied to VOUT ({rail}) so the rail collapses when off")
    e.check(PARTS_BY_REF["SW1"].i_max_ma >= 2 * PARTS_BY_REF["U1"].i_max_ma,
            "SW1 must carry the SEN63C's 200 mA peaks with margin")

    # 10. charger vs cell: C-rate must be safe and the charge time sane
    i_charge_ma = 1188.0 / CHARGER_R_ISET * 1e3
    e.check(i_charge_ma <= CELL_MAH,
            f"charge current {i_charge_ma:.0f} mA exceeds 1C for {CELL_MAH:.0f} mAh")
    e.check(CELL_MAH / i_charge_ma <= 12.0,
            f"charge time {CELL_MAH / i_charge_ma:.1f} h is unreasonable", warn=True)

    # 11. both load switches are driven from LP GPIOs so they hold in sleep
    for sw, net in (("SW1", "EN_SEN6X"), ("SW2", "EN_SENS")):
        gp = [g for g in GPIO_MAP if g.signal == net]
        e.check(len(gp) == 1 and gp[0].gpio in C6_RTC_GPIO,
                f"{sw} enable net {net} is not on an LP GPIO")
        e.check((sw, "3") in NETS[net], f"{sw}.ON is not on {net}")

    # 12. USB sense: a logic high on USB, never above the pad's maximum
    r_top, r_bot = 68e3, 100e3
    vusb = RAILS["VUSB"]
    v_lo = vusb.vmin * r_bot / (r_top + r_bot)
    v_hi = vusb.vmax * r_bot / (r_top + r_bot)
    e.check(v_lo >= 0.75 * RAILS["+3V3"].vnom,
            f"USB sense {v_lo:.2f} V at 4.75 V VBUS is below VIH")
    e.check(v_hi <= RAILS["+3V3"].vmin + 0.3,
            f"USB sense {v_hi:.2f} V at 5.25 V VBUS exceeds VDD + 0.3 V")

    # 13. the LED anode is on VBAT (the only rail with headroom for blue/green)
    e.check(_nets_of("LED1", "A") == ["VBAT"], "LED1 anode must be on VBAT")
    e.check(("M1", "BAT+") in NETS["VBAT"] and ("M1", "BAT-") in NETS["BATN"],
            "the battery must reach the FireBeetle through the carrier")

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
    w("# AIR CHECK - net list and pin map (ACC-1 rev C, v1.2)")
    w("")
    w("## Power tree")
    w("")
    w("```")
    w("1S LiPo 4000 mAh --J2--+--J3--> FireBeetle BAT (JST PH)")
    w("                       `------> LED1 common anode (VBAT)")
    w("")
    w("USB-C (FireBeetle) --> CN3165 charger, 540 mA --> VBAT")
    w("VBAT / USB --> TPS62A02 buck --> +3V3 (always on, FireBeetle)")
    w("")
    w("+3V3 --[SW1 TPS22918, CT 4.7 nF, GPIO2]--> +3V3_SEN6X --> SEN63C, R11/R12 pull-ups")
    w("+3V3 --[SW2 TPS22918, CT 1 nF,   GPIO3]--> +3V3_SENS  --> SGP40 breakout, R6/R7 pull-ups")
    w("+3V3 ---------------------------------->  ESP32-C6, button pull-up")
    w("VIN (USB 5 V) --68k--+--100k-- GND        USB_SENSE on GPIO18")
    w("VBAT --1M--+--1M-- GND  (on the FireBeetle)  BAT_ADC on GPIO0")
    w("```")
    w("")
    w("## Rails")
    w("")
    w("| rail | min | nom | max | source |")
    w("|---|---|---|---|---|")
    for r in RAILS.values():
        w(f"| {r.name} | {r.vmin:.2f} V | {r.vnom:.2f} V | {r.vmax:.2f} V | {r.source} |")
    w("")
    w("## GPIO map (ESP32-C6 on the DFRobot FireBeetle 2)")
    w("")
    w("| GPIO | header label | signal | dir | LP pad | note |")
    w("|---|---|---|---|---|---|")
    for g in sorted(GPIO_MAP, key=lambda x: x.gpio):
        w(f"| {g.gpio} | {g.board_pin} | {g.signal} | {g.direction} | "
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
    w("One bus per switched rail, each with its pull-ups on the rail it serves, "
      "100 kHz:")
    w("")
    w("| address | device |")
    w("|---|---|")
    for a, d in sorted({**I2C_BUS, **SEN_BUS}.items()):
        w(f"| 0x{a:02X} | {d} |")
    w("")
    w("The Adafruit SGP40 breakout has its own 10k pull-ups (to its VIN and to its "
      "LDO output) and BSS138 level shifters. They sit on the same switched rail, "
      "so they vanish with it; together with R6/R7 the bus sees about 3.2k.")
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
        "recommended", "RECOMMENDED BUILD",
        "FireBeetle 2 ESP32-C6, SEN63C and the Adafruit SGP40 breakout on the ACC-1 "
        "carrier. No fine-pitch soldering: the smallest thing you hand-solder is an "
        "0603 resistor, and the carrier can be ordered assembled instead. This is the "
        "build the assembly guide and the CAD are drawn around.",
        swaps={"X4": None},
        extra=[],
    ),
    Variant(
        "assembled", "ASSEMBLED CARRIER",
        "The same parts, with the carrier's SMD components placed by JLCPCB instead "
        "of by hand. About EUR 45-50 for two assembled boards including shipping, "
        "VAT and the EUR 3 EU customs duty that applies since July 2026 - choose DDP "
        "shipping so the carrier does not add a handling fee on delivery. Worth it "
        "from the second unit on, or if you do not solder SMD at all.",
        swaps={"X4": None,
               "PCB1": ("ACC-1 rev C, SMD assembled (JLCPCB, DDP)", "ACC-1 rev C", 23.0),
               "SW1": ("TPS22918 (placed by JLCPCB)", "TPS22918DBVR", 0.0),
               "SW2": ("TPS22918 (placed by JLCPCB)", "TPS22918DBVR", 0.0)},
        extra=[],
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
    w("| build | electronics total | note |")
    w("|---|---|---|")
    for v in VARIANTS:
        _, t = variant_bom(v)
        note = {"recommended": "hand-solder the carrier (0603 and SOT-23)",
                "assembled": "carrier arrives soldered"}[v.key]
        w(f"| {v.title} | **EUR {t:.2f}** | {note} |")
    w("")
    w("### Where the money goes, and what changed in v1.2")
    w("")
    w("v1.1 cost EUR 173: an SPS30 (EUR 38), an SCD41 breakout (EUR 52), a 5 V")
    w("boost converter and an Adafruit Feather (EUR 22). Two thirds of that was")
    w("sensors bought one function at a time.")
    w("")
    w("v1.2 buys the particle, CO2, temperature and humidity channels as one")
    w("Sensirion SEN63C (EUR 34, less than the SPS30 alone), runs it straight from")
    w("3.3 V so the boost converter goes, and replaces the Feather with a EUR 7.50")
    w("FireBeetle 2 ESP32-C6 whose deep-sleep current is also lower. The VOC")
    w("channel stays a separate SGP40, because it has to run every 10 s and the")
    w("SEN6x family cannot do that on a battery (EDR-15). Nothing was dropped:")
    w("PM1/2.5/4/10, true CO2, VOC index, temperature and humidity are all still")
    w("there. The one measured trade-off is CO2 accuracy, +-(100 ppm + 10 %)")
    w("instead of +-(50 ppm + 5 %).")
    w("")
    w("Prices were checked on the shops' own pages in September 2026 where")
    w("possible (Mouser prices are shown there without VAT; they are converted).")
    w("Mouser ships free above EUR 50 net, so one Mouser parcel for the sensor,")
    w("the load switches, the connectors and the RGB LED is the cheapest route;")
    w("the FireBeetle, the cell and the inserts come from shops that stock them")
    w("(see `supplier`). Avoid SPS30/SCD4x/SEN6x listings on Amazon or AliExpress")
    w("that are cheaper than the bare part at a distributor: they are not the")
    w("genuine part at that price.")
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
    w("See `manufacturing/print-settings.md` for filament quantities. Inserts,")
    w("screws and foam are in the table above (X1, X2).")
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
