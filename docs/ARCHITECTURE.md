# Architecture (v1.4)

## What the device is

One box, powered by any USB-C charger with four LiFePO4 cells inside as the
backup, that measures particulate matter, CO2, temperature,
humidity and VOC, and publishes them over Thread and Matter. It has no
display: numbers are shown in Apple Home and on a separate e-ink dashboard.
There is exactly one hardware design. To watch two places, build two identical
boxes and name them differently.

## System

```
                 ┌───────────────── AIR CHECK ──────────────────────┐
  ROOM AIR ──►   │ left wall              │ electronics              │
  (ports)        │  SEN62  PM1-10 (I2C)   │  FireBeetle 2 ESP32-C6   │
                 │                        │  regulator, ideal diode  │
                 │                        │  USB-C socket J2 (power) │
  ROOM AIR ──►   │ gas bay: Sunrise CO2,  │  button + RGB LED        │
  (vents)        │   SGP40 VOC, SHT40 T/RH│                          │
                 │ battery compartment: 1S4P LiFePO4, charger chamber│
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

No custom PCB: every part is a finished module, plus fourteen through-hole
resistors, two capacitors, two Schottky diodes, four fuses and an NTC
soldered inline (EDR-19, `NETLIST.md` "Inline parts").

| block | part | why |
|---|---|---|
| MCU | DFRobot FireBeetle 2 ESP32-C6 | native 802.15.4, USB-C for configuration and updates, TPS62A02 3.3 V buck, 1M/1M divider from its battery input; EUR 7.50 (EDR-1, EDR-15) |
| PM | Sensirion SEN62 | PM1/2.5/4/10 and number concentration only; power-gated by a Pololu #2810 |
| CO2 | Senseair Sunrise 006-0-0008 | NDIR, ±(30 ppm + 3 %), single-measurement mode, ABC state kept by the host (EDR-16) |
| T, RH | Sensirion SHT40 (Seeed Grove) | ±0.2 °C, ±1.8 %RH; feeds the SGP40's compensation (EDR-17) |
| VOC | Sensirion SGP40 (SparkFun SEN-18345) | VOC Index; the 10 s tripwire (EDR-5) |
| status | RGB LED in a panel holder + one panel button | the only local output (EDR-12) |
| power | the Power-Standard's module C: Adafruit #6050 USB-C socket, Adafruit #6091 charger (TI BQ25185, LFP 3.65 V, 1 A, NTC, 6 h timer, power path), 4 × Lithium Werks AER18650m2A2 LiFePO4 (1S4P) in two Keystone 1049 holders, a Littelfuse PICO II 2 A per cell, HY2112 BMS; Pololu S9V11E2A 3.90 V; Adafruit LM66200 ideal diode | charged in the device over USB-C, runs from the charger with the cells as backup (EDR-21); see docs/BATTERY_LIFE.md and the user guide docs/NUTZUNG.md |

Full wiring list, pin map and power tree: `electronics/schematic/NETLIST.md`,
generated and rule-checked by `electronics/schematic/design.py`.

## Power

```
4 × AER18650m2A2 (2 × Keystone 1049) ─PICO 2 A each─► BMS HY2112 ─► VCELL 3.0..3.65 V
    BMS P− = system GND; B− goes nowhere else
USB-C J2 ─► VBUS_EXT 5 V ─► #6091 DCIN (TI BQ25185: LFP 3.65 V, 1 A, NTC, 6 h timer)
VCELL ◄─► #6091 BATT          NTC 103AT-2 on a cell in the middle of the pack ─► #6091 TH
#6091 LOAD (3.0..3.65 V on the cells, 4.5 V on USB-C) ─► PS1 Pololu S9V11E2A ─► +VREG 3.90 V
+VREG ─► LM66200 VIN1 (VIN2, ON at GND) ─► VSYS ─► FireBeetle battery input
VSYS ─► Sunrise VBB, LED common anode
VSYS ─► FireBeetle TPS62A02 ─► +3V3 (always on): ESP32-C6, SGP40, SHT40
+3V3 ─[Pololu #2810, ON = GPIO2]─► +3V3_SEN ─► SEN62, its pull-ups
GPIO18 ─► Sunrise VDDIO and its pull-ups       (EN = GPIO14, 100k pull-down)
VCELL ─ 470k/470k ─► GPIO3 (ADC 6 dB)          VBAT_S, cell voltage / 2
VBUS_EXT ─ 100k ─┬─► GPIO4 (ADC 12 dB, 100 nF)   PWR-K node: EXT, CHG_N, FLT_N
                 ├─ 150k ─ GND
                 ├─ 150k ─►| BAT43 ─ #6091 S2 (CHG_N)
                 └─  33k ─►| BAT43 ─ #6091 S1 (FLT_N)
GPIO5 ─► #6091 !CE                             high = charge pause, input = charging
VSYS ─ 1M/1M (on the FireBeetle) ─► GPIO0      sanity only
```

* **Power path.** With USB-C in J2 the #6091 feeds LOAD from the charger
  (4.5 V) and charges the cells with what is left of its 1.1 A input limit;
  pull the charger and LOAD comes from the cells without a gap. PS1 turns
  either into a steady 3.90 V, so everything behind the FireBeetle's battery
  input sees the same voltage on the cells and on USB-C, and the SEN62's rail
  stays above its 3.15 V minimum down to the cells' 3.0 V.
* **The FireBeetle's own charger never reaches the cells.** With a computer
  on the FireBeetle's USB-C its CN3165 holds VSYS at 4.2 V; the LM66200 blocks
  any current back towards PS1. The FireBeetle's USB-C is for configuration
  and updates; only J2 charges the cells.
* **Protection in hardware**: charge voltage 3.65 V set by resistor, a 6 h
  safety timer, charging only between 0 and 60 °C at the cell (NTC), the
  HY2112 at 3.75 V / 2.1 V and over-current, a fuse per cell, and the
  BQ25185's undervoltage cut-off (BUVLO) at 3.0 V; it restarts with USB-C or
  at 3.15 V. All limits are in hardware; CE only ever pauses charging.
* **Charge pause.** After "full" on USB-C the firmware holds CE high; it
  releases it when USB-C was unplugged, below 3.30 V, or after 30 days. On
  permanent USB-C the cells are topped up about once a month. The #6091's
  pull-down keeps CE low through a reset: charging allowed, the fail-safe
  direction.
* **Safety timer.** 1S4P (6.8–7.2 Ah) takes 8–9 h from flat at the charge
  current left beside the device's own draw; the BQ25185 stops after 6 h. One
  150 ms CE pulse per USB session restarts it; a fault seen after at least
  5.5 h of charging is taken for the timer, because PWR-K cannot tell a
  latched fault from a recoverable one.
* **Charger heat.** At 1 A the linear charger turns up to 1.7 W into heat, in
  its own vented chamber at the far end of the battery compartment, 54–93 mm
  from the SHT40, SGP40 and Sunrise. Temperature and humidity can read a
  little high while charging; nothing is compensated, Matter's
  `BatChargeState` marks the window.
* The SEN62 idles at 3.3 mA, so its 3.3 V is switched off completely between
  windows (60 s an hour in ECO). The #2810 has no soft start; the switch-on
  step on the FireBeetle's buck is a bench item (TESTING T-P3).
* The Sunrise is off between measurements: EN low, and GPIO18 (VDDIO and both
  pull-ups) low, so nothing reaches its I/O while it is disabled.
* SGP40 and SHT40 are powered permanently. Neither breakout has a regulator;
  the SparkFun power LED is cut at its jumper.
* External power is USB-C in J2 (the PWR-K node above 0.6 V) or an
  enumerated USB host on the FireBeetle (USB Serial/JTAG). It means
  CONTINUOUS mode and no low/critical battery state. On the cells, low
  (3.20 V, about 10 %) and critical (3.10 V, about 6 %, measuring stops) come
  from the cell voltage with hysteresis, not from a percentage: on the flat
  LFP plateau a percentage is a guess. A cell voltage under 1.0 V on USB-C
  means no cells.

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
      ac_power       the power module: PWR-K decoding, safety-timer detection,
                     the CE decision (charge pause, timer restart), on pwr_std
    pwr_std/         the Power-Standard's C99 module, copied unchanged: LFP levels,
                     coarse percentage, charge state, charge pause, timer retry
    ac_hal/          ESP-IDF drivers: SEN62, Sunrise, SGP40, SHT40, the VBAT_S,
                     PWR-K and VSYS ADC, CE, USB detection, LED, button, NVS,
                     the I2C buses
    sensirion_gas_index/   vendored VOC Index algorithm, BSD-3
  main/
    app_main.cpp     two tasks: measure, and button/LED
    ac_matter.cpp    the Matter data model
    ac_console.cpp   service console on the FireBeetle's USB (only on external power)
  test/host/         537 checks plus the pwr_std test, run on a workstation
```

`ac_core` decides *what happens and when*; `ac_hal` only does what it is told.
That split is why the scheduling, event detection, LED logic, the power
module's decisions and the 24 h statistics are testable without hardware.

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
                  ├─► CHARGING          (external power; whether the cells charge is BatChargeState)
                  ├─► LOW_BATTERY       (< 3.20 V, on the cells only)
                  ├─► CRITICAL_BATTERY  (< 3.10 V, on the cells only; measurement stops)
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
| 0 | Root Node | Basic Information (NodeLabel = device name), Power Source "LiFePO4 1S4P" (Battery, Rechargeable, Replaceable; 4 × 18650 LiFePO4; `Status` 1 on the cells, 2 behind USB-C, 3 without cells; `BatChargeState`) |
| 1 | Air Quality Sensor `0x002C` | Air Quality; PM2.5, PM10, CO2, TVOC with 24 h peak and average; PM1 |
| 2 | Temperature Sensor `0x0302` | Temperature Measurement |
| 3 | Humidity Sensor `0x0307` | Relative Humidity Measurement |
| next (`usb-c=N` in the boot log) | Power Source `0x0011` | Power Source "USB-C" (Wired, DC; `Status` 1 while powered from USB-C, 3 otherwise; `WiredPresent`) |

All standard device types and clusters. `docs/APPLE_HOME.md` covers what Apple
shows; `docs/DASHBOARD_INTERFACE.md` covers what a dashboard reads and how.

## Mechanical

136 × 174 × 34 mm, PETG only, a front shell with a lid held by four M2.5
screws into heat-set inserts. Three zones:

* **battery compartment** along the bottom: the two cell holders, screwed to
  bosses with their pins hanging free on 5 mm standoffs, and a separate
  charger chamber (bottom left seen from the front) with the #6091 on four
  posts at least 5 mm from every wall and the BMS strip, vented through the
  bottom wall and high in the side wall. Walled off from everything else by a
  4 mm partition up to the lid, with pressure-relief slots in its bottom wall.
  Its own door (two screws, hex key 2 mm) carries cell-retaining ribs 1 mm
  above the cells, a finger that holds the NTC on its cell, and the engraved
  battery label (EDR-21);
* **gas bay** on the right-hand side (seen from the front), above the
  compartment: Sunrise, SGP40 and SHT40, walled off, vented through the
  right-hand wall and the front face, at least 30 mm from the charger;
  nothing inside is glued, taped or foamed (EDR-17);
* **electronics**: the SEN62 in a cradle with its port face gasketed against
  the left wall, the FireBeetle with its USB-C in an opening in the right-hand
  wall, the USB-C socket J2 on two posts at the top wall with its own opening
  there, regulator, ideal diode and switch in pockets or on posts; LED holder
  and button through the front.
