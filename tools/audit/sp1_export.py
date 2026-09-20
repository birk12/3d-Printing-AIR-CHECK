#!/usr/bin/env python3
"""Emit SP-1 (AIR CHECK v1.4) for the Elektronik-Audit's schematic tool.

The audit (PA-01) asks for one machine-checkable file per device.  We already
have the wiring in `electronics/schematic/design.py`; writing the TOML by hand
would fork it, which is exactly the failure mode PA-01 was written against.  So
this script reads `design.py` and adds what that file does not carry:

  * the direction of every pin                     (A3, A9)
  * a reason for every pin that stays open         (A10)
  * supply limits per part, from the datasheets    (A7)
  * a peak current per rail, radio burst included  (A8)
  * resistor tolerance and ADC range at ADC nets   (A12)

Run:  python3 tools/audit/sp1_export.py [--out <file.toml>]
Then: python3 <Elektronik-Audit>/tools/schematic.py <file.toml>
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_OUT = os.path.normpath(os.path.join(
    ROOT, "..", "Elektronik-Audit", "einreichung", "SP-1_air-check.toml"))


def _load_design():
    path = os.path.join(ROOT, "electronics", "schematic", "design.py")
    spec = importlib.util.spec_from_file_location("aircheck_design", path)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules["aircheck_design"] = mod        # dataclasses need the module
    spec.loader.exec_module(mod)
    return mod


d = _load_design()

# ---------------------------------------------------------------------------
# Pin directions.  "passiv" is the default for two-lead parts.
# ---------------------------------------------------------------------------

ROLES: dict[str, dict[str, str]] = {
    "M1": {
        "3V3": "supply_out", "GND": "supply_in", "VIN": "supply_in",
        "BAT+": "supply_in", "BAT-": "supply_in",
        "IO1": "in", "IO2": "out", "IO3": "analog_in", "IO4": "analog_in",
        "IO5": "out", "IO6": "bidir", "IO7": "bidir", "IO8": "bidir",
        "IO9": "bidir", "IO14": "out", "IO15": "out", "IO16": "out",
        "IO17": "bidir", "IO18": "out", "SDA": "bidir", "SCL": "bidir",
        "IO21": "bidir", "IO22": "out", "IO23": "out", "RST": "in",
    },
    "U1": {"1": "supply_in", "2": "supply_in", "3": "bidir", "4": "bidir",
           "5": "supply_in", "6": "supply_in"},
    "U2": {"3V3": "supply_in", "GND": "supply_in", "SDA": "bidir", "SCL": "bidir"},
    "U3": {"VCC": "supply_in", "GND": "supply_in", "SDA": "bidir", "SCL": "bidir"},
    "U4": {"1": "supply_in", "2": "supply_in", "3": "supply_in", "4": "bidir",
           "5": "bidir", "6": "in", "7": "out", "8": "supply_out", "9": "in"},
    "U5": {"B+": "supply_in", "B-": "supply_in", "P+": "supply_out", "P-": "supply_out"},
    "U6": {"DCIN+": "supply_in", "DCIN-": "supply_in", "BATT+": "supply_out",
           "BATT-": "supply_out", "LOAD+": "supply_out", "LOAD-": "supply_out",
           "S1": "out", "S2": "out", "!CE": "in", "TH": "analog_in",
           "VS": "passiv", "IS": "passiv"},
    "PS1": {"VIN": "supply_in", "GND": "supply_in", "VOUT": "supply_out", "EN": "in"},
    "D1": {"VIN1": "supply_in", "VIN2": "supply_in", "GND": "supply_in",
           "VOUT": "supply_out", "ON": "in", "ST": "out"},
    "J2": {"VBUS": "supply_out", "GND": "supply_out", "D+": "passiv", "D-": "passiv",
           "CC1": "passiv", "CC2": "passiv", "SBU1": "passiv", "SBU2": "passiv"},
    "SW1": {"VIN": "supply_in", "GND": "supply_in", "VOUT": "supply_out",
            "ON": "in", "SW": "passiv"},
}
CELL_ROLES = {"+": "supply_out", "-": "supply_out"}

# ---------------------------------------------------------------------------
# Open pins: one reason each, in plain words (A10).
# ---------------------------------------------------------------------------

NC_REASONS: dict[str, dict[str, str]] = {
    "M1": {
        "VIN": "USB-5-V-Pad der eigenen USB-C-Buchse des FireBeetle; die Buchse "
               "dient nur zum Konfigurieren und Flashen, Strom kommt ueber BAT+",
        "IO8": "Strapping-Pin, bleibt unbeschaltet (design.py RESERVED_GPIO)",
        "IO9": "Strapping-Pin, BOOT-Taster auf dem Board",
        "IO15": "Strapping-Pin, gruene Board-LED; die Firmware haelt ihn low, "
                "keine Leitung nach aussen",
        "RST": "Reset-Taster auf dem Board, keine externe Leitung",
    },
    "U4": {
        "7": "nRDY nicht genutzt: die Firmware wartet die Worst-Case-Messzeit ab "
             "(Senseair TDE7318); spart eine Leitung und einen GPIO",
        "8": "DVCC ist ein Ausgang des internen Reglers und bleibt laut PSP12440 offen",
    },
    "U6": {
        "VS": "VSET wird auf dem Adafruit-Board per Jumper gesetzt (VS auftrennen, "
              "3.65V bruecken); das Header-Pad bleibt offen, damit die LFP-Ladeschluss"
              "spannung nicht versehentlich verstellt wird",
        "IS": "ISET ebenso per Jumper (1 Amp gebrueckt); Pad offen",
    },
    "PS1": {
        "EN": "auf dem Pololu-Board ueber 100 k an VIN hochgezogen, der Regler ist "
              "damit dauerhaft eingeschaltet",
    },
    "D1": {
        "ST": "Status-Ausgang (open drain) nicht ausgewertet: welcher Eingang aktiv "
              "ist, sagt die Firmware ueber PWR-K",
    },
    "J2": {
        "D+": "reine Stromversorgung, keine Datenverbindung",
        "D-": "reine Stromversorgung, keine Datenverbindung",
        "CC1": "5,1-k-Widerstaende sitzen auf dem Breakout, sie fordern 5 V ohne "
               "Verhandlung an",
        "CC2": "wie CC1",
        "SBU1": "Sideband nicht genutzt",
        "SBU2": "Sideband nicht genutzt",
    },
    "SW1": {
        "SW": "Kontakt des Schiebeschalters bleibt offen (Schalter in Stellung OFF), "
              "damit allein der ON-Pin schaltet",
    },
}

# ---------------------------------------------------------------------------
# Supply limits per part: (rail, vmin, vmax, source).  From the datasheets.
# ---------------------------------------------------------------------------

SUPPLY: dict[str, tuple[str, float, float, str]] = {
    "M1":  ("VSYS", 3.0, 4.25,
            "DFRobot FireBeetle 2 ESP32-C6: Akku-Eingang, TPS62A02 VIN 2,5-5,5 V, "
            "CN3165-Lader 4,2 V"),
    "U1":  ("+3V3_SEN", 3.1525, 3.6, "Sensirion SEN6x DS v0.92, Tab. 11"),
    "U2":  ("+3V3", 1.7, 3.6, "Sensirion SGP40 DS v1.2"),
    "U3":  ("+3V3", 3.2, 5.0, "Seeed Grove SHT40 (Board 3,3/5 V; SHT4x selbst 1,08-3,6 V)"),
    "U4":  ("VSYS", 3.05, 5.5, "Senseair Sunrise PSP12440: VBB 3,05-5,5 V"),
    "U6":  ("VBUS_EXT", 2.7, 5.5,
            "TI SLUSF65B 5.4: VIN 2,7-5,5 V empfohlen (OVP erst bei 18,5 V)"),
    "PS1": ("LOAD", 3.0, 16.0, "Pololu S9V11E2A: 2-16 V, Start ab 3 V"),
    "D1":  ("+VREG", 1.6, 5.5, "TI LM66200 SLVSG04: 1,6-5,5 V"),
    "SW1": ("+3V3", 1.8, 16.0, "Pololu 2810 LV: 1,8-16 V empfohlen"),
    "C3":  ("+3V3", 0.0, 6.3, "Panasonic FR: 6,3 V Nennspannung"),
    "C1":  ("VBAT_S", 0.0, 50.0, "KEMET C320C104: 50 V"),
    "C2":  ("PWR_K", 0.0, 50.0, "KEMET C320C104: 50 V"),
}

# Current a part can deliver into the rail it feeds (A8), with its source.
I_MAX: dict[str, tuple[float, str]] = {
    "J2":  (3000, "USB-C-Buchse und 22-AWG-Leitung; das Netzteil liefert 18 W"),
    "U5":  (2500, "eremit 1S-LiFePO4-BMS, 2,5 A Dauerstrom"),
    "U6":  (3125, "TI SLUSF65B 5.4: IBAT (BAT nach SYS) 3,125 A. Am USB begrenzt "
                  "ILIM den Eingang auf 995-1100 mA (Setting 1050 mA, 5.5); LOAD "
                  "liegt dann bei 4,5 V, und Spitzen deckt der Supplement-Modus "
                  "aus den Zellen (VBSUP1 40 mV)"),
    "PS1": (1000, "Pololu-Kennlinie 'Maximum Continuous Output Current': bei VIN "
                  "3,0 V rund 1,1 A fuer VOUT 3,3 V und 0,8 A fuer VOUT 5 V, "
                  "interpoliert >= 1,0 A fuer unsere 3,9 V. Beim Start begrenzt "
                  "das Board auf ~700 mA, bis der Ausgang steht - da haengt nur "
                  "der ESP im Boot daran"),
    "D1":  (2500, "TI LM66200: 2,5 A je Kanal"),
    "M1":  (2000, "TI TPS62A02 auf dem FireBeetle: IOUT 0-2 A (DS 6.3)"),
    "SW1": (6000, "Pololu 2810 LV: 6 A Dauerstrom"),
}
CELL_I_MAX = (5400.0, "Lithium Werks AER18650m2A2: 3 C Dauerentladung bei 1,8 Ah")
FUSE_I_MAX = (2000.0, "Littelfuse PICO II 251, 2 A Nennstrom (flink)")

# ---------------------------------------------------------------------------
# Rails: peak current including the radio burst (A8).
#
# Worst case, everything at once:
#   ESP32-C6 802.15.4 TX          350 mA   design.py ESP_TX_PEAK_MA; the
#                                          datasheet (v1.5 Tab. 5-9) gives
#                                          305 mA at +20 dBm, was THREAD.md
#                                          leaves as the default
#   SEN62 Spitze                  190 mA   SEN6x DS v0.92 Tab. 11 (2 ms Puls)
#   SGP40                           4 mA   DS v1.2, Heizpuls
#   SHT40                         0.5 mA   SHT4x DS
#   I2C-Pull-ups                  1.4 mA   2 x 4,7 k an 3,3 V
#   Sunrise VBB waehrend der Messung 125 mA  Senseair PSP12440
#   RGB-LED, alle drei Zweige     6.7 mA   1,9 + 2,4 + 2,4 mA
# ---------------------------------------------------------------------------

BUDGET = d.power_budget()

RAIL_PEAK: dict[str, tuple[float, str]] = {
    "VCELL":     (BUDGET["VCELL"], "wie LOAD: bei leerer Zelle zieht der Pololu "
                                   "den groessten Strom"),
    "VBUS_EXT":  (BUDGET["VBUS_EXT"], "BQ25185 IIN (SLUSF65B Tab. 5.4): mehr als "
                                      "1,1 A nimmt der Lader nicht auf"),
    "LOAD":      (BUDGET["LOAD"], "Eingangsstrom von PS1 im schlechtesten Punkt: "
                                  "VSYS-Spitze x 3,9 V / (3,0 V x 0,80). Am USB "
                                  "liegt LOAD bei 4,5 V, dort sind es 0,72 A"),
    "+VREG":     (BUDGET["+VREG"], "VSYS-Spitze; der LM66200 gibt sie unveraendert "
                                   "weiter"),
    "VSYS":      (BUDGET["VSYS"], "+3V3-Spitze x 3,3 V / (0,90 x 3,80 V) durch den "
                                  "TPS62A02, dazu Sunrise 125 mA und die drei "
                                  "LED-Zweige"),
    "+3V3":      (BUDGET["+3V3"], "350 mA TX + 190 mA SEN62-Puls + 4 mA SGP40 + "
                                  "0,5 mA SHT40 + 1,4 mA Pull-ups, alles zugleich "
                                  "angenommen"),
    "+3V3_SEN":  (BUDGET["+3V3_SEN"], "SEN62-Spitze 190 mA (2 ms) plus die beiden "
                                      "4,7-k-Pull-ups"),
    "CO2_VDDIO": (BUDGET["CO2_VDDIO"], "nur die beiden 10-k-Pull-ups des Sunrise"),
    "CELL1A+":   (BUDGET["VCELL"], "im Fehlerfall traegt ein Zweig den ganzen "
                                   "Packstrom; die PICO-Sicherung haelt 2 A"),
    "CELL1B+":   (BUDGET["VCELL"], "wie CELL1A+"),
    "CELL2A+":   (BUDGET["VCELL"], "wie CELL1A+"),
    "CELL2B+":   (BUDGET["VCELL"], "wie CELL1A+"),
    "PACK_B+":   (BUDGET["VCELL"], "Packstrom, verteilt auf vier Sicherungen"),
    "CELL_B-":   (BUDGET["VCELL"], "Rueckleiter des Packs, vor den BMS-FETs"),
}

# Rails whose source part is not the one design.py names in Rail.source.
RAIL_SOURCE_REF = {
    "VCELL": "U5", "VBUS_EXT": "J2", "LOAD": "U6", "+VREG": "PS1", "VSYS": "D1",
    "+3V3": "M1", "+3V3_SEN": "SW1", "CO2_VDDIO": "M1",
    "CELL1A+": "BT1", "CELL1B+": "BT2", "CELL2A+": "BT3", "CELL2B+": "BT4",
    "PACK_B+": "FU1", "CELL_B-": "BT1",
}

# Cell-side rails that design.py does not list under RAILS.
EXTRA_RAILS: dict[str, tuple[float, float, float, str]] = {
    "CELL1A+": (2.10, 3.25, 3.75, "Zelle 1A, LiFePO4 AER18650m2A2; das BMS trennt "
                                  "bei 3,75 V nach oben und 2,1 V nach unten"),
    "CELL1B+": (2.10, 3.25, 3.75, "Zelle 1B, wie 1A"),
    "CELL2A+": (2.10, 3.25, 3.75, "Zelle 2A, wie 1A"),
    "CELL2B+": (2.10, 3.25, 3.75, "Zelle 2B, wie 1A"),
    "PACK_B+": (2.10, 3.25, 3.75, "vier Zellen parallel hinter ihren Sicherungen, "
                                  "vor dem BMS"),
    "CELL_B-": (0.00, 0.00, 3.75, "B- des BMS: liegt auf GND, solange das BMS "
                                  "leitet, und steigt auf die Packspannung, wenn "
                                  "es abschaltet"),
}

ANALOG_NETS = {
    "VBAT_S": {
        "vmin": 1.05, "vnom": 1.625, "vmax": 1.875, "vmax_worstcase": 1.894,
        "adc_atten_mv": 1900,
        "pegel_definiert_durch": "Teiler R1/R2 (470 k / 470 k) aus VCELL, C1 100 nF",
        "hinweis": "ADC1_CH3 mit 6 dB, Messbereich 0-1900 mV (ESP32-C6 DS v1.5 "
                   "Tab. 5-6). vmax 1875 mV ist der Nennwert bei VCELL 3,75 V "
                   "(BMS-Abschaltung im Ladefehler), vmax_worstcase 1894 mV "
                   "derselbe Punkt mit 1 % Toleranz: 3,75 V x 474,7/(465,3+474,7) "
                   "- 6 mV unter dem Bereichsende (NC-04). Wir lassen das so: der "
                   "Punkt tritt nur auf, wenn das BMS im Ladefehler abschaltet, "
                   "und ein saettigender ADC liest dann 'sehr hoch', was zur "
                   "selben Entscheidung fuehrt. Im Normalbetrieb (VCELL <= 3,65 V) "
                   "sind es hoechstens 1844 mV. Der Gesamtfehler der "
                   "Rueckrechnung (NC-03, +-77 mV) ist damit NICHT behoben - dazu "
                   "die Einpunktkalibrierung je Geraet, siehe Begleitschreiben",
    },
    "PWR_K": {
        "vmin": 0.0, "vnom": 3.00, "vmax": 3.15, "vmax_worstcase": 3.175,
        "adc_atten_mv": 3300,
        "pegel_definiert_durch": "Leiter R20 10 k an VBUS_EXT, R21 15 k nach GND; "
                                 "R22/R23 ueber D20/D21 an den STAT-Pins",
        "baender": "kein USB 0 V | Fehler 1,05-1,54 V | laedt 2,09-2,43 V | voll bzw. "
                   "Ladepause 2,84-3,17 V (Monte-Carlo des Audits fuer die 10k-Leiter, "
                   "3000 Laeufe je Zustand); Schwellen in pwr_std.h 0,60 / 1,85 / "
                   "2,65 V, unveraendert",
        "hinweis": "ADC1_CH4 mit 12 dB, Messbereich 0-3300 mV (DS v1.5 Tab. 5-6). "
                   "Oberhalb von etwa 2,9 V geht der Wandler in die Saettigung; "
                   "das stoert nicht, weil das oberste Band ('voll bzw. Ladepause') "
                   "allein daran erkannt wird, dass der Wert ueber 2,65 V liegt. "
                   "Sperrstrom der Dioden (Audit-Beobachtung O2): im Leerlauf "
                   "liegen beide STAT-Pins ueber die Board-LEDs auf etwa 4,5 V, "
                   "D20/D21 sperren mit rund 1,3 V und heben den Knoten. Deshalb "
                   "ist die Leiter seit v1.4.2 zehnfach niederohmiger "
                   "(10k/15k/15k/3k3, Standarddimensionierung des Power-Standards). "
                   "Die Baender haengen nur an den Widerstandsverhaeltnissen und "
                   "bleiben von der Skalierung unberuehrt; die Flussspannung der "
                   "Dioden steigt mit dem zehnfachen Strom aber von rund 0,13 auf "
                   "0,28 V, wodurch die Fehlerbaender um bis zu 60 mV steigen "
                   "(kleinste Marge zur Schwelle 149 mV statt 182 mV, 0 von 15000 "
                   "Fehldekodierungen im Monte-Carlo des Audits). Die Quellimpedanz "
                   "faellt von 60 k auf 6 k, jedes uA Sperrstrom hebt den Knoten "
                   "also um 6 mV statt 60 mV. Der Strom "
                   "kommt aus VBUS, nie aus den Zellen; der Senkstrom der STAT-Pins "
                   "bleibt mit 0,28 mA weit unter den 5 mA, fuer die SLUSF65B 5.5 "
                   "die 0,4 V VOL angibt. D22 klemmt zusaetzlich auf VDD+Vf (K16). "
                   "Die ERC rechnet mit 2 uA Zuschlag (3,19 V); T-L1b misst den "
                   "Knoten nach 30 min Laden bei warmem Kern gegen 3,20 V",
    },
}

# Extra fields the audit asks for by name (PA-01, Abschnitt 5, SP-1).
EXTRA_FIELDS: dict[str, dict[str, object]] = {
    "M1": {
        "tx_power_dbm": 20,
        "tx_hinweis": "eingestellte Sendeleistung 802.15.4: +20 dBm, die "
                      "ESP-IDF-Voreinstellung, so dokumentiert in docs/THREAD.md. "
                      "Das Datenblatt (ESP32-C6 v1.5 Tab. 5-9) nennt dafuer "
                      "305 mA; in i_peak_ma rechnen wir wie die eigene ERC mit "
                      "350 mA, also darueber.",
    },
    "U3": {
        "abstand_lader_mm": 70.7,
        "abstand_quelle": "cad/openscad/aircheck_case.scad, Zusicherung E5 "
                          "(assert gap2d >= 30) und echo 'charger to SHT40 / "
                          "SGP40 / Sunrise: [70.676, 54.4785, 92.6307] mm'. "
                          "Modul C 7 verlangt >= 30 mm; der SHT40 sitzt hinter "
                          "der Kammerwand in der Gas-Bucht (NC-12).",
    },
    "U2": {"abstand_lader_mm": 54.5, "abstand_quelle": "wie U3, gleicher echo"},
    "U4": {"abstand_lader_mm": 92.6, "abstand_quelle": "wie U3, gleicher echo"},
}

TOLERANCE = {r: 1.0 for r in ("R1", "R2", "R20", "R21", "R22", "R23")}
TOLERANCE["TH1"] = 1.0      # Semitec 103AT-2, +-1 % bei 25 C

DATASHEET_FALLBACK = {
    "BH1": "Keystone 1049 Zeichnung (Mouser 534-1049)",
    "BH2": "Keystone 1049 Zeichnung (Mouser 534-1049)",
    "FU1": "Littelfuse PICO II 251 Serie, 2 A flink",
    "FU2": "Littelfuse PICO II 251 Serie, 2 A flink",
    "FU3": "Littelfuse PICO II 251 Serie, 2 A flink",
    "FU4": "Littelfuse PICO II 251 Serie, 2 A flink",
    "U5": "eremit Produktangabe (HY2112-CB Schutz-IC + 8205A: 3,75 V / 2,1 V, 2,5 A)",
    "TH1": "Semitec 103AT-2 (10 k, B = 3435 K)",
    "PS1": "Pololu 5719 Produktseite inkl. Kennlinie 'Maximum Continuous Output Current'",
    "J1": "JST PH Serie (PHR-2)",
    "D20": "Vishay BAT43",
    "D21": "Vishay BAT43",
    "D22": "Vishay BAT43",
    "SW1": "Pololu 2810 Produktseite und Schaltbild",
    "SW2": "Reichelt T 250A, Datenblatt des Tasters",
    "LED1": "Adafruit 159 (RGB 5 mm, gemeinsame Anode)",
    "C1": "KEMET C320C104K5R5TA",
    "C2": "KEMET C320C104K5R5TA",
}
RESISTOR_DATASHEET = "Yageo MFR-25 Serie (MFR-25FBF52), 1 %, 0,25 W"

CELLS = {  # ref -> (Klemmennetz, Beschriftung)
    "BT1": ("CELL1A+", "Zelle 1A (Halter BH1, innen)"),
    "BT2": ("CELL1B+", "Zelle 1B (Halter BH1, aussen)"),
    "BT3": ("CELL2A+", "Zelle 2A (Halter BH2, innen)"),
    "BT4": ("CELL2B+", "Zelle 2B (Halter BH2, aussen, traegt den NTC)"),
}

SKIP_PARTS = {"CELL"}          # ersetzt durch BT1..BT4


def _q(s: str) -> str:
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def _key(k: str) -> str:
    return k if k.replace("_", "").isalnum() else _q(k)


def _wrap(prefix: str, text: str, width: int = 96) -> list[str]:
    """One long TOML string, folded into readable comment-free chunks."""
    return [f"{prefix}{_q(text)}"]


def parts_toml(nodes_by_part: dict[str, set[str]]) -> list[str]:
    out: list[str] = []
    for p in d.PARTS:
        if p.ref in SKIP_PARTS or not p.pins or not p.required:
            continue
        ds = p.datasheet or DATASHEET_FALLBACK.get(p.ref) or (
            RESISTOR_DATASHEET if p.ref.startswith("R") else "")
        out += ["[[part]]",
                f"ref        = {_q(p.ref)}",
                f"value      = {_q(p.value)}",
                f"mpn        = {_q(p.mpn)}",
                f"datenblatt = {_q(ds)}",
                f"footprint  = {_q(p.footprint)}"]
        if p.ref in I_MAX:
            out.append(f"i_max_ma   = {I_MAX[p.ref][0]:g}")
            out.append(f"i_max_quelle = {_q(I_MAX[p.ref][1])}")
        if p.ref.startswith("FU"):
            out.append(f"i_max_ma   = {FUSE_I_MAX[0]:g}")
            out.append(f"i_max_quelle = {_q(FUSE_I_MAX[1])}")
        if p.ref in TOLERANCE:
            out.append(f"toleranz_pct = {TOLERANCE[p.ref]}")
        if p.i2c_addr is not None:
            out.append(f"i2c_addr   = 0x{p.i2c_addr:02x}")
        for k, v in EXTRA_FIELDS.get(p.ref, {}).items():
            out.append(f"{k} = {v if isinstance(v, (int, float)) else _q(v)}")
        out.append(f"begruendung = {_q(' '.join(p.why.split()))}")
        out.append("[part.pins]")
        roles = ROLES.get(p.ref, {})
        for pin in p.pins:
            out.append(f"{_key(pin):<8} = {_q(roles.get(pin, 'passiv'))}")
        if p.ref in SUPPLY:
            rail, vmin, vmax, src = SUPPLY[p.ref]
            out += ["[part.supply]", f"schiene = {_q(rail)}",
                    f"vmin    = {vmin}", f"vmax    = {vmax}",
                    f"quelle  = {_q(src)}"]
        if p.nc:
            out.append("[part.nc]")
            for pin in p.nc:
                reason = NC_REASONS.get(p.ref, {}).get(pin)
                assert reason, f"kein NC-Grund fuer {p.ref}.{pin}"
                out.append(f"{_key(pin):<8} = {_q(reason)}")
        missing = [pin for pin in p.pins
                   if pin not in nodes_by_part.get(p.ref, set()) and pin not in p.nc]
        assert not missing, f"{p.ref}: Pins ohne Netz und ohne NC-Grund: {missing}"
        out.append("")
    # die vier Zellen: mechanisch in den Haltern, elektrisch eigene Bauteile
    cell = d.PARTS_BY_REF["CELL"]
    for ref, (net, label) in CELLS.items():
        out += ["[[part]]",
                f"ref        = {_q(ref)}",
                f"value      = {_q(cell.value + ' - ' + label)}",
                f"mpn        = {_q(cell.mpn)}",
                f"datenblatt = {_q('Lithium Werks AER18650m2A2, Zellspezifikation 320749-001')}",
                f"footprint  = {_q('18650, flat top, im Keystone-Halter')}",
                f"i_max_ma   = {CELL_I_MAX[0]:g}",
                f"i_max_quelle = {_q(CELL_I_MAX[1])}",
                "[part.pins]",
                f'"+"      = "supply_out"',
                f'"-"      = "supply_out"',
                ""]
    return out


# ---------------------------------------------------------------------------
# Parts that design.py does not carry because they are not bought separately.
# ---------------------------------------------------------------------------

EXTRA_PARTS = [{
    "ref": "CX1",
    "value": "Eingangskapazitaet des SEN62 am VDD-Pin (modulintern), "
             "Auslegungsgrenze 33 uF",
    "mpn": "Bestandteil von SEN62-SIN-T",
    "datenblatt": "Sensirion SEN6x DS v0.92 - dort NICHT spezifiziert (siehe hinweis)",
    "footprint": "im SEN62-Modul",
    "pins": {"1": "passiv", "2": "passiv"},
    "hinweis": "Sensirion gibt fuer SEN6x und SEN5x weder eine Eingangskapazitaet "
               "noch einen Blockkondensator an: im Datenblatt v0.92 (60 Seiten) "
               "kommt das Wort 'capacitor' nicht vor, die Applikationsschaltung "
               "Fig. 5 zeigt nur die beiden I2C-Pull-ups, und die Design-In- bzw. "
               "Assembly-Guidelines haben kein Elektrikkapitel. Gemessen werden "
               "kann sie nicht, bevor das Modul da ist - nichts ist bestellt. "
               "Statt zu raten ist die Schaltung so ausgelegt, dass jeder Wert bis "
               "33 uF traegt: C3 (470 uF, -20 %) laesst +3V3 beim Schliessen von "
               "SW1 auf 3,03 V einbrechen, ueber dem VDD-Minimum 3,00 V und weit "
               "ueber dem Brown-out 2,92 V. T-P3 misst C(SEN62) vor dem "
               "Zusammenbau gegen genau diese Grenze; darueber kommt ein zweiter "
               "C3 dazu. Die Rechnung steht in design.py (sen62_switch_on_v) und "
               "laeuft in der ERC mit.",
    "nets": {"1": "+3V3_SEN", "2": "GND"},
}]


def nets_toml() -> list[str]:
    out: list[str] = []
    extra_nodes: dict[str, list[tuple[str, str]]] = {}
    for p in EXTRA_PARTS:
        for pin, net in p["nets"].items():
            extra_nodes.setdefault(net, []).append((p["ref"], pin))
    for ref, (net, _) in CELLS.items():
        extra_nodes.setdefault(net, []).append((ref, "+"))
        extra_nodes.setdefault("CELL_B-", []).append((ref, "-"))

    for name, nodes in d.NETS.items():
        nodes = list(nodes) + extra_nodes.get(name, [])
        rail = d.RAILS.get(name)
        out.append("[[net]]")
        out.append(f"name       = {_q(name)}")
        if name in ANALOG_NETS:
            a = ANALOG_NETS[name]
            out += ['class      = "analog"',
                    f"vmin       = {a['vmin']}",
                    f"vnom       = {a['vnom']}",
                    f"vmax       = {a['vmax']}"]
            if "vmax_worstcase" in a:
                out.append(f"vmax_worstcase = {a['vmax_worstcase']}")
            out += [f"adc_atten_mv = {a['adc_atten_mv']}",
                    f"pegel_definiert_durch = {_q(a['pegel_definiert_durch'])}",
                    f"hinweis    = {_q(a['hinweis'])}"]
        elif rail is not None or name in EXTRA_RAILS:
            vmin, vnom, vmax, src = (
                (rail.vmin, rail.vnom, rail.vmax, rail.source) if rail is not None
                else EXTRA_RAILS[name])
            out += ['class      = "power"',
                    f"vmin       = {vmin}", f"vnom       = {vnom}", f"vmax       = {vmax}",
                    f"quelle     = {_q(src)}"]
            if name != "GND":
                peak, why = RAIL_PEAK[name]
                out += [f"quelle_ref = {_q(RAIL_SOURCE_REF[name])}",
                        f"i_peak_ma  = {peak:.0f}",
                        f"i_peak_quelle = {_q(why)}"]
        else:
            out.append('class      = "signal"')
            lvl = SIGNAL_LEVEL.get(name)
            if lvl:
                out.append(f"pegel_definiert_durch = {_q(lvl)}")
        out.append("nodes      = [" + ", ".join(_q(f"{r}.{p}") for r, p in nodes) + "]")
        out.append("")
    return out


SIGNAL_LEVEL = {
    "NTC": "TH1 (NTC 10 k) nach GND; den Pegel stellt der BQ25185 selbst ein",
    "CE": "GPIO5 treibt high fuer die Ladepause; als Eingang geschaltet zieht der "
          "Pull-down auf dem Adafruit-Board !CE nach GND = laden erlaubt",
    "BTN": "interner Pull-up des ESP32-C6 an GPIO1, SW2 zieht nach GND",
    "CHG_N": "STAT2 des BQ25185, open drain gegen den LED-Zweig auf dem Board",
    "FLT_N": "STAT1 des BQ25185, open drain",
    "CHG_R": "R22 150 k von D20 zum PWR-K-Knoten",
    "FLT_R": "R23 33 k von D21 zum PWR-K-Knoten",
    "LED_R_K": "GPIO16 senkt ueber R8", "LED_G_K": "GPIO22 senkt ueber R9",
    "LED_B_K": "GPIO23 senkt ueber R10",
}

# NTC is an analog input of the charger, so the tool treats it as an ADC net.
ANALOG_NETS["NTC"] = {
    "vmin": 0.0, "vnom": 1.25, "vmax": 3.65, "adc_atten_mv": 5500,
    "pegel_definiert_durch": "TH1 (103AT-2) nach GND, Vorspannung aus dem BQ25185",
    "hinweis": "kein ESP-ADC: das ist der TH/TS-Eingang des BQ25185. Als Bereich "
               "steht hier die belegbare Grenze aus SLUSF65B 5.1 ('all other pins' "
               "5,5 V absolut) - dieselbe Zahl, die der Akku-Experte in SP-3/4/5 "
               "fuehrt (Audit-Beobachtung O1; vorher stand hier 3650 mV, das war "
               "konservativ, aber nicht belegt). Die Schwellen VCOLD/VHOT liegen "
               "weit darunter. TH1 hat 1 % Toleranz, sitzt aber als NTC und nicht "
               "als Teiler",
}


BUS_HINWEIS = {
    "CO2": "Die Warnung A11 (Teilnehmer an VSYS, Pull-ups an CO2_VDDIO) ist hier "
           "die Absicht: Senseair verlangt in TDE7318 'Low power integration', "
           "dass an den I/O-Pins nichts anliegt, solange EN low ist. Deshalb "
           "kommen VDDIO und beide Pull-ups aus GPIO18 und gehen mit dem Sensor "
           "zusammen aus; VBB an VSYS bleibt stehen, zieht aber nur 0,034 mA. Der "
           "Bus kann das Bauteil also gerade nicht speisen - genau andersherum "
           "als im Regelfall, gegen den A11 warnt.",
    "SEN": "Pull-ups an +3V3_SEN, also hinter SW1: sie gehen mit dem SEN62 aus.",
    "LP": "SGP40 und SHT40 haengen dauerhaft an +3V3, die Pull-ups sitzen auf den "
          "Breakout-Boards (SparkFun 4,7 k, I2C-Jumper geschlossen).",
}


def buses_toml() -> list[str]:
    out: list[str] = []
    for name, b in d.BUSES.items():
        out += ["[[bus]]", f"name           = {_q(name)}",
                "teilnehmer     = [" + ", ".join(_q(r) for r in b["devices"].values()) + "]",
                f"pullup_schiene = {_q(b['rail'])}",
                f"pullups        = {_q(b['pullups'])}"]
        if name in BUS_HINWEIS:
            out.append(f"hinweis        = {_q(BUS_HINWEIS[name])}")
        out.append("")
    return out


def extra_parts_toml() -> list[str]:
    out: list[str] = []
    for p in EXTRA_PARTS:
        out += ["[[part]]", f"ref        = {_q(p['ref'])}",
                f"value      = {_q(p['value'])}", f"mpn        = {_q(p['mpn'])}",
                f"datenblatt = {_q(p['datenblatt'])}",
                f"footprint  = {_q(p['footprint'])}",
                f"hinweis    = {_q(p['hinweis'])}", "[part.pins]"]
        for pin, role in p["pins"].items():
            out.append(f"{_key(pin):<8} = {_q(role)}")
        out.append("")
    return out


HEADER = """# SP-1 - AIR CHECK v1.4, Gesamtschaltung
#
# ERZEUGT von tools/audit/sp1_export.py aus electronics/schematic/design.py.
# Nicht von Hand aendern: die Quelle ist design.py, dort laeuft auch die eigene
# ERC (764 Pruefungen).  Was hier zusaetzlich steht - Pinrichtungen, Gruende fuer
# offene Pins, Versorgungsgrenzen, Spitzenstroeme, Toleranzen - steht in
# tools/audit/sp1_export.py und ist mit Quelle belegt.
#
# Nicht enthalten, weil sie keine elektrischen Knoten haben: die Kabel W1-W3
# (JST GH, Qwiic, Grove), die Mechanik X1-X7 und der optionale Messadapter X4.
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=DEFAULT_OUT)
    a = ap.parse_args()

    nodes_by_part: dict[str, set[str]] = {}
    for nodes in d.NETS.values():
        for ref, pin in nodes:
            nodes_by_part.setdefault(ref, set()).add(pin)

    lines = [HEADER, "[meta]",
             'projekt  = "AIR CHECK"',
             'revision = "v1.4"',
             'autor    = "AIR CHECK (Claude-Sitzung 3d-Printing-AIR-CHECK)"',
             f'datum    = "{d.__dict__.get("SP1_DATE", "2026-09-20")}"',
             'quelle   = "electronics/schematic/design.py (ERC 764 Pruefungen, '
             '0 Fehler), erzeugt mit tools/audit/sp1_export.py; Grenzwerte aus den '
             'in docs/BOM.md genannten Datenblaettern"',
             ""]
    lines += parts_toml(nodes_by_part)
    lines += extra_parts_toml()
    lines += nets_toml()
    lines += buses_toml()

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as fh:
        fh.write("\n".join(lines))
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
