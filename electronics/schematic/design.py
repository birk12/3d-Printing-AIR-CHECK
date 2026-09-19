"""
3D Printing AIR CHECK - electrical design source of truth (v1.3).

Since v1.3 there is no custom circuit board (EDR-19).  The device is a set of
off-the-shelf modules joined by wires; the handful of resistors that are left
are through-hole parts soldered at a module's pins and covered with heat
shrink.  This module *is* the wiring diagram: it declares every module, every
wire and every connection, then runs an electrical rule check over them.  From
it we emit:

  * electronics/schematic/aircheck.net   KiCad-compatible flat netlist
  * electronics/schematic/NETLIST.md     wiring list, pin map, power tree
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
from dataclasses import dataclass

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
    "VPACK":     Rail("VPACK", 5.40, 8.70, 10.80,
                      "6 x AA in series: 0.9 V/cell floor, 1.45 V L91 average, "
                      "1.8 V open-circuit of a fresh L91"),
    "VPACK_F":   Rail("VPACK_F", 5.40, 8.70, 10.80, "VPACK behind the PTC fuse F1"),
    "+4V0":      Rail("+4V0", 3.92, 4.00, 4.08,
                      "Pololu S9V11E2A, trimpot set to 4.00 V +-2 %; always on"),
    "VSYS":      Rail("VSYS", 3.88, 4.00, 4.24,
                      "FireBeetle battery input: +4V0 through the LM66200, or the "
                      "FireBeetle's own CN3165 at 4.2 V +-1 % while USB is plugged in"),
    "+3V3":      Rail("+3V3", 3.20, 3.30, 3.40, "FireBeetle TPS62A02 buck, always on"),
    "+3V3_SEN":  Rail("+3V3_SEN", 3.20, 3.30, 3.40, "+3V3 behind the Pololu #2810 switch"),
    "CO2_VDDIO": Rail("CO2_VDDIO", 0.0, 3.30, 3.40,
                      "GPIO18 driven high only while the Sunrise is enabled"),
    "GND":       Rail("GND", 0.0, 0.0, 0.0, "system ground"),
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
    nc: tuple = ()      # pins deliberately left unconnected, with the reason in `why`
    vsupply_min: float | None = None
    vsupply_max: float | None = None
    i_typ_ma: float | None = None
    i_max_ma: float | None = None
    i2c_addr: int | None = None
    datasheet: str = ""


def _r(ref, value, why, price=0.10):
    return Part(ref=ref, value=f"{value}, 1 %, 0.25 W, metal film, THT", mfr="Yageo",
                mpn=f"MFR-25FBF52-{value.replace(' ', '')}", footprint="axial, 6.3 mm body",
                pins={"1": "a", "2": "b"}, why=why, price_eur=price,
                supplier="Reichelt / Mouser")


PARTS: list[Part] = [
    # ---- controller -------------------------------------------------------
    Part(
        ref="M1", value="FireBeetle 2 ESP32-C6", mfr="DFRobot", mpn="DFR1075",
        footprint="60.0 x 25.4 mm, 4 x M2 holes at 1.7 mm from the edges, "
                  "2 x 0.1in rows 22.86 mm apart, JST PH 2.0 battery socket",
        pins={
            "3V3": "+3V3 out (TPS62A02 buck)", "GND": "ground", "VIN": "USB 5 V",
            "IO1": "GPIO1", "IO2": "GPIO2", "IO3": "GPIO3", "IO4": "GPIO4",
            "IO5": "GPIO5", "IO6": "GPIO6 / LP_SDA", "IO7": "GPIO7 / LP_SCL",
            "IO8": "GPIO8", "IO9": "GPIO9 / BOOT", "IO14": "GPIO14",
            "IO15": "GPIO15 / green LED", "IO16": "GPIO16 / U0TXD", "IO17": "GPIO17 / U0RXD",
            "IO18": "GPIO18", "SDA": "GPIO19", "SCL": "GPIO20",
            "IO21": "GPIO21", "IO22": "GPIO22", "IO23": "GPIO23", "RST": "reset",
            "BAT+": "JST PH battery +", "BAT-": "JST PH battery -",
        },
        why="ESP32-C6 with native 802.15.4 for Thread, USB-C for configuration and "
            "updates, a TPS62A02 3.3 V buck, a 1M/1M divider from the battery input "
            "to GPIO0. Its CN3165 charger is present but never charges anything: the "
            "LM66200 in front of the battery input blocks it (EDR-18).",
        price_eur=7.50, supplier="Botland / Berrybase / DFRobot",
        vsupply_min=3.0, vsupply_max=4.25,
        datasheet="https://wiki.dfrobot.com/SKU_DFR1075_FireBeetle_2_Board_ESP32_C6",
    ),
    # ---- sensors ----------------------------------------------------------
    Part(
        ref="U1", value="SEN62 (PM1/2.5/4/10)", mfr="Sensirion", mpn="SEN62-SIN-T",
        footprint="55.2 x 25.6 x 21.3 mm module, ACES 51468-0064N-001 6-way",
        pins={"1": "VDD", "2": "GND", "3": "SDA", "4": "SCL", "5": "GND", "6": "VDD"},
        why="Laser particle sensor with sheath flow, 3.3 V, 75 mA. Only particles: "
            "CO2 comes from the Sunrise and T/RH from the SHT40, which are each better "
            "at their job than the SEN6x combination modules (EDR-16, EDR-17). "
            "Power-gated by SW1 because it idles at 3.3 mA.",
        price_eur=20.71, supplier="Mouser (EUR 17.40 net)",
        vsupply_min=3.15, vsupply_max=3.45, i_typ_ma=75.0, i_max_ma=190.0,
        i2c_addr=0x6B, datasheet="Sensirion SEN6x Datasheet v0.92, December 2025",
    ),
    Part(ref="W1", value="JST GH 6-way cable, 150 mm, one end open", mfr="JST / generic",
         mpn="GHR-06V-S + pre-crimped SSHL-002T-P0.2 leads", footprint="1.25 mm pitch",
         pins={}, why="SEN62 connector to the wiring. Sensirion allow up to 50 cm.",
         price_eur=2.00, supplier="Mouser / Berrybase"),
    Part(
        ref="U2", value="SparkFun Qwiic SGP40", mfr="SparkFun", mpn="SEN-18345",
        footprint="25.4 x 25.4 mm, 4 x 3.3 mm holes 2.54 mm from the edges",
        pins={"3V3": "3.3 V", "GND": "ground", "SDA": "I2C data", "SCL": "I2C clock"},
        why="SGP40 MOX VOC sensor: the tripwire that speeds the particle sensor up. "
            "No regulator on the board; the red power LED is cut at the PWR jumper, "
            "the I2C jumper stays closed (4.7k pull-ups for the always-on bus). "
            "Powered permanently, as the VOC algorithm wants (EDR-17).",
        price_eur=16.45, supplier="Berrybase",
        vsupply_min=1.7, vsupply_max=3.6, i_typ_ma=2.6, i_max_ma=4.0, i2c_addr=0x59,
        datasheet="Sensirion SGP40 Datasheet v1.2; SparkFun Qwiic_Air_Quality_Sensor_SGP40",
    ),
    Part(ref="W2", value="Qwiic cable to open leads, 150 mm", mfr="SparkFun",
         mpn="PRT-17261", footprint="JST SH 4-way", pins={},
         why="SGP40 to the wiring. Qwiic colours: black GND, red 3V3, blue SDA, "
             "yellow SCL.", price_eur=1.60, supplier="Berrybase / Eckstein"),
    Part(
        ref="U3", value="Seeed Grove SHT40", mfr="Seeed Studio", mpn="101021032",
        footprint="20 x 40 mm Grove board, Grove 4-way 2.0 mm",
        pins={"VCC": "supply", "GND": "ground", "SDA": "I2C data", "SCL": "I2C clock"},
        why="Temperature and humidity, +-0.2 C / +-1.8 %RH, for the dashboard and for "
            "the SGP40's compensation. Sits low in the gas bay, away from the Sunrise's "
            "lamp and the ESP32 (EDR-17). 1.2 uA idle per Seeed. Seeed rate the "
            "board for 3.3 V / 5 V; the SHT40 itself runs from 1.08-3.6 V, so a "
            "small regulator dropout at the rail's 3.2 V minimum is harmless.",
        price_eur=6.60, supplier="Seeed / robot-italy / Berrybase",
        vsupply_min=3.2, vsupply_max=5.0, i_typ_ma=0.4, i_max_ma=0.5, i2c_addr=0x44,
        datasheet="Sensirion SHT4x Datasheet; wiki.seeedstudio.com/Grove-SHT4x",
    ),
    Part(ref="W3", value="Grove cable to female jumpers, 200 mm", mfr="Seeed Studio",
         mpn="110990028", footprint="Grove 4-way", pins={},
         why="SHT40 to the wiring. Grove colours: black GND, red VCC, white SDA, "
             "yellow SCL.", price_eur=2.50, supplier="Berrybase / Seeed"),
    Part(
        ref="U4", value="Senseair Sunrise CO2", mfr="Senseair", mpn="006-0-0008",
        footprint="33.5 x 19.7 x 11.5 mm, two 0.1in rows 30.48 mm apart (5 + 4 pins)",
        pins={"1": "GND", "2": "VBB", "3": "VDDIO", "4": "RxD/SDA", "5": "TxD/SCL",
              "6": "COMSEL", "7": "nRDY", "8": "DVCC", "9": "EN"},
        why="NDIR CO2, +-(30 ppm + 3 %), built for battery single-measurement use: "
            "EN low between measurements, the host keeps the ABC state (EDR-16). "
            "COMSEL to GND selects I2C. VDDIO and the bus pull-ups come from GPIO18, "
            "so nothing reaches the sensor's I/O while EN is low (TDE7318 'Low power "
            "integration'). nRDY is not used: the firmware waits the worst-case "
            "measurement time instead. DVCC is an output and stays open.",
        price_eur=49.00, supplier="Mouser (EUR 41.18 net)",
        nc=("7", "8"),
        vsupply_min=3.05, vsupply_max=5.5, i_typ_ma=0.034, i_max_ma=125.0,
        i2c_addr=0x68, datasheet="Senseair PSP12440, TDE7318, TDE5531",
    ),
    # ---- power -------------------------------------------------------------
    Part(
        ref="BT1", value="battery holder 6 x AA, flat, 150 mm leads", mfr="MPD",
        mpn="BH36AAW", footprint="110.0 x 49.5 x 16.9 mm, 4 x 3.2 mm holes on 48.1 x 35.3",
        pins={"+": "red lead", "-": "black lead"},
        why="Six AA cells side by side, screwed flat to the floor of the battery "
            "compartment behind its own door. Takes any AA: lithium L91 "
            "(recommended), NiMH or alkaline.",
        price_eur=4.50, supplier="Mouser",
        vsupply_min=0.0, vsupply_max=10.8,
    ),
    Part(ref="CELL", value="Energizer Ultimate Lithium AA, first set of 6",
         mfr="Energizer", mpn="L91", footprint="AA", pins={}, qty=1,
         why="Primary lithium-iron-disulfide: the most energy per AA, no leakage, "
             "works to -40 C, 20 years shelf life. Nothing charges inside the device "
             "(EDR-18). eneloop pro or alkaline also work, for less runtime.",
         price_eur=14.00, supplier="dm / Rossmann / Reichelt"),
    Part(
        ref="F1", value="PTC resettable fuse 0.5 A hold / 1.0 A trip", mfr="Bourns",
        mpn="MF-R050", footprint="radial, 5.1 mm lead spacing, 7.9 x 13.7 x 3.1 mm",
        pins={"1": "a", "2": "b"},
        why="Short-circuit protection right at the holder's red lead: any fault "
            "downstream (a pinched wire, a failed regulator) is limited to 1 A. "
            "Trips within 4 s at 2.5 A. Soldered inline, under heat shrink.",
        price_eur=0.30, supplier="Reichelt",
        i_max_ma=500.0,
    ),
    Part(
        ref="PS1", value="Pololu S9V11E2A buck-boost, set to 4.00 V", mfr="Pololu",
        mpn="5719", footprint="10.9 x 16.5 x 4.0 mm, 4 pins: VOUT GND VIN EN",
        pins={"VIN": "input 2-16 V (3 V to start)", "GND": "ground",
              "VOUT": "output 2.5-9 V (trimpot)", "EN": "enable, 100k pull-up to VIN"},
        why="Turns 5.4-10.8 V from the pack into a steady 4.0 V - what the "
            "FireBeetle expects on its battery input. Buck-boost, so the whole pack is usable. EN stays open "
            "(on). Set the trimpot before connecting anything (ASSEMBLY step 4).",
        price_eur=6.50, supplier="Eckstein / Pololu",
        nc=("EN",),
        vsupply_min=3.0, vsupply_max=16.0, i_max_ma=1700.0,
    ),
    Part(
        ref="D1", value="Adafruit LM66200 ideal diode breakout", mfr="Adafruit",
        mpn="5830", footprint="16.51 x 10.16 mm, 2 x 2.5 mm holes, 6 pins",
        pins={"VIN1": "input 1", "VIN2": "input 2", "GND": "ground", "VOUT": "output",
              "ON": "active-low enable", "ST": "status, open drain"},
        why="Feeds +4V0 into the FireBeetle's battery input and blocks every current "
            "the other way: with USB plugged in the FireBeetle's charger holds its "
            "battery input at 4.2 V, 0.2 V above +4V0, far past the 70 mV reverse-"
            "blocking threshold (TI SLVSG04). So the AA cells are never charged. "
            "VIN2 and ON to GND (truth table: VIN1 > VIN2, ON low = VIN1 feeds VOUT). "
            "1.3 uA quiescent. ST is unused.",
        price_eur=3.50, supplier="Adafruit / Berrybase",
        nc=("ST",),
        vsupply_min=1.6, vsupply_max=5.5, i_max_ma=2500.0,
        datasheet="TI LM66200 SLVSG04; github.com/adafruit/Adafruit-LM66200-PCB",
    ),
    Part(ref="J1", value="JST PH 2-way pigtail, 100 mm", mfr="generic",
         mpn="PHR-2 with leads", footprint="JST PH 2.0 mm", pins={"+": "+", "-": "-"},
         why="LM66200 output to the FireBeetle's battery socket. Check the polarity "
             "against the '+' on the FireBeetle before plugging in: JST PH leads are "
             "not standardised.", price_eur=0.50, supplier="Berrybase"),
    Part(
        ref="SW1", value="Pololu Mini MOSFET Switch LV", mfr="Pololu", mpn="2810",
        footprint="15.24 x 15.24 mm, pins on a 0.1in grid",
        pins={"VIN": "input", "GND": "ground", "VOUT": "switched output",
              "ON": "logic on (> ~1 V)", "SW": "slide-switch contact"},
        why="Switches the SEN62's 3.3 V off completely between windows. The slide "
            "switch must stay in OFF so the ON pin has control. Its red LED draws "
            "~0.7 mA only while the switch is on. No soft start: see the ERC note.",
        price_eur=5.34, supplier="Eckstein",
        nc=("SW",),
        vsupply_min=1.8, vsupply_max=16.0, i_max_ma=6000.0,
    ),
    Part(ref="C1", value="100 nF ceramic, THT", mfr="KEMET", mpn="C320C104K5R5TA",
         footprint="radial 2.54 mm", pins={"1": "+", "2": "-"},
         why="Holds the pack-divider node steady for the ADC's sample capacitor.",
         price_eur=0.10, supplier="Reichelt / Mouser"),
    _r("R1", "1 M", "Pack divider, top: 10.8 V x 220k/1.22M = 1.95 V at most on GPIO3. "
                   "Draws 7 uA from the pack."),
    _r("R2", "220 k", "Pack divider, bottom."),
    _r("R3", "4.7 k", "SEN62 SDA pull-up to +3V3_SEN: it switches off with the sensor, "
                     "so no pull-up back-feeds the unpowered SEN62."),
    _r("R4", "4.7 k", "SEN62 SCL pull-up, as R3."),
    _r("R5", "10 k", "Sunrise SDA pull-up to CO2_VDDIO (GPIO18). Senseair recommend "
                    "5-15k. Solder it across Sunrise pins 3 and 4."),
    _r("R6", "10 k", "Sunrise SCL pull-up, across pins 3 and 5."),
    _r("R7", "100 k", "Sunrise EN pull-down: EN must never float (PSP12440), also not "
                     "while the ESP32 is in reset."),
    _r("R8", "1 k", "Red: (4.0 V - 2.0 V) / 1k = 2.0 mA (2.2 mA on USB, 4.2 V)."),
    _r("R9", "330", "Green: (4.0 V - 3.1 V) / 330 = 2.7 mA."),
    _r("R10", "330", "Blue: as green."),
    # ---- user interface ----------------------------------------------------
    Part(ref="LED1", value="RGB LED 5 mm diffused, common anode", mfr="Adafruit",
         mpn="159", footprint="THT 5 mm, 4 leads",
         pins={"A": "common anode", "R": "red cathode", "G": "green cathode",
               "B": "blue cathode"},
         why="The only output: air quality at a glance, pairing, calibration, low "
             "battery. Anode on VSYS for green/blue headroom; cathodes sunk by GPIOs.",
         price_eur=1.20, supplier="Mouser / Adafruit"),
    Part(ref="LH1", value="LED panel holder 5 mm, M8 x 0.75", mfr="Signal Construct",
         mpn="SMR1089", footprint="8.2 mm hole", pins={},
         why="Holds LED1 in the front panel.", price_eur=1.10, supplier="Reichelt (EBF A-5 S)"),
    Part(ref="SW2", value="miniature push button, momentary, 7 mm panel hole",
         mfr="generic", mpn="T 250A SW", footprint="7 mm hole, ~29 mm long",
         pins={"1": "a", "2": "b"},
         why="The one button: pairing, fresh-air calibration, reset (hold times in "
             "USER_GUIDE). To GND; the pull-up is inside the ESP32-C6.",
         price_eur=0.60, supplier="Reichelt"),
    # ---- mechanical and consumables ----------------------------------------
    Part(ref="X1", value="M2.5 brass heat-set inserts + M2.5 screws", mfr="ruthex",
         mpn="RX-M2.5x5.7 + DIN 912 M2.5x8", footprint="-", pins={},
         why="Re-openable enclosure and module mounts.", price_eur=9.60,
         supplier="ruthex.de / Berrybase"),
    Part(ref="X2", value="M2 x 6 screws + M2 nuts, 4 each", mfr="generic", mpn="DIN 912 M2x6",
         footprint="-", pins={}, why="FireBeetle to its standoffs.", price_eur=1.00,
         supplier="Reichelt"),
    Part(ref="X3", value="silicone wire 26 AWG, 4 colours", mfr="generic", mpn="-",
         footprint="-", pins={}, why="Harness.", price_eur=3.00, supplier="local"),
    Part(ref="X5", value="heat-shrink tubing assortment", mfr="generic", mpn="-",
         footprint="-", pins={}, why="Over every inline resistor, the fuse and every "
                                     "splice.", price_eur=2.00, supplier="local"),
    Part(ref="X4", value="Nordic PPK II power profiler", mfr="Nordic", mpn="nRF-PPK2",
         footprint="-", pins={}, why="Only way to confirm the energy model on real "
                                     "hardware. Optional but strongly recommended.",
         price_eur=95.0, supplier="Mouser / Digi-Key", required=False),
]

PARTS_BY_REF = {p.ref: p for p in PARTS}

# --------------------------------------------------------------------------
# Nets  (net name -> [(ref, pin), ...])
# --------------------------------------------------------------------------

NETS: dict[str, list[tuple[str, str]]] = {
    # ---- power path ---------------------------------------------------------
    "VPACK":     [("BT1", "+"), ("F1", "1")],
    "VPACK_F":   [("F1", "2"), ("PS1", "VIN"), ("R1", "1")],
    "+4V0":      [("PS1", "VOUT"), ("D1", "VIN1")],
    "VSYS":      [("D1", "VOUT"), ("J1", "+"), ("M1", "BAT+"), ("U4", "2"), ("LED1", "A")],
    "+3V3":      [("M1", "3V3"), ("SW1", "VIN"), ("U2", "3V3"), ("U3", "VCC")],
    "+3V3_SEN":  [("SW1", "VOUT"), ("U1", "1"), ("U1", "6"), ("R3", "1"), ("R4", "1")],
    "GND":       [("BT1", "-"), ("PS1", "GND"), ("D1", "GND"), ("D1", "VIN2"),
                  ("D1", "ON"), ("J1", "-"), ("M1", "BAT-"), ("M1", "GND"),
                  ("SW1", "GND"), ("U1", "2"), ("U1", "5"), ("U2", "GND"),
                  ("U3", "GND"), ("U4", "1"), ("U4", "6"), ("R2", "2"), ("C1", "2"),
                  ("R7", "2"), ("SW2", "2")],
    # ---- measurement / control ---------------------------------------------
    "PACK_ADC":  [("R1", "2"), ("R2", "1"), ("C1", "1"), ("M1", "IO3")],
    "SEN_EN":    [("M1", "IO2"), ("SW1", "ON")],
    "CO2_EN":    [("M1", "IO14"), ("U4", "9"), ("R7", "1")],
    "CO2_VDDIO": [("M1", "IO18"), ("U4", "3"), ("R5", "1"), ("R6", "1")],
    "BTN":       [("M1", "IO1"), ("SW2", "1")],
    # ---- SEN62: HP I2C on GPIO19/20 ------------------------------------------
    "SEN_SDA":   [("M1", "SDA"), ("U1", "3"), ("R3", "2")],
    "SEN_SCL":   [("M1", "SCL"), ("U1", "4"), ("R4", "2")],
    # ---- Sunrise: the same HP controller, re-routed to GPIO17/21 --------------
    "CO2_SDA":   [("M1", "IO17"), ("U4", "4"), ("R5", "2")],
    "CO2_SCL":   [("M1", "IO21"), ("U4", "5"), ("R6", "2")],
    # ---- always-on bus: LP I2C, fixed pads GPIO6/7 --------------------------
    "SENS_SDA":  [("M1", "IO6"), ("U2", "SDA"), ("U3", "SDA")],
    "SENS_SCL":  [("M1", "IO7"), ("U2", "SCL"), ("U3", "SCL")],
    # ---- status LED ------------------------------------------------------------
    "LED_R":     [("M1", "IO16"), ("R8", "1")],
    "LED_R_K":   [("R8", "2"), ("LED1", "R")],
    "LED_G":     [("M1", "IO22"), ("R9", "1")],
    "LED_G_K":   [("R9", "2"), ("LED1", "G")],
    "LED_B":     [("M1", "IO23"), ("R10", "1")],
    "LED_B_K":   [("R10", "2"), ("LED1", "B")],
}

# Where each inline part is physically soldered (for NETLIST.md and ASSEMBLY).
SOLDER_AT = {
    "F1": "in the red lead of the battery holder, 2 cm from the holder",
    "R1": "at the regulator's VIN pad, on its own wire to FireBeetle IO3",
    "R2": "at FireBeetle IO3 to GND, together with C1",
    "C1": "at FireBeetle IO3 to GND",
    "R3": "from SW1 VOUT to the SEN62 SDA wire",
    "R4": "from SW1 VOUT to the SEN62 SCL wire",
    "R5": "across Sunrise pins 3 (VDDIO) and 4 (SDA)",
    "R6": "across Sunrise pins 3 (VDDIO) and 5 (SCL)",
    "R7": "at FireBeetle IO14 to GND",
    "R8": "on the LED's red lead",
    "R9": "on the LED's green lead",
    "R10": "on the LED's blue lead",
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

# ESP32-C6: LP GPIOs are GPIO0..GPIO7.  Strapping pins GPIO4, 5, 8, 9, 15.
C6_RTC_GPIO = set(range(0, 8))
C6_STRAPPING = {4, 5, 8, 9, 15}
C6_ROM_UART_TX = 16      # the ROM boot log toggles it after every reset

GPIO_MAP = [
    GpioUse(0,  "(on board)", "REG_ADC", "analog in", True, False,
            "FireBeetle 1M/1M divider from VSYS: 4.0 V on the pack, 4.2 V on USB"),
    GpioUse(1,  "1",   "BTN",       "in",    True,  False, "wake source, internal pull-up, active low"),
    GpioUse(2,  "2",   "SEN_EN",    "out",   True,  False, "held LOW in sleep"),
    GpioUse(3,  "3",   "PACK_ADC",  "analog in", True, False, "pack / 5.545, ADC1 channel 3"),
    GpioUse(6,  "6",   "SENS_SDA",  "bidir", True,  False,
            "LP_I2C SDA - a fixed pad on the C6, not remappable"),
    GpioUse(7,  "7",   "SENS_SCL",  "bidir", True,  False,
            "LP_I2C SCL - a fixed pad on the C6, not remappable"),
    GpioUse(14, "14",  "CO2_EN",    "out",   False, False,
            "R7 keeps it low through reset and sleep"),
    GpioUse(16, "16",  "LED_R",     "out",   False, False,
            "U0TXD: the ROM boot log only makes the red LED flicker"),
    GpioUse(17, "17",  "CO2_SDA",   "bidir", False, False, "input while the Sunrise is off"),
    GpioUse(18, "18",  "CO2_VDDIO", "out",   False, False,
            "Sunrise VDDIO and its pull-ups; low whenever EN is low"),
    GpioUse(19, "SDA", "SEN_SDA",   "bidir", False, False, "input while the SEN62 is off"),
    GpioUse(20, "SCL", "SEN_SCL",   "bidir", False, False, "input while the SEN62 is off"),
    GpioUse(21, "21",  "CO2_SCL",   "bidir", False, False, "input while the Sunrise is off"),
    GpioUse(22, "22",  "LED_G",     "out",   False, False, "sink, active low"),
    GpioUse(23, "23",  "LED_B",     "out",   False, False, "sink, active low"),
]

RESERVED_GPIO = {
    4:  "strapping pin (MTMS) - left unconnected",
    5:  "strapping pin (MTDI) - left unconnected",
    8:  "strapping pin - left unconnected",
    9:  "strapping pin, BOOT button on the FireBeetle",
    12: "native USB D-",
    13: "native USB D+",
    15: "strapping pin, FireBeetle green LED - driven low by the firmware",
}

# I2C buses and what hangs on them
BUSES = {
    "LP": {"sda": "SENS_SDA", "scl": "SENS_SCL", "rail": "+3V3",
           "devices": {0x59: "U2", 0x44: "U3"},
           "pullups": "on the boards: SparkFun 4.7k (I2C jumper closed), Grove board's own"},
    "SEN": {"sda": "SEN_SDA", "scl": "SEN_SCL", "rail": "+3V3_SEN",
            "devices": {0x6B: "U1"}, "pullups": "R3, R4"},
    "CO2": {"sda": "CO2_SDA", "scl": "CO2_SCL", "rail": "CO2_VDDIO",
            "devices": {0x68: "U4"}, "pullups": "R5, R6"},
}

# Currents for the power checks (datasheets; see the Part entries)
ESP_TX_PEAK_MA = 350.0      # ESP32-C6 802.15.4 TX peak at 3.3 V, conservative
ADC_MAX_V = 2.9             # ESP32-C6 ADC, 12 dB attenuation, usable range
LM66200_VRCB_MAX = 0.070    # reverse-current blocking threshold, max (SLVSG04)
LED_VF = {"R": 1.8, "G": 2.9, "B": 2.9}     # minimum forward voltages
PS1_EFF_MIN = 0.80


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

    # 1. every pin referenced by a net exists on that part
    for net, conns in NETS.items():
        for ref, pin in conns:
            e.check(ref in PARTS_BY_REF, f"net {net}: unknown part {ref}")
            p = PARTS_BY_REF.get(ref)
            if p and p.pins:
                e.check(pin in p.pins,
                        f"net {net}: {ref} has no pin '{pin}' (has {sorted(p.pins)})")

    # 2. no net with a single connection
    for net, conns in NETS.items():
        e.check(len(conns) >= 2, f"net {net} has only {len(conns)} connection(s)")

    # 3. no pin on two nets
    seen: dict[tuple[str, str], str] = {}
    for net, conns in NETS.items():
        for c in conns:
            prev = seen.get(c)
            e.check(prev is None, f"pin {c[0]}.{c[1]} is on both '{prev}' and '{net}'")
            seen[c] = net

    # 3b. every pin of every part is connected or declared NC, and an NC pin
    #     really is unconnected
    for p in PARTS:
        if not p.pins or p.ref == "M1":
            continue
        for pin in p.pins:
            if pin in p.nc:
                e.check((p.ref, pin) not in seen, f"{p.ref}.{pin} is declared NC but wired")
            else:
                e.check((p.ref, pin) in seen,
                        f"{p.ref}.{pin} ({p.pins[pin]}) is unconnected")

    # 4. supply-voltage compatibility
    supply_of = {"U1": ("1", "+3V3_SEN"), "U2": ("3V3", "+3V3"), "U3": ("VCC", "+3V3"),
                 "U4": ("2", "VSYS"), "PS1": ("VIN", "VPACK_F"), "D1": ("VIN1", "+4V0"),
                 "SW1": ("VIN", "+3V3"), "M1": ("BAT+", "VSYS")}
    for ref, (pin, railname) in supply_of.items():
        p = PARTS_BY_REF[ref]
        r = RAILS[railname]
        e.check(_nets_of(ref, pin) == [railname],
                f"{ref}.{pin} should be on {railname}, is on {_nets_of(ref, pin)}")
        e.check(p.vsupply_min <= r.vmin,
                f"{ref} needs >= {p.vsupply_min} V but {railname} can fall to {r.vmin} V")
        e.check(p.vsupply_max >= r.vmax,
                f"{ref} max {p.vsupply_max} V but {railname} can reach {r.vmax} V")
    e.check(PARTS_BY_REF["D1"].vsupply_max >= RAILS["VSYS"].vmax,
            "LM66200 output side must tolerate the charger's 4.2 V")

    # 5. the pack reaches nothing except through F1
    e.check(sorted(NETS["VPACK"]) == [("BT1", "+"), ("F1", "1")],
            "VPACK may only connect the holder to the fuse")

    # 6. no path that could charge the AA cells: VSYS (where the FireBeetle's
    #    charger sits) is separated from +4V0 by D1 alone, and D1 blocks
    #    whenever the charger lifts VSYS above +4V0
    e.check(("D1", "VIN1") in NETS["+4V0"] and ("D1", "VOUT") in NETS["VSYS"],
            "D1 must sit between +4V0 and VSYS")
    shared = {c for c in NETS["+4V0"]} & {c for c in NETS["VSYS"]}
    e.check(not shared, f"+4V0 and VSYS share {shared}")
    e.check(4.20 * 0.99 - RAILS["+4V0"].vmax > LM66200_VRCB_MAX,
            "charger voltage is not far enough above +4V0 for D1 to block")
    e.check(_nets_of("D1", "ON") == ["GND"] and _nets_of("D1", "VIN2") == ["GND"],
            "D1: ON low (enabled) and VIN2 at GND so VIN1 always feeds VOUT")

    # 7. current: regulator, switch, fuse
    # everything hangs off +4V0 through D1: the 3.3 V loads via the FireBeetle's
    # buck, the Sunrise and the LED directly on VSYS
    load_4v0_ma = ((PARTS_BY_REF["U1"].i_max_ma + ESP_TX_PEAK_MA
                    + PARTS_BY_REF["U2"].i_max_ma) * 3.3 / (0.9 * 4.0)
                   + PARTS_BY_REF["U4"].i_max_ma + 3 * 3.0)
    e.check(load_4v0_ma < PARTS_BY_REF["PS1"].i_max_ma * 0.6,
            f"worst-case +4V0 load {load_4v0_ma:.0f} mA leaves too little regulator margin")
    e.check(PARTS_BY_REF["SW1"].i_max_ma >= 2 * PARTS_BY_REF["U1"].i_max_ma,
            "SW1 must carry the SEN62's peaks with margin")
    # sustained pack current: SEN62 window + radio average, at the lowest pack voltage
    sustained_ma = ((PARTS_BY_REF["U1"].i_typ_ma + 30.0) * 3.3 / 0.9
                    ) / (RAILS["VPACK"].vmin * PS1_EFF_MIN)
    e.check(sustained_ma < 0.41 * 1000 * 0.5,
            f"sustained {sustained_ma:.0f} mA is too close to F1's 0.41 A hold at 40 C")
    e.check(False, "SW1 has no soft start: the SEN62's switch-on step lands on the "
                   "FireBeetle's 3.3 V buck - verify on the bench (TESTING T-P3)", warn=True)

    # 8. ADC ranges
    v_adc = RAILS["VPACK_F"].vmax * 220e3 / (1e6 + 220e3)
    e.check(v_adc <= ADC_MAX_V, f"pack divider gives {v_adc:.2f} V > {ADC_MAX_V} V")
    e.check(RAILS["VSYS"].vmax / 2 <= ADC_MAX_V, "VSYS / 2 exceeds the ADC range")

    # 9. I2C: unique addresses, pull-ups on the rail of the bus's devices
    addrs = [p.i2c_addr for p in PARTS if p.i2c_addr is not None]
    e.check(len(addrs) == len(set(addrs)), f"duplicate I2C address in {addrs}")
    for name, b in BUSES.items():
        for a, ref in b["devices"].items():
            e.check(PARTS_BY_REF[ref].i2c_addr == a, f"{name}: {ref} is not at 0x{a:02x}")
            e.check(0x08 <= a <= 0x77, f"I2C address 0x{a:02x} outside the 7-bit range")
            e.check((ref, "SDA") in NETS[b["sda"]] or (ref, "3") in NETS[b["sda"]]
                    or (ref, "4") in NETS[b["sda"]], f"{ref} is not on {b['sda']}")
        pu = [r for r, _ in NETS[b["sda"]] + NETS[b["scl"]] if r.startswith("R")]
        if name == "LP":
            e.check(not pu, "the LP bus uses the modules' own pull-ups")
            continue
        e.check(len(pu) == 2, f"{name} bus needs exactly one pull-up per line, has {pu}")
        for r in pu:
            e.check(_nets_of(r, "1") == [b["rail"]], f"{r} must pull up to {b['rail']}")
    e.check(_nets_of("U4", "3") == ["CO2_VDDIO"],
            "Sunrise VDDIO must share the switched net with its pull-ups")
    e.check(_nets_of("U4", "6") == ["GND"], "Sunrise COMSEL to GND selects I2C")
    e.check(("R7", "1") in NETS["CO2_EN"] and _nets_of("R7", "2") == ["GND"],
            "Sunrise EN needs its pull-down")
    gpio18_ma = 2 * 3.4 / 10e3 * 1e3
    e.check(gpio18_ma < 20.0, f"GPIO18 sources {gpio18_ma:.1f} mA")

    # 10. GPIO usage
    gm = {g.signal: g.gpio for g in GPIO_MAP}
    e.check(gm.get("SENS_SDA") == 6 and gm.get("SENS_SCL") == 7,
            "LP_I2C on the ESP32-C6 only exists on GPIO6 (SDA) / GPIO7 (SCL)")
    used = {}
    for g in GPIO_MAP:
        e.check(g.gpio not in used, f"GPIO{g.gpio} used twice: {used.get(g.gpio)} and {g.signal}")
        used[g.gpio] = g.signal
        e.check(g.gpio not in C6_STRAPPING, f"GPIO{g.gpio} ({g.signal}) is a strapping pin")
        e.check(g.rtc_capable == (g.gpio in C6_RTC_GPIO),
                f"GPIO{g.gpio} LP capability declared wrongly")
        if "held" in g.note or "wake" in g.note:
            e.check(g.gpio in C6_RTC_GPIO, f"GPIO{g.gpio} must be an LP GPIO to hold/wake")
    e.check(used.get(C6_ROM_UART_TX, "").startswith("LED"),
            "GPIO16 is toggled by the ROM boot log: only an LED may sit on it")
    for gpio in RESERVED_GPIO:
        e.check(gpio not in used, f"GPIO{gpio} is reserved but used for {used.get(gpio)}")
    pin_to_gpio = {"SDA": 19, "SCL": 20}
    for net, conns in NETS.items():
        for ref, pin in conns:
            if ref != "M1" or not pin.startswith(("IO", "SDA", "SCL")):
                continue
            gpio = pin_to_gpio.get(pin, int(pin[2:]) if pin.startswith("IO") else -1)
            e.check(gm.get(net) == gpio,
                    f"net {net} is on M1.{pin} (GPIO{gpio}) but the GPIO map says {gm.get(net)}")
    # the firmware's pin table must agree with the wiring
    hal = os.path.join(ROOT, "firmware/components/ac_hal/include/ac_hal/ac_hal.h")
    if os.path.exists(hal):
        pins = {}
        for line in open(hal):
            parts = line.split()
            if len(parts) >= 3 and parts[0] == "#define" and parts[1].startswith("AC_PIN_"):
                pins[parts[1][7:]] = int(parts[2])
        for sig, gpio in gm.items():
            if sig in pins:
                e.check(pins[sig] == gpio,
                        f"firmware says AC_PIN_{sig} = {pins[sig]}, wiring says {gpio}")
        for sig in ("SEN_EN", "CO2_EN", "CO2_IO", "CO2_SDA", "CO2_SCL", "SEN_SDA",
                    "SEN_SCL", "LED_R", "LED_G", "LED_B", "BUTTON", "PACK_ADC"):
            e.check(sig in pins, f"firmware has no AC_PIN_{sig}")
        e.check(pins.get("CO2_IO") == gm["CO2_VDDIO"] and pins.get("BUTTON") == gm["BTN"],
                "firmware CO2_IO / BUTTON disagree with the wiring")

    # 11. LED: on when sunk, off when the GPIO is high
    for c in "RGB":
        e.check(RAILS["VSYS"].vmax - RAILS["+3V3"].vmin < LED_VF[c],
                f"LED {c} would glow with its GPIO high")

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
    w("# AIR CHECK - wiring list and pin map (v1.3, no custom PCB)")
    w("")
    w("## Power tree")
    w("")
    w("```")
    w("6 x AA (BT1) --F1 PTC 0.5 A--> VPACK_F 5.4..10.8 V")
    w("   VPACK_F --> PS1 Pololu S9V11E2A (buck-boost) --> +4V0")
    w("   VPACK_F --R1 1M--+--R2 220k-- GND        PACK_ADC on GPIO3 (C1 100 nF)")
    w("+4V0 --> D1 LM66200 (ideal diode, blocks reverse) --> VSYS --> FireBeetle BAT (J1)")
    w("VSYS --> Sunrise VBB, LED common anode (spliced onto the J1 + lead)")
    w("USB-C --> FireBeetle CN3165 --> VSYS at 4.2 V: D1 blocks, the cells are never charged;")
    w("          the Sunrise and the LED then run from USB too")
    w("VSYS --> FireBeetle TPS62A02 --> +3V3 (always on): ESP32-C6, SGP40, SHT40")
    w("+3V3 --[SW1 Pololu #2810, ON = GPIO2]--> +3V3_SEN --> SEN62, R3/R4 pull-ups")
    w("GPIO18 --> CO2_VDDIO --> Sunrise VDDIO, R5/R6 pull-ups  (EN = GPIO14, R7 pull-down)")
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
    w("Reserved / not used:")
    w("")
    w("| GPIO | reason |")
    w("|---|---|")
    for gpio, why in sorted(RESERVED_GPIO.items()):
        w(f"| {gpio} | {why} |")
    w("")
    w("## I2C buses (100 kHz)")
    w("")
    w("| bus | pins | address | device | pull-ups |")
    w("|---|---|---|---|---|")
    for name, b in BUSES.items():
        for a, ref in b["devices"].items():
            w(f"| {name} | {b['sda']} / {b['scl']} | 0x{a:02X} | "
              f"{ref} {PARTS_BY_REF[ref].value} | {b['pullups']} |")
    w("")
    w("The SEN62 and the Sunrise share the ESP32-C6's single HP I2C controller; the "
      "firmware routes it to one pin pair at a time and leaves the other pair as "
      "inputs, so nothing drives a sensor that is switched off.")
    w("")
    w("## Wiring list")
    w("")
    w("Every net is one or more wires. Solder, then heat-shrink every joint.")
    w("")
    w("| net | connections |")
    w("|---|---|")
    for net, conns in NETS.items():
        s = ", ".join(f"{r}.{p}" for r, p in conns)
        w(f"| `{net}` | {s} |")
    w("")
    w("## Inline parts: where they are soldered")
    w("")
    w("| ref | part | where |")
    w("|---|---|---|")
    for ref, where in SOLDER_AT.items():
        w(f"| {ref} | {PARTS_BY_REF[ref].value} | {where} |")
    w("")
    w("## Pins deliberately left open")
    w("")
    w("| pin | why |")
    w("|---|---|")
    for p in PARTS:
        for pin in p.nc:
            w(f"| {p.ref}.{pin} ({p.pins[pin]}) | see {p.ref} in the BOM |")
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
    return sum(p.price_eur * p.qty for p in PARTS if p.required or not required_only)


def bom_markdown() -> str:
    o = []
    w = o.append
    w("<!-- GENERATED by electronics/schematic/design.py - do not edit by hand. -->")
    w("")
    w("# Bill of materials (v1.3)")
    w("")
    w("Prices are single-unit European retail including VAT, rounded, as of "
      "September 2026. Mouser prices are shown there without VAT and are converted "
      "(x 1.19). They drift; treat them as an order of magnitude, not a quotation.")
    w("")
    w(f"**Total: EUR {total_cost(True):.2f}** including the first set of L91 cells "
      f"(EUR {PARTS_BY_REF['CELL'].price_eur:.2f}).")
    w("")
    w("Nothing on this list is a custom circuit board. Every electronic part is a "
      "finished module or a through-hole resistor (EDR-19).")
    w("")
    w("## Where the money goes")
    w("")
    groups = {
        "sensors (Sunrise, SEN62, SGP40, SHT40, cables)":
            ("U1", "U2", "U3", "U4", "W1", "W2", "W3"),
        "controller (FireBeetle)": ("M1",),
        "power (holder, fuse, regulator, ideal diode, switch)":
            ("BT1", "F1", "PS1", "D1", "SW1", "J1"),
        "first set of cells": ("CELL",),
        "LED, button, resistors": ("LED1", "LH1", "SW2", "C1", "R1", "R2", "R3", "R4",
                                   "R5", "R6", "R7", "R8", "R9", "R10"),
        "screws, wire, heat shrink": ("X1", "X2", "X3", "X5"),
    }
    w("| group | EUR |")
    w("|---|---|")
    for g, refs in groups.items():
        w(f"| {g} | {sum(PARTS_BY_REF[r].price_eur * PARTS_BY_REF[r].qty for r in refs):.2f} |")
    w("")
    w("The Sunrise is the single most expensive part and the reason v1.3 costs more "
      "than v1.2. It is also the reason the CO2 number can be trusted: the SEN63C's "
      "CO2 channel loses its self-calibration when it is power-cycled (EDR-16).")
    w("")
    w("## Parts")
    w("")
    w("| ref | qty | part | MPN | supplier | EUR | why this part |")
    w("|---|---|---|---|---|---|---|")
    for p in PARTS:
        if p.required:
            w(f"| {p.ref} | {p.qty} | {p.value} | `{p.mpn}` | {p.supplier} | "
              f"{p.price_eur:.2f} | {p.why} |")
    w(f"| | | | | | **{total_cost(True):.2f}** | |")
    w("")
    w("## Optional but recommended")
    w("")
    w("| part | MPN | EUR | why |")
    w("|---|---|---|---|")
    for p in PARTS:
        if not p.required:
            w(f"| {p.value} | `{p.mpn}` | {p.price_eur:.2f} | {p.why} |")
    w("")
    w("## Cells")
    w("")
    w("| cells | runtime, ECO, with margin | note |")
    w("|---|---|---|")
    w("| Energizer Ultimate Lithium L91 | 3.7 months | recommended |")
    w("| eneloop pro (NiMH) | 2.2 months | recharge in any NiMH charger, outside the device |")
    w("| alkaline | 2.1 months | cheap; remove when empty, they can leak |")
    w("| 1.5 V Li-ion with USB-C | 1.8 months | not recommended (docs/BATTERY_LIFE.md) |")
    w("")
    w("Figures from `tools/battery_calculator/model.py`.")
    w("")
    w("## Printed parts and hardware")
    w("")
    w("See `manufacturing/print-settings.md` for filament and `docs/ASSEMBLY.md` "
      "for the order of work.")
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
