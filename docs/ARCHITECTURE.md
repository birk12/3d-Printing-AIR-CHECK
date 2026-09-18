# Architecture (v1.1)

## What the device is

One battery-powered box that measures particulate matter, VOC, CO2,
temperature and humidity, and publishes them over Thread and Matter. It has no
display: numbers are shown in Apple Home and on a separate e-ink dashboard.
There is exactly one hardware design. To watch two places, build two identical
boxes and name them differently.

## System

```
                 ┌───────────────── AIR CHECK ──────────────────┐
  ROOM AIR ──►   │ sensor bay            │ electronics            │
                 │  SPS30  (PM, UART)    │  ESP32-C6 Feather      │
                 │  SGP40  (VOC) ┐ LP-   │  ACC-1 carrier         │
                 │  SCD41  (CO2) ┘ I2C   │  button + RGB LED      │
                 │               back layer: 4000 mAh LiPo        │
                 └───────────────────────────┬────────────────────┘
                                             │ 802.15.4, sleepy end device
                                             ▼
                             HomePod mini (Thread border router)
                                   │                     │
                         Matter fabric 1          Matter fabric 2
                                   ▼                     ▼
                              Apple Home          e-ink dashboard
                          (iPhone, automations)   (multi-admin, local)
```

No cloud, no account, no Home Assistant, no MQTT broker, no bridge. If the
Internet is down, everything keeps working. If the Thread network is down, the
sensor keeps measuring and keeps its own history. It just cannot report until
the network is back.

## Hardware

| block | part | why |
|---|---|---|
| MCU | ESP32-C6-MINI-1 on an Adafruit ESP32-C6 Feather | native 802.15.4, 512 kB SRAM, charger + fuel gauge + USB-C on board (EDR-1) |
| PM | Sensirion SPS30 over UART | factory calibrated; UART so it can be power-gated cleanly (EDR-2) |
| VOC | Sensirion SGP40 | VOC Index with on-chip humidity compensation |
| CO2 | Sensirion SCD41 | NDIR; single-shot mode makes the budget work (EDR-4) |
| status | Adafruit 159 RGB LED + one tactile switch | the only local output since v1.1 (EDR-12) |
| battery | 1S LiPo 4000 mAh with protection | see docs/BATTERY_LIFE.md |

Full net list, pin map and power tree: `electronics/schematic/NETLIST.md`,
generated and rule-checked by `electronics/schematic/design.py`.

## Power

```
USB-C → MCP73831 (196 mA) → VBAT (1S LiPo) ──► MAX17048, LED anode
                          └► RT9080 LDO → +3V3 (always on): MCU, button pull-up

VBAT ─[SW1 TPS22918, GPIO2]─► TPS61023 boost → +5V ─► SPS30
+3V3 ─[SW2 TPS22918, GPIO3]─► +3V3_SENS ─► SGP40, SCD41, sensor-bus pull-ups
+3V3 ─[Feather LDO, GPIO20]─► VSENSOR ─► gauge-bus pull-ups, Feather WS2812B
```

* SW1 sits on the boost's **input**: a disabled boost still passes its input
  to its output, which would leave the SPS30 at an undefined 3.4 V.
* The sensor-bus pull-ups are on the **switched** rail: switching it off
  leaves no path into the unpowered sensors.
* GPIO20 is only high for the few milliseconds of a battery read, because the
  Feather puts its I2C pull-ups and a WS2812B on the same LDO (EDR-11).

## Two I2C buses

| bus | pins | pull-ups | devices | powered |
|---|---|---|---|---|
| sensor | LP_I2C, GPIO6 SDA / GPIO7 SCL (fixed pads) | 4.7k on the carrier, to +3V3_SENS | SGP40 0x59, SCD41 0x62 | always, in normal operation |
| gauge | HP I2C, GPIO19 SDA / GPIO18 SCL | 10k on the Feather, to VSENSOR | MAX17048 0x36 (+ AHT20 0x38 if fitted) | only during a read, every 5 min |

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
    ac_hal/          ESP-IDF drivers: SPS30 (SHDLC/UART), SGP40, SCD41,
                     MAX17048, LED, button, NVS, two I2C buses
    sensirion_gas_index/   vendored VOC Index algorithm, BSD-3
  main/
    app_main.cpp     two tasks: measure, and button/LED
    ac_matter.cpp    the Matter data model
  test/host/         466 checks that run on a workstation
```

`ac_core` decides *what happens and when*; `ac_hal` only does what it is told.
That split is why the scheduling, event detection, LED logic and the 24 h
statistics are testable without hardware.

## Measurement flow

```
   RAW sample ─► ac_engine_submit ─► EMA ─► rate of change ─► statistics
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

## Device states

```
BOOT → WARMUP → NORMAL ⇄ ACTIVE → POST_PRINT → NORMAL
                  ├─► CHARGING          (USB present)
                  ├─► LOW_BATTERY       (≤ 20 %)
                  ├─► CRITICAL_BATTERY  (≤ 5 %, measurement stops)
                  ├─► ERROR             (all three sensors failed)
                  ├─► COMMISSIONING     (3–8 s button press)
                  └─► FACTORY_RESET     (12–20 s button press)
```

Nothing fails silently. Each sensor error is counted, a channel that keeps
failing is marked down and excluded from the verdict (never reported as zero),
and an all-channels failure blinks the LED red on its own.

## Matter model

| endpoint | device type | clusters |
|---|---|---|
| 0 | Root Node | Basic Information (NodeLabel = device name), Power Source (battery, rechargeable) |
| 1 | Air Quality Sensor `0x002C` | Air Quality; PM2.5, PM10, CO2, TVOC with 24 h peak and average; PM1 |
| 2 | Temperature Sensor `0x0302` | Temperature Measurement |
| 3 | Humidity Sensor `0x0307` | Relative Humidity Measurement |

All standard device types and clusters. `docs/APPLE_HOME.md` covers what Apple
shows; `docs/DASHBOARD_INTERFACE.md` covers what a dashboard reads and how.

## Mechanical

98 × 102 × 32 mm, two PETG shells, four M2.5 screws into heat-set inserts from
the back. Front layer: sensor bay along the bottom (SPS30 in a sealed split
duct with its ports facing down, gas sensors in their own vented bay), carrier
and Feather above it. Back layer: the cell, upright, under a printed cover.
Seen from the front, the USB-C port is on the right-hand side.
