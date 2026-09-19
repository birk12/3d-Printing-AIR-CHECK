# Architecture (v1.3.1)

## What the device is

One box, powered by six AA cells or any USB-C charger, that measures particulate matter, CO2, temperature,
humidity and VOC, and publishes them over Thread and Matter. It has no
display: numbers are shown in Apple Home and on a separate e-ink dashboard.
There is exactly one hardware design. To watch two places, build two identical
boxes and name them differently.

## System

```
                 ┌───────────────── AIR CHECK ──────────────────────┐
  ROOM AIR ──►   │ left wall              │ electronics              │
  (ports)        │  SEN62  PM1-10 (I2C)   │  FireBeetle 2 ESP32-C6   │
                 │                        │  regulators, ideal diode │
                 │                        │  USB-C power socket J2   │
  ROOM AIR ──►   │ gas bay: Sunrise CO2,  │  button + RGB LED        │
  (vents)        │   SGP40 VOC, SHT40 T/RH│                          │
                 │ battery compartment: 6 × AA, screwed door         │
                 └───────────────────────────┬────────────────────────┘
                                             │ 802.15.4, sleepy end device
                                             ▼
                             HomePod mini (Thread border router)
                                   │                     │
                         Matter fabric 1          Matter fabric 2
                                   ▼                     ▼
                              Apple Home          e-ink dashboard
                          (iPhone, automations)   (multi-admin, local)
```

No cloud, no account, no Home Assistant, no MQTT broker, no bridge, no web
server on the device. If the Internet is down, everything keeps working. If
the Thread network is down, the sensor keeps measuring and keeps its own
history. It just cannot report until the network is back.

## Hardware

No custom PCB: every part is a finished module, plus eleven through-hole
resistors and one capacitor soldered inline (EDR-19).

| block | part | why |
|---|---|---|
| MCU | DFRobot FireBeetle 2 ESP32-C6 | native 802.15.4, USB-C for configuration and updates, TPS62A02 3.3 V buck, 1M/1M divider from its battery input; EUR 7.50 (EDR-1, EDR-15) |
| PM | Sensirion SEN62 | PM1/2.5/4/10 and number concentration only; power-gated by a Pololu #2810 |
| CO2 | Senseair Sunrise 006-0-0008 | NDIR, ±(30 ppm + 3 %), single-measurement mode, ABC state kept by the host (EDR-16) |
| T, RH | Sensirion SHT40 (Seeed Grove) | ±0.2 °C, ±1.8 %RH; feeds the SGP40's compensation (EDR-17) |
| VOC | Sensirion SGP40 (SparkFun SEN-18345) | VOC Index; the 10 s tripwire (EDR-5) |
| status | RGB LED in a panel holder + one panel button | the only local output (EDR-12) |
| power | 6 × AA, PTC fuse, Pololu S9V11E2A 3.90 V with an undervoltage lockout (R11); Adafruit #6050 USB-C power socket, second S9V11E2A 4.20 V; Adafruit LM66200 ideal diode | nothing is charged inside the device (EDR-18); runs from any USB-C charger with the cells as backup (EDR-20); see docs/BATTERY_LIFE.md |

Full wiring list, pin map and power tree: `electronics/schematic/NETLIST.md`,
generated and rule-checked by `electronics/schematic/design.py`.

## Power

```
6 × AA ─F1 PTC─► VPACK_F 5.4..10.8 V ─► PS1 Pololu S9V11E2A ─► +VREG 3.90 V
                    ├─ 1M/220k ─► GPIO3 (ADC)   pack voltage
                    └─ 100k (in PS1) ─ EN ─ R11 13k ─ GND   UVLO: off < 6.1 V, on > 7.0 V
USB-C J2 ─► VBUS_EXT 5 V ─► PS2 Pololu S9V11E2A ─► +VEXT 4.20 V
+VREG ─► LM66200 VIN1 ─┐
+VEXT ─► LM66200 VIN2 ─┴─ the higher one ─► VSYS ─► FireBeetle battery input
VSYS ─► Sunrise VBB, LED common anode
VSYS ─► FireBeetle TPS62A02 ─► +3V3 (always on): ESP32-C6, SGP40, SHT40
+3V3 ─[Pololu #2810, ON = GPIO2]─► +3V3_SEN ─► SEN62, its pull-ups
GPIO18 ─► Sunrise VDDIO and its pull-ups       (EN = GPIO14, 100k pull-down)
VSYS ─ 1M/1M (on the FireBeetle) ─► GPIO0      which source: cells or external
```

* **External power always wins.** PS2 sits 0.3 V above PS1, so with a
  charger in J2 the LM66200 feeds VSYS from +VEXT, and the cells are the
  backup: when the charger goes, VIN1 takes over without a gap. VSYS is
  3.9 V on the cells and 4.2 V on external power.
* **Nothing charges the cells.** With USB plugged in, the FireBeetle's
  CN3165 holds VSYS at 4.2 V; with a charger in J2, PS2 does. Either way the
  LM66200 blocks any current back towards PS1, so the cells never see it.
  USB 5 V never touches the pack. The FireBeetle's USB-C is for
  configuration and updates; J2 is power only.
* **Undervoltage lockout.** R11 against PS1's internal EN pull-up switches
  the cells' branch off at ~1.0 V per cell and restarts it only with fresh
  cells, so no cell is driven into reversal.
* The SEN62 idles at 3.3 mA, so its 3.3 V is switched off completely between
  windows (60 s an hour in ECO). The #2810 has no soft start; the switch-on
  step on the FireBeetle's buck is a bench item (TESTING T-P3).
* The Sunrise is off between measurements: EN low, and GPIO18 (VDDIO and both
  pull-ups) low, so nothing reaches its I/O while it is disabled.
* SGP40 and SHT40 are powered permanently. Neither breakout has a regulator;
  the SparkFun power LED is cut at its jumper.
* The power source comes from VSYS on GPIO0 (the FireBeetle's 1M/1M
  divider): at or above 4.08 V (`AC_EXT_POWER_V`), or with an enumerated USB
  host (USB Serial/JTAG), the device is on external power; a pack under
  3.0 V then means an empty holder. External power means CONTINUOUS mode and
  no low/critical battery state; only the regulator's quiescent current and
  the resistors across the pack are booked against the cells.

## I2C buses (100 kHz)

| bus | pins | pull-ups | device | powered |
|---|---|---|---|---|
| LP | LP_I2C, GPIO6 SDA / GPIO7 SCL (fixed pads) | on the boards | SGP40 0x59, SHT40 0x44 | always |
| SEN | HP I2C, GPIO19 SDA / GPIO20 SCL | 4.7k to +3V3_SEN | SEN62 0x6B | one window an hour (ECO) |
| CO2 | HP I2C, GPIO17 SDA / GPIO21 SCL | 10k to GPIO18 | Sunrise 0x68 | one measurement every 5 min (ECO) |

The SEN62 and the Sunrise share the ESP32-C6's single HP I2C controller. The
firmware creates it on one pin pair at a time, deletes it afterwards and
leaves both pairs as inputs, so nothing drives a sensor that is switched off.
The Sunrise bus is on GPIO17/21 rather than GPIO16, because GPIO16 is U0TXD
and the ROM boot log toggles it (EDR-19); GPIO16 drives the red LED instead.

## Firmware

```
firmware/
  components/
    ac_core/         pure C99, no ESP-IDF headers, runs on a workstation
      ac_config      configuration, validation, CRC, NVS serialisation
      ac_filter      time-aware EMA, least-squares rate, spike statistics
      ac_baseline    persistent room baseline, frozen during events
      ac_airquality  threshold + trend classification with a stated reason
      ac_event       IDLE -> POSSIBLE_PRINT -> ACTIVE -> POST_PRINT -> NORMALIZED
      ac_history     5 min and 1 h aggregation rings, min/mean/max, 24 h windows
      ac_engine      the scheduler: what to sample, when to sleep
      ac_status      what the RGB LED shows, as a pure function
      ac_battery     AA pack percentage: voltage curve per chemistry and energy counter;
                     power source (cells / external / holder empty) from VSYS
    ac_hal/          ESP-IDF drivers: SEN62, Sunrise, SGP40, SHT40, pack and
                     VSYS ADC, USB detection, LED, button, NVS, the I2C buses
    sensirion_gas_index/   vendored VOC Index algorithm, BSD-3
  main/
    app_main.cpp     two tasks: measure, and button/LED
    ac_matter.cpp    the Matter data model
    ac_console.cpp   service console on the FireBeetle's USB (only on external power)
  test/host/         546 checks that run on a workstation
```

`ac_core` decides *what happens and when*; `ac_hal` only does what it is told.
That split is why the scheduling, event detection, LED logic, battery
percentage and the 24 h statistics are testable without hardware.

## Measurement flow

```
   SEN62 window (PM)                   ┐
   Sunrise measurement (CO2)           ├─► ac_engine_submit ─► EMA ─► rate ─► statistics
   SHT40 (T, RH), then SGP40 (VOC)     ┘
        │
   ac_engine_tick
        ├─ classify         (ac_airquality)
        ├─ detect events    (ac_event)    ─► freezes the baseline
        ├─ update baseline  (ac_baseline) ─► NVS every 30 min
        ├─ aggregate        (ac_history)  ─► 24 h fine, 7 d coarse, 24 h peak/mean
        ├─ pick a mode      ECO / NORMAL / ACTIVE / POST_PRINT / CONTINUOUS
        └─ return a plan    what to sample next, how long to sleep
        │
   publish to Matter (only on a real change)
```

| profile | PM every | window | CO2 every | VOC + T/RH every |
|---|---|---|---|---|
| ECO (default) | 60 min | 60 s | 5 min | 10 s |
| NORMAL | 15 min | 60 s | 5 min | 10 s |
| ACTIVE | 2 min | 60 s | 2 min | 10 s |
| POST_PRINT | 5 min | 60 s | 5 min | 10 s |
| CONTINUOUS (external power) | continuous | 60 s | 1 min | 1 s |

A SEN62 window: switch on, start measurement, read once a second, discard the
first 30 s (`AC_PM_SETTLE_S`) and average the rest; then stop and switch off.
60 s (`AC_PM_MIN_WINDOW_S`) is the floor. On external power (CONTINUOUS) the
module stays on between windows.

A Sunrise measurement: VDDIO and EN high, write back the saved ABC and filter
state plus the pressure computed from `altitude_m`, start one 32-sample
measurement, read the result and the new state, switch off. The state is
saved to NVS (blob `sunrise`) with the other persistent data every 30 min.
The VOC channel keeps its 10 s grid while a SEN62 window or a Sunrise
measurement is running.

## Device states

```
BOOT → WARMUP → NORMAL ⇄ ACTIVE → POST_PRINT → NORMAL
                  ├─► CHARGING          (external power; historical name, nothing is charged)
                  ├─► LOW_BATTERY       (≤ 20 %, on the cells only)
                  ├─► CRITICAL_BATTERY  (≤ 5 %, on the cells only; measurement stops)
                  ├─► ERROR             (SEN62, Sunrise and SGP40 all failed)
                  ├─► COMMISSIONING     (3–8 s button press)
                  ├─► CO2 calibration   (8–12 s press, outdoors: 3 measurements + target calibration)
                  └─► FACTORY_RESET     (12–20 s button press)
```

Nothing fails silently. Each sensor error is counted, a channel that keeps
failing is marked down and excluded from the verdict (never reported as zero),
and an all-channels failure blinks the LED red on its own.

## Matter model

| endpoint | device type | clusters |
|---|---|---|
| 0 | Root Node | Basic Information (NodeLabel = device name), Power Source (battery, replaceable, 6 × AA; `Status` 1 on the cells, 2 on external power with the cells as backup, 3 with the holder empty) |
| 1 | Air Quality Sensor `0x002C` | Air Quality; PM2.5, PM10, CO2, TVOC with 24 h peak and average; PM1 |
| 2 | Temperature Sensor `0x0302` | Temperature Measurement |
| 3 | Humidity Sensor `0x0307` | Relative Humidity Measurement |

All standard device types and clusters. `docs/APPLE_HOME.md` covers what Apple
shows; `docs/DASHBOARD_INTERFACE.md` covers what a dashboard reads and how.

## Mechanical

130 × 146 × 34 mm, PETG, a front shell with a lid held by four M2.5 screws
into heat-set inserts. Three zones:

* **battery compartment**: the 6 × AA holder behind its own door (two M2.5
  screws), walled off from everything else by a 4 mm partition up to the lid,
  with pressure-relief slots in its bottom wall (EDR-18);
* **gas bay** in the lower right corner (seen from the front): Sunrise, SGP40
  and SHT40, walled off, vented through the right-hand wall and the front
  face; nothing inside is glued, taped or foamed (EDR-17);
* **electronics**: the SEN62 in a cradle with its port face gasketed against
  the left wall, the FireBeetle with its USB-C in an opening in the right-hand
  wall, the USB-C power socket J2 on two posts at the top wall with its own
  opening there, regulators, ideal diode and switch in pockets or on posts;
  LED holder and button through the front.
