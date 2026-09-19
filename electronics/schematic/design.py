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
    "VCELL":     Rail("VCELL", 3.00, 3.25, 3.65,
                      "1S4P LiFePO4 behind the HY2112 BMS: BUVLO 3.0 V, VBATREG 3.65 V"),
    "VBUS_EXT":  Rail("VBUS_EXT", 4.75, 5.00, 5.25,
                      "USB-C power socket J2: 5 V from any USB-C charger (5.1k on CC)"),
    "LOAD":      Rail("LOAD", 3.00, 3.25, 4.59,
                      "#6091 LOAD: the cells, or 4.5 V +-2 % from USB-C (power path)"),
    "+VREG":     Rail("+VREG", 3.82, 3.90, 3.98,
                      "PS1 Pololu S9V11E2A from LOAD, trimmed to 3.90 V +-2 %"),
    "VSYS":      Rail("VSYS", 3.80, 3.90, 4.24,
                      "FireBeetle battery input: +VREG through the LM66200, or the "
                      "FireBeetle's own CN3165 at 4.2 V +-1 % while a computer is on "
                      "its USB-C (then blocked from +VREG by the LM66200)"),
    "+3V3":      Rail("+3V3", 3.20, 3.30, 3.40, "FireBeetle TPS62A02 buck, always on"),
    "+3V3_SEN":  Rail("+3V3_SEN", 3.20, 3.30, 3.40, "+3V3 behind the Pololu #2810 switch"),
    "CO2_VDDIO": Rail("CO2_VDDIO", 0.0, 3.30, 3.40,
                      "GPIO18 driven high only while the Sunrise is enabled"),
    "GND":       Rail("GND", 0.0, 0.0, 0.0, "system ground = BMS P-"),
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
            "LM66200 in front of the battery input blocks it from the LFP cells "
            "(EDR-21).",
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
        vsupply_min=3.15, vsupply_max=3.6, i_typ_ma=75.0, i_max_ma=190.0,   # v0.92 Table 11
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
        ref="BH1", value="2 x 18650 holder, THT", mfr="Keystone", mpn="1049", qty=2,
        footprint="2 x 18650, 4 separate pins (see cad/openscad for the mount)",
        pins={"A+": "cell A +", "A-": "cell A -", "B+": "cell B +", "B-": "cell B -"},
        why="Two of them hold the four cells, each cell with its own contacts so "
            "each gets its own PICO fuse; UL94 V-0. Cells stay user-replaceable "
            "(EU Battery Regulation, relevant only if ever sold). BH1 stands for "
            "both holders in the netlist: pins A/B of the first, and of the second "
            "through BH2's identical wiring (NETLIST.md, inline parts).",
        price_eur=6.22, supplier="Mouser",
    ),
    Part(ref="CELL", value="LiFePO4 18650, 1.8 Ah, AER18650m2A2", mfr="Lithium Werks",
         mpn="AER18650m2A2 (320749-001)", footprint="18650, flat top", pins={}, qty=4,
         why="1S4P: 6.8-7.2 Ah at 3.2 V. LFP: no thermal runaway chemistry, >2000 "
             "cycles. One type, one batch, charged together in an XTAR MX4 (LFP) "
             "before the first fit, <= 20 mV apart (Power-Standard).",
         price_eur=5.90, supplier="akkuteile.de"),
    Part(ref="FU1", value="PICO II fuse 2 A fast, axial (cell A of each holder)",
         mfr="Littelfuse", mpn="0251002.MAT1L (Reichelt LITT 0251002.MAT)",
         footprint="axial, d2.8 x 7.1", pins={"1": "cell", "2": "B+"}, qty=2,
         why="One per cell, right at its + terminal: a shorted cell cannot be fed by "
             "its three neighbours. FU1 is the fuse of each holder's cell A, FU2 of "
             "cell B.",
         price_eur=0.82, supplier="Reichelt"),
    Part(ref="FU2", value="PICO II fuse 2 A fast, axial (cell B of each holder)",
         mfr="Littelfuse", mpn="0251002.MAT1L (Reichelt LITT 0251002.MAT)",
         footprint="axial, d2.8 x 7.1", pins={"1": "cell", "2": "B+"}, qty=2,
         why="As FU1, for each holder's cell B.", price_eur=0.82, supplier="Reichelt"),
    Part(
        ref="U5", value="LiFePO4 1S BMS 2.5 A (HY2112 + 8205A)", mfr="eremit",
        mpn="LiFePO4 1S 3,2V Schutzschaltung BMS 2,5A",
        footprint="small board, B+ B- P+ P- pads",
        pins={"B+": "cell +", "B-": "cell -", "P+": "pack +", "P-": "pack - = GND"},
        why="Over-charge 3.75 V, over-discharge 2.1 V, over-current - for LFP. Not a "
            "DW01/FS312F/HY2113 board: those only cut at >= 4.25 V. It switches the "
            "minus side, so P- is the ground of the whole device and B- goes "
            "nowhere else.",
        price_eur=1.79, supplier="eremit.de"),
    Part(ref="TH1", value="NTC 10 k, B = 3435 K", mfr="Semitec", mpn="103AT-2",
         footprint="glass bead, leads", pins={"1": "a", "2": "b"},
         why="On holder 1's inner cell, the middle of the pack, held by a finger on "
             "the battery door, Kapton between: the BQ25185 charges only between 0 and 60 C with it. The "
             "#6091's TH jumper is opened for it - with the jumper closed there is "
             "no temperature protection at all.",
         price_eur=1.50, supplier="Mouser"),
    Part(
        ref="U6", value="Adafruit bq25185 charger, set to LFP 3.65 V, 1 A", mfr="Adafruit",
        mpn="6091", footprint="31.75 x 25.40 mm, 4 x d2.5 holes on 26.67 x 20.32; left "
                              "header TH VS IS S1 S2 !CE, right header DCIN- DCIN+ "
                              "LOAD- LOAD+ BATT- BATT+",
        pins={"DCIN+": "5 V in", "DCIN-": "ground", "BATT+": "battery +",
              "BATT-": "battery -", "LOAD+": "system out", "LOAD-": "ground",
              "S1": "STAT1", "S2": "STAT2",
              "!CE": "charge enable, active low (pull-down on board)",
              "TH": "NTC (no GND pad beside it)", "VS": "VSET pad", "IS": "ISET pad"},
        why="The Power-Standard's core (TI BQ25185): LFP charge voltage set by "
            "resistor (jumper VS = 3.65 V; it cannot fall back to 4.2 V in "
            "software), power path (USB-C feeds the device, the rest charges), "
            "BUVLO 3.0 V, 6 h timer, NTC window 0-60 C, 4 uA from the cells, "
            "input OVP 18.5 V. Jumpers (board file, rev B1): cut VS (top) and "
            "bridge 3.65V (bottom); bridge 1 Amp (bottom) - the factory setting is "
            "500 mA, whatever the product page says (Adafruit issue #2); cut TH "
            "(top) for the NTC. The VS and IS header pads stay open (EDR-21).",
        price_eur=6.90, supplier="Adafruit / Berrybase / Mouser",
        nc=("VS", "IS"),
        vsupply_min=4.35, vsupply_max=18.5, i_max_ma=1000.0,
        datasheet="TI SLUSF65B; learn.adafruit.com bq25185 guide",
    ),
    Part(
        ref="PS1", value="Pololu S9V11E2A buck-boost, set to 3.90 V", mfr="Pololu",
        mpn="5719", footprint="10.9 x 16.5 x 4.0 mm, 4 pins: VOUT GND VIN EN",
        pins={"VIN": "input 2-16 V (3 V to start)", "GND": "ground",
              "VOUT": "output 2.5-9 V (trimpot)", "EN": "enable, 100k pull-up to VIN"},
        why="Turns LOAD (3.0-3.65 V on the cells, 4.5 V on USB-C) into a steady "
            "3.90 V for the FireBeetle's battery input: without it the FireBeetle's "
            "3.3 V rail would sag below the SEN62's 3.15 V minimum on the cells. "
            "Starts from ~3 V, which the BQ25185 guarantees (BUVLO release 3.15 V). "
            "EN open. Set the trimpot before connecting anything (ASSEMBLY).",
        price_eur=6.50, supplier="Eckstein / Pololu",
        nc=("EN",),
        vsupply_min=3.0, vsupply_max=16.0, i_max_ma=1700.0,
    ),
    Part(
        ref="D1", value="Adafruit LM66200 ideal diode breakout", mfr="Adafruit",
        mpn="5830", footprint="16.51 x 10.16 mm, 2 x 2.5 mm holes, 6 pins",
        pins={"VIN1": "input 1", "VIN2": "input 2", "GND": "ground", "VOUT": "output",
              "ON": "active-low enable", "ST": "status, open drain"},
        why="Feeds +VREG into the FireBeetle's battery input and blocks every current "
            "the other way: with a computer on the FireBeetle's USB-C its Li-ion "
            "charger (CN3165) holds its battery input at 4.2 V, 0.2 V above +VREG, "
            "past the 70 mV reverse-blocking threshold (TI SLVSG04) - so the "
            "FireBeetle's charger never reaches the LFP cells. VIN2 and ON to GND. "
            "1.3 uA quiescent. ST unused.",
        price_eur=3.50, supplier="Adafruit / Berrybase",
        nc=("ST",),
        vsupply_min=1.6, vsupply_max=5.5, i_max_ma=2500.0,
        datasheet="TI LM66200 SLVSG04; github.com/adafruit/Adafruit-LM66200-PCB",
    ),
    Part(
        ref="J2", value="USB-C power socket, sunken breakout", mfr="Adafruit",
        mpn="6050", footprint="20.32 x 13.97 mm, 2 x d2.5 plated holes, 8-pin 0.1in row",
        pins={"VBUS": "5 V", "GND": "ground", "D+": "data", "D-": "data",
              "CC1": "config", "CC2": "config", "SBU1": "sideband", "SBU2": "sideband"},
        why="The charging and power input, from any USB-C charger (18 W is plenty): "
            "the board's 5.1k resistors on CC ask for plain 5 V without "
            "negotiation. Power only; data lines open. Screwed to two posts.",
        price_eur=3.50, supplier="Adafruit / Mouser",
        nc=("D+", "D-", "CC1", "CC2", "SBU1", "SBU2"),
        vsupply_min=4.75, vsupply_max=5.25,
        datasheet="github.com/adafruit/Adafruit-Sunken-USB-Type-C-Breakout-PCB",
    ),
    Part(ref="D20", value="BAT43 Schottky, DO-35", mfr="Vishay", mpn="BAT43", qty=1,
         footprint="axial", pins={"A": "anode", "K": "cathode"},
         why="PWR-K ladder: the charger's STAT2 (CHG_N) pulls the node down through "
             "it; the diode keeps the #6091's LED anode voltage off the GPIO.",
         price_eur=0.10, supplier="Reichelt"),
    Part(ref="D21", value="BAT43 Schottky, DO-35", mfr="Vishay", mpn="BAT43", qty=1,
         footprint="axial", pins={"A": "anode", "K": "cathode"},
         why="PWR-K ladder: STAT1 (FLT_N), as D20.",
         price_eur=0.10, supplier="Reichelt"),
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
         why="VBAT_S at GPIO3, directly at the pin (the lead is an antenna).",
         price_eur=0.10, supplier="Reichelt / Mouser"),
    Part(ref="C2", value="100 nF ceramic, THT", mfr="KEMET", mpn="C320C104K5R5TA",
         footprint="radial 2.54 mm", pins={"1": "+", "2": "-"},
         why="PWR-K node at GPIO4, directly at the pin.",
         price_eur=0.10, supplier="Reichelt / Mouser"),
    _r("R1", "470 k", "VBAT_S top: cell / 2, 3.65 V -> 1.83 V at GPIO3 (ADC 6 dB, 0-1.9 V). "
                      "3.5 uA from the cells."),
    _r("R2", "470 k", "VBAT_S bottom."),
    _r("R20", "100 k", "PWR-K: from USB 5 V to the node (EXT)."),
    _r("R21", "150 k", "PWR-K: node to GND. 5.25 V x 150/250 = 3.15 V at most on GPIO4."),
    _r("R22", "150 k", "PWR-K: D20 to STAT2 (CHG_N): charging pulls the node to 2.08-2.35 V."),
    _r("R23", "33 k", "PWR-K: D21 to STAT1 (FLT_N): a fault pulls the node to 0.99-1.34 V."),
    _r("R3", "4.7 k", "SEN62 SDA pull-up to +3V3_SEN: it switches off with the sensor, "
                     "so no pull-up back-feeds the unpowered SEN62."),
    _r("R4", "4.7 k", "SEN62 SCL pull-up, as R3."),
    _r("R5", "10 k", "Sunrise SDA pull-up to CO2_VDDIO (GPIO18). Senseair recommend "
                    "5-15k. Solder it across Sunrise pins 3 and 4."),
    _r("R6", "10 k", "Sunrise SCL pull-up, across pins 3 and 5."),
    _r("R7", "100 k", "Sunrise EN pull-down: EN must never float (PSP12440), also not "
                     "while the ESP32 is in reset."),
    _r("R8", "1 k", "Red: (3.9 V - 2.0 V) / 1k = 1.9 mA (2.2 mA at 4.2 V on external power)."),
    _r("R9", "330", "Green: (3.9 V - 3.1 V) / 330 = 2.4 mA."),
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
    Part(ref="X7", value="M3 x 10 self-tapping screws for plastic, 4", mfr="generic",
         mpn="e.g. ejot PT / DIN 7981 3.0x10", footprint="-", pins={},
         why="The two battery holders, two each through their floor into the bosses.",
         price_eur=1.00, supplier="Reichelt"),
    Part(ref="X2", value="M2 x 6 screws + M2 nuts, 4 each", mfr="generic", mpn="DIN 912 M2x6",
         footprint="-", pins={}, why="FireBeetle to its standoffs.", price_eur=1.00,
         supplier="Reichelt"),
    Part(ref="X3", value="silicone wire: 22 AWG (0.34 mm2) red/black + 26 AWG, 4 colours",
         mfr="generic", mpn="-", footprint="-", pins={},
         why="22 AWG for everything that carries the cells' or the charger's current "
             "(cells, fuses, BMS, #6091 BATT/LOAD/DCIN, regulator, LM66200, J1): "
             "Power-Standard checklist E9 asks >= 24 AWG up to 1 A. 26 AWG for "
             "signals and sensors.",
         price_eur=4.00, supplier="local"),
    Part(ref="X6", value="Kapton tape 10 mm", mfr="generic", mpn="-", footprint="-", pins={},
         why="Insulates the NTC bead against the cell's mantle; the door's finger "
             "holds it in place (checklist E3).", price_eur=3.00, supplier="local"),
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
    # ---- cells and protection ---------------------------------------------------
    # BH1 stands for both holders, FU1/FU2 for the fuses of their cells A/B:
    # every cell's + goes through its own PICO fuse to PACK_B+, every cell's -
    # to CELL_B-.
    "CELL_A+":   [("BH1", "A+"), ("FU1", "1")],
    "CELL_B+":   [("BH1", "B+"), ("FU2", "1")],
    "PACK_B+":   [("FU1", "2"), ("FU2", "2"), ("U5", "B+")],
    "CELL_B-":   [("BH1", "A-"), ("BH1", "B-"), ("U5", "B-")],
    "VCELL":     [("U5", "P+"), ("U6", "BATT+"), ("R1", "1")],
    # ---- charger and power path ----------------------------------------------
    "VBUS_EXT":  [("J2", "VBUS"), ("U6", "DCIN+"), ("R20", "1")],
    "LOAD":      [("U6", "LOAD+"), ("PS1", "VIN")],
    "+VREG":     [("PS1", "VOUT"), ("D1", "VIN1")],
    "NTC":       [("U6", "TH"), ("TH1", "1")],
    "CHG_N":     [("U6", "S2"), ("D20", "K")],
    "FLT_N":     [("U6", "S1"), ("D21", "K")],
    "CHG_R":     [("D20", "A"), ("R22", "1")],
    "FLT_R":     [("D21", "A"), ("R23", "1")],
    "VSYS":      [("D1", "VOUT"), ("J1", "+"), ("M1", "BAT+"), ("U4", "2"), ("LED1", "A")],
    "+3V3":      [("M1", "3V3"), ("SW1", "VIN"), ("U2", "3V3"), ("U3", "VCC")],
    "+3V3_SEN":  [("SW1", "VOUT"), ("U1", "1"), ("U1", "6"), ("R3", "1"), ("R4", "1")],
    "GND":       [("U5", "P-"), ("U6", "BATT-"), ("U6", "DCIN-"), ("U6", "LOAD-"),
                  ("TH1", "2"), ("R21", "2"), ("C2", "2"),
                  ("PS1", "GND"), ("D1", "GND"), ("D1", "VIN2"),
                  ("D1", "ON"), ("J2", "GND"), ("J1", "-"), ("M1", "BAT-"), ("M1", "GND"),
                  ("SW1", "GND"), ("U1", "2"), ("U1", "5"), ("U2", "GND"),
                  ("U3", "GND"), ("U4", "1"), ("U4", "6"), ("R2", "2"), ("C1", "2"),
                  ("R7", "2"), ("SW2", "2")],
    # ---- measurement / control ---------------------------------------------
    "VBAT_S":    [("R1", "2"), ("R2", "1"), ("C1", "1"), ("M1", "IO3")],
    "PWR_K":     [("R20", "2"), ("R21", "1"), ("R22", "2"), ("R23", "2"), ("C2", "1"),
                  ("M1", "IO4")],
    "CE":        [("M1", "IO5"), ("U6", "!CE")],
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
    "FU1": "one per holder, at cell A's + contact pin, under heat shrink",
    "FU2": "one per holder, at cell B's + contact pin, under heat shrink",
    "R1": "from the BMS P+ lead to FireBeetle IO3",
    "R2": "at FireBeetle IO3 to GND, together with C1",
    "C1": "at FireBeetle IO3 to GND",
    "R20": "from the J2/#6091 DCIN+ wire to the PWR-K node, at FireBeetle IO4",
    "R21": "PWR-K node to GND at FireBeetle IO4, together with C2",
    "C2": "at FireBeetle IO4 to GND",
    "D20": "cathode on the #6091's S2 pad, anode to R22",
    "D21": "cathode on the #6091's S1 pad, anode to R23",
    "R22": "from D20 to the PWR-K node at IO4",
    "R23": "from D21 to the PWR-K node at IO4",
    "TH1": "between #6091 TH and its GND (the DCIN- pad), bead on the pack's middle cell",
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
# MTMS/MTDI (GPIO4/5) only set the SDIO slave's timing, which is not used;
# their level at reset does not matter (battery session approval, EDR-21).
STRAPPING_OK = {4, 5}
C6_ROM_UART_TX = 16      # the ROM boot log toggles it after every reset

GPIO_MAP = [
    GpioUse(0,  "(on board)", "REG_ADC", "analog in", True, False,
            "FireBeetle 1M/1M divider from VSYS: 3.9 V on the cells, 4.2 V on external power"),
    GpioUse(1,  "1",   "BTN",       "in",    True,  False, "wake source, internal pull-up, active low"),
    GpioUse(2,  "2",   "SEN_EN",    "out",   True,  False, "held LOW in sleep"),
    GpioUse(3,  "3",   "VBAT_S",    "analog in", True, False, "cell / 2 (470k/470k), ADC1_CH3, 6 dB"),
    GpioUse(4,  "4",   "PWR_K",     "analog in", True, True,
            "PWR-K ladder (EXT, CHG_N, FLT_N), ADC1_CH4, 12 dB; <= 3.15 V"),
    GpioUse(5,  "5",   "CE",        "out / in",  True, True,
            "#6091 !CE: high = charge pause, input = charging (pull-down on the board)"),
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
PWR_K_IDLE_MIN_V = 2.65     # pwr_std PWR_K_IDLE_MIN
ADC_6DB_MAX_V = 1.9         # ESP32-C6 ADC, 6 dB attenuation (Power-Standard PWR-7)
TPS62A02_DROPOUT_V = 0.35   # FireBeetle buck at ~350 mA radio peaks, 100 % duty
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
                 "U4": ("2", "VSYS"), "PS1": ("VIN", "LOAD"), "D1": ("VIN1", "+VREG"),
                 "J2": ("VBUS", "VBUS_EXT"), "U6": ("DCIN+", "VBUS_EXT"),
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
            "LM66200 output side must tolerate the FireBeetle charger's 4.2 V")

    # 5. the cells (review checklist K1/K2): every cell through its own fuse
    #    to the BMS, the BMS to the charger; P- is GND and B- goes nowhere else
    e.check(sorted(NETS["CELL_A+"]) == [("BH1", "A+"), ("FU1", "1")]
            and sorted(NETS["CELL_B+"]) == [("BH1", "B+"), ("FU2", "1")],
            "each cell's + may only go to its own fuse")
    e.check(set(NETS["PACK_B+"]) == {("FU1", "2"), ("FU2", "2"), ("U5", "B+")},
            "the fused cells must meet only at the BMS's B+")
    e.check(set(NETS["CELL_B-"]) == {("BH1", "A-"), ("BH1", "B-"), ("U5", "B-")},
            "B- may connect nothing but the cells and the BMS")
    e.check(("U5", "P-") in NETS["GND"], "BMS P- must be the system ground")
    e.check(_nets_of("U5", "P+") == ["VCELL"] and ("U6", "BATT+") in NETS["VCELL"],
            "BMS P+ must feed the charger's BATT+")
    e.check(PARTS_BY_REF["FU1"].qty + PARTS_BY_REF["FU2"].qty == PARTS_BY_REF["CELL"].qty == 4
            and PARTS_BY_REF["BH1"].qty * 2 == PARTS_BY_REF["CELL"].qty,
            "one fuse per cell, two cells per holder")
    e.check(_nets_of("U6", "TH") == ["NTC"] and _nets_of("TH1", "2") == ["GND"],
            "the NTC must sit between the charger's TH and GND (checklist K3)")

    # 6. power path: the FireBeetle's own Li-ion charger never reaches the
    #    LFP cells (K4): VSYS is separated from +VREG by D1 alone, and D1
    #    blocks whenever the CN3165 lifts VSYS above +VREG
    e.check(("D1", "VIN1") in NETS["+VREG"] and ("D1", "VOUT") in NETS["VSYS"],
            "D1 must sit between +VREG and VSYS")
    shared = {c for c in NETS["+VREG"]} & {c for c in NETS["VSYS"]}
    e.check(not shared, f"+VREG and VSYS share {shared}")
    e.check(4.20 * 0.99 - RAILS["+VREG"].vmax > LM66200_VRCB_MAX,
            "charger voltage is not far enough above +VREG for D1 to block")
    e.check(_nets_of("D1", "ON") == ["GND"] and _nets_of("D1", "VIN2") == ["GND"],
            "D1: ON low (enabled), VIN2 unused at GND")
    e.check(("PS1", "VIN") in NETS["LOAD"] and len(NETS["LOAD"]) == 2,
            "LOAD feeds the regulator and nothing else")
    # K5: the SEN62's rail stays >= 3.15 V over the whole LOAD range: PS1 turns
    # 3.0-4.59 V into 3.90 V, which keeps the FireBeetle's buck in regulation
    e.check(RAILS["LOAD"].vmin >= PARTS_BY_REF["PS1"].vsupply_min,
            "PS1 must start at the BQ25185's lowest LOAD voltage (BUVLO)")
    e.check(RAILS["VSYS"].vmin - TPS62A02_DROPOUT_V >= RAILS["+3V3"].vmin,
            "VSYS too low for the FireBeetle's buck to hold 3.3 V")
    e.check(RAILS["+3V3_SEN"].vmin >= PARTS_BY_REF["U1"].vsupply_min,
            "the SEN62's rail can fall below its 3.15 V minimum")

    # 7. current: regulator, switch, charger input
    load_ma = ((PARTS_BY_REF["U1"].i_max_ma + ESP_TX_PEAK_MA
                + PARTS_BY_REF["U2"].i_max_ma) * 3.3 / (0.9 * 3.9)
               + PARTS_BY_REF["U4"].i_max_ma + 3 * 3.0)
    e.check(load_ma < PARTS_BY_REF["PS1"].i_max_ma * 0.6,
            f"worst-case regulator load {load_ma:.0f} mA leaves too little margin")
    # K8: peaks on LOAD from the cells at 3.0 V, through an 80 % regulator
    peak_load_ma = load_ma * 3.9 / (RAILS["LOAD"].vmin * PS1_EFF_MIN)
    e.check(peak_load_ma <= 2000.0, f"LOAD peak {peak_load_ma:.0f} mA exceeds 2 A (PWR-7)")
    e.check(PARTS_BY_REF["SW1"].i_max_ma >= 2 * PARTS_BY_REF["U1"].i_max_ma,
            "SW1 must carry the SEN62's peaks with margin")
    e.check(False, "SW1 has no soft start: the SEN62's switch-on step lands on the "
                   "FireBeetle's 3.3 V buck - verify on the bench (TESTING T-P3)", warn=True)

    # 8. ADC ranges and GPIO levels (K6, K7)
    v_vbat_s = RAILS["VCELL"].vmax / 2
    e.check(v_vbat_s <= ADC_6DB_MAX_V, f"VBAT_S {v_vbat_s:.2f} V exceeds the 6 dB range")
    v_ladder = RAILS["VBUS_EXT"].vmax * 150e3 / (100e3 + 150e3)
    e.check(v_ladder <= 3.6, f"PWR-K node {v_ladder:.2f} V exceeds the pad's 3.6 V")
    # above ~2.9 V the 12 dB range clips; the highest threshold must sit below
    e.check(PWR_K_IDLE_MIN_V < ADC_MAX_V,
            "the PWR-K 'idle' threshold lies where the ADC clips")
    e.check(RAILS["VSYS"].vmax / 2 <= ADC_MAX_V, "VSYS / 2 exceeds the ADC range")
    e.check(("D20", "K") in NETS["CHG_N"] and ("D21", "K") in NETS["FLT_N"],
            "STAT pins reach the ladder only through their Schottky diodes")
    thr = None
    hdr = os.path.join(ROOT, "firmware/components/pwr_std/include/pwr_std.h")
    for line in open(hdr):
        if line.startswith("#define PWR_K_USB_MIN"):
            thr = float(line.split()[2].rstrip("f"))
    e.check(thr is not None and RAILS["VBUS_EXT"].vmin * 150e3 / 250e3 - 0.35 > thr,
            "PWR-K: USB present does not clear pwr_std's threshold")

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
        e.check(g.gpio not in C6_STRAPPING or g.gpio in STRAPPING_OK,
                f"GPIO{g.gpio} ({g.signal}) is a strapping pin")
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
                    "SEN_SCL", "LED_R", "LED_G", "LED_B", "BUTTON", "VBAT_S", "PWR_K", "CE"):
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
    w("# AIR CHECK - wiring list and pin map (v1.4, no custom PCB)")
    w("")
    w("## Power tree")
    w("")
    w("```")
    w("4 x AER18650m2A2 (2 x Keystone 1049) --PICO 2 A each--> CELL_B+ --BMS HY2112--> VCELL")
    w("   BMS P- = system GND; B- goes nowhere else")
    w("USB-C socket J2 (5 V) --> #6091 DCIN+ (TI BQ25185: LFP 3.65 V, 1 A, NTC, 6 h timer)")
    w("VCELL <--> #6091 BATT+        NTC 103AT-2 on the middle cell --> #6091 TH")
    w("#6091 LOAD (3.0-3.65 V on the cells, 4.5 V on USB-C) --> PS1 S9V11E2A --> +VREG 3.90 V")
    w("+VREG --> D1 LM66200 VIN1 (VIN2, ON at GND) --> VSYS --> FireBeetle BAT (J1)")
    w("VCELL --R1 470k--+--R2 470k-- GND      VBAT_S on GPIO3 (C1 100 nF)")
    w("VBUS --R20 100k--+--R21 150k-- GND     PWR_K on GPIO4 (C2 100 nF)")
    w("                 +--R22 150k--|<D20-- #6091 S2 (CHG_N)")
    w("                 +--R23  33k--|<D21-- #6091 S1 (FLT_N)")
    w("GPIO5 --> #6091 !CE (high = charge pause, input = charging)")
    w("VSYS --> Sunrise VBB, LED common anode (spliced onto the J1 + lead)")
    w("Computer on the FireBeetle's USB-C --> its CN3165 --> VSYS 4.2 V: D1 blocks it from")
    w("          +VREG, so the FireBeetle's Li-ion charger never reaches the LFP cells")
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
    w("# Bill of materials (v1.4)")
    w("")
    w("Prices are single-unit European retail including VAT, rounded, as of "
      "September 2026. Mouser prices are shown there without VAT and are converted "
      "(x 1.19). They drift; treat them as an order of magnitude, not a quotation.")
    w("")
    w(f"**Total: EUR {total_cost(True):.2f}** including the four LiFePO4 cells "
      f"(EUR {PARTS_BY_REF['CELL'].price_eur * PARTS_BY_REF['CELL'].qty:.2f}).")
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
        "power module C (charger, BMS, holders, fuses, NTC, regulator, ideal diode, "
        "switch, USB-C socket)":
            ("U6", "U5", "BH1", "FU1", "FU2", "TH1", "PS1", "D1", "SW1", "J1", "J2", "D20", "D21"),
        "cells (4 x LiFePO4)": ("CELL",),
        "LED, button, resistors": ("LED1", "LH1", "SW2", "C1", "C2", "R1", "R2", "R3", "R4",
                                   "R5", "R6", "R7", "R8", "R9", "R10", "R20", "R21",
                                   "R22", "R23"),
        "screws, wire, heat shrink, Kapton": ("X1", "X2", "X3", "X5", "X6", "X7"),
    }
    w("| group | EUR |")
    w("|---|---|")
    for g, refs in groups.items():
        w(f"| {g} | {sum(PARTS_BY_REF[r].price_eur * PARTS_BY_REF[r].qty for r in refs):.2f} |")
    w("")
    w("The Sunrise is the single most expensive part. It is also the reason the CO2 "
      "number can be trusted: the SEN63C's CO2 channel loses its self-calibration "
      "when it is power-cycled (EDR-16). The power module follows the "
      "Power-Standard's module C (EDR-21); the XTAR MX4 for the first charge of "
      "the cells is not included.")
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
    w("## Runtime")
    w("")
    w("| power | runtime, ECO, with margin |")
    w("|---|---|")
    w("| USB-C charger on J2 | unlimited; the cells are topped up about monthly |")
    w("| the 1S4P LiFePO4 pack alone | 2.9 months (docs/BATTERY_LIFE.md) |")
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
