# 3D Printing AIR CHECK

**An open-source Thread/Matter air-quality sensor for 3D-printing emissions.**

A small battery-powered box that sits next to your printer and measures what
the print does to the air. It reports to Apple Home and to your own e-ink
dashboard: locally, with no cloud, no account and no hub beyond the HomePod
mini you already have.

<p align="center">
  <img src="cad/drawings/front_iso.png" alt="The finished case, seen from the front" width="48%">
  <img src="cad/drawings/open_view.png" alt="Inside: SEN63C, VOC bay, carrier with FireBeetle" width="48%">
</p>

---

## What it measures

| | |
|---|---|
| **Particles** | PM1.0, PM2.5, PM4, PM10 and number concentration: Sensirion SEN63C |
| **CO₂** | true CO₂ measurement, ±(100 ppm + 10 %): SEN63C |
| **Climate** | temperature and relative humidity: SEN63C |
| **VOC** | Sensirion VOC Index, 1–500: SGP40 |
| **Itself** | battery level, charging state, Thread and Matter status |

It classifies the air as **GOOD / ELEVATED / HIGH / VERY HIGH** and keeps a
reason for it ("PM2.5 + VOC"). It never says the air is unsafe, because it
cannot know that. See [docs/RISKS.md](docs/RISKS.md).

## How it works

```
 AIR CHECK "3D Printer" ──┐                        ┌──► Apple Home
                          ├── Thread ──► HomePod ──┤
 AIR CHECK "Room" ────────┘               mini     └──► your e-ink dashboard
                                   (border router)       (Matter multi-admin)
```

The sensor has **no display of its own**. It has one button and an RGB LED.
The numbers live in Apple Home and on a separate dashboard that reads the
sensors directly over Matter. That route is standard, local, and needs no
extra hub; [docs/DASHBOARD_INTERFACE.md](docs/DASHBOARD_INTERFACE.md) is the
complete contract for building one.

The power budget is built around one idea. The particle/CO₂ module costs
about a hundred times more to run than everything else, so it runs **40 s
once an hour**.
**VOC is watched every ten seconds** for almost nothing, and when the VOC index
climbs the sensor escalates itself to a particle reading every two minutes.
Printing raises VOC long before an hourly sample would notice.

```
ECO  --VOC rises-->  ACTIVE  --air clears-->  POST-PRINT  -->  ECO
```

## Battery life

4000 mAh cell, with a **25 % engineering margin**:

| mode | particles every | runtime |
|---|---|---|
| **ECO** (default) | 1 h | **3.2 months** (2.7 at the sensor's worst-case current) |
| ECO_LONG | 4 h | 7.0 months |
| NORMAL | 15 min | 3 weeks |
| ACTIVE | 2 min | 3 days (automatic, time-limited) |

These figures include a dashboard reading the sensor every 15 minutes. Every
input is traced to a datasheet or a measurement in
[docs/BATTERY_LIFE.md](docs/BATTERY_LIFE.md), which is generated from a model
you can re-run. (v1.1 claimed 3.0 months here and was wrong - two sensor
breakouts carried an LDO and an LED the model did not count. EDR-14.)

## Hardware

| | |
|---|---|
| MCU | DFRobot FireBeetle 2 ESP32-C6 (charger on board, 36 µA deep sleep) |
| Sensors | SEN63C and SGP40, each on its own switched rail and its own I²C bus |
| Status | one button, one diffused RGB LED behind a 0.6 mm skin of the front face |
| Battery | 1S LiPo 4000 mAh with protection, user replaceable, 8 h to charge |
| Case | **112 × 102 × 32 mm**, PETG front shell + lid, no supports |
| Cost | **about EUR 95** in parts (v1.1: EUR 173), EUR 110 with the carrier assembled by JLCPCB |

Full parts list with manufacturer part numbers: [docs/BOM.md](docs/BOM.md).

## The LED and the button

| press | |
|---|---|
| short | the LED shows the air quality for 3 s: 🟢 good, 🟡 elevated, 🔴 high, 🟣 very high |
| 3–8 s | pairing mode, the LED blinks blue for 5 minutes |
| 8–12 s | fresh-air CO₂ calibration - **outdoors only**; cyan for 3 min, then green |
| 12–20 s | factory reset, the LED blinks red fast |
| over 20 s | ignored, so a jammed button cannot wipe the device |

While you hold the button the LED shows what letting go would do: blue,
cyan, red. Unprompted, the LED stays dark. The exceptions are a short red blip every
10 s at critical battery or on a hardware fault. "Identify" from the Home app
or the dashboard blinks it white.

## Two devices

There is one hardware design. To watch two places you build two identical
units and name them "3D Printer" and "Room". There is no pairing between them
and nothing in the firmware that knows the other exists. Apple Home and the
dashboard compare them; [docs/SHORTCUTS.md](docs/SHORTCUTS.md) has recipes.

## Build it

```
PARTS -> PRINT -> ASSEMBLE -> FLASH -> CHARGE -> PAIR -> SHARE WITH THE DASHBOARD
```

1. **Parts:** [docs/BOM.md](docs/BOM.md)
2. **Print:** five parts, no supports, about 8 h and 160 g of PETG:
   [manufacturing/print-settings.md](manufacturing/print-settings.md)
3. **Assemble:** [docs/ASSEMBLY.md](docs/ASSEMBLY.md)
4. **Flash:**
   ```bash
   cd firmware
   . $HOME/esp/esp-idf/export.sh && . $HOME/esp/esp-matter/export.sh
   idf.py set-target esp32c6
   idf.py -p /dev/tty.usbmodem* flash monitor
   ```
5. **Charge:** about 8 h from flat at the FireBeetle's 540 mA
6. **Pair** with Apple Home: [docs/APPLE_HOME.md](docs/APPLE_HOME.md)
7. **Share** with the dashboard: [docs/DASHBOARD_INTERFACE.md](docs/DASHBOARD_INTERFACE.md)

## Status

**Nothing in this repository has been verified on assembled hardware.** No
AIR CHECK has been built. What *has* been done:

| | |
|---|---|
| measurement core | 509 host checks, run on every build, validated against injected bugs |
| electrical design | 460 automated rule checks over the netlist, validated against injected faults |
| enclosure | OpenSCAD asserts, every part intersected with every other, manifold check, fits a 250 × 210 bed |
| firmware | full ESP-IDF + esp-matter build for esp32c6: 1.68 MB, 15 % OTA headroom, 46.5 % of RAM |
| Matter attribute IDs | checked against the Matter SDK's generated headers |
| **sensor drivers** | **untested**: every register and timing read from a datasheet |
| **Thread, Matter, Apple Home, dashboard** | **untested**: no controller has seen this device |

[docs/TESTING.md](docs/TESTING.md) is the honest breakdown and the bench
protocol. If you build one, a report is the most useful thing you can
contribute.

## Documentation

| | |
|---|---|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | how the whole thing fits together |
| [DASHBOARD_INTERFACE.md](docs/DASHBOARD_INTERFACE.md) | **everything a dashboard needs**: pairing, attribute IDs, timing, power |
| [ENGINEERING_DECISIONS.md](docs/ENGINEERING_DECISIONS.md) | fifteen decisions and what they cost |
| [BATTERY_LIFE.md](docs/BATTERY_LIFE.md) | the energy model, with sources |
| [BOM.md](docs/BOM.md) | parts, prices, where to buy |
| [ASSEMBLY.md](docs/ASSEMBLY.md) | step by step |
| [APPLE_HOME.md](docs/APPLE_HOME.md) | what works, what does not |
| [MATTER.md](docs/MATTER.md) | endpoints, clusters, ICD |
| [THREAD.md](docs/THREAD.md) | sleepy end device, border routers |
| [CALIBRATION.md](docs/CALIBRATION.md) | baseline reset vs sensor calibration |
| [CONFIGURATION.md](docs/CONFIGURATION.md) | every setting and what changing it costs |
| [SHORTCUTS.md](docs/SHORTCUTS.md) | two-device automations |
| [TESTING.md](docs/TESTING.md) | what is verified and what is not |
| [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | when it does not work |
| [RISKS.md](docs/RISKS.md) | **read this before trusting a number** |

## What this is not

Not a medical device. Not a safety detector. Not a certified instrument. The
SEN63C cannot see the ultrafine particles that dominate 3D-printing emissions,
and the VOC Index cannot name a chemical. This device is good at telling you
that the air **changed**, by how much, and how long it took to recover. That is
useful, and it is not the same thing as a health assessment.

## Licence

Apache 2.0. Sensirion's Gas Index Algorithm is vendored under BSD-3-Clause;
see the licence file next to it.
