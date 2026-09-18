# Architecture (v1.2)

## What the device is

One battery-powered box that measures particulate matter, CO2, temperature,
humidity and VOC, and publishes them over Thread and Matter. It has no
display: numbers are shown in Apple Home and on a separate e-ink dashboard.
There is exactly one hardware design. To watch two places, build two identical
boxes and name them differently.

## System

```
                 ┌───────────────── AIR CHECK ──────────────────────┐
  ROOM AIR ──►   │ left wall             │ front layer               │
  (ports)        │  SEN63C  PM1-10, CO2, │  ACC-1 carrier            │
                 │          T, RH (I2C)  │  FireBeetle 2 ESP32-C6    │
  ROOM AIR ──►   │ VOC bay: SGP40 (I2C)  │  button + RGB LED         │
  (vents)        │                back layer (lid): 4000 mAh LiPo    │
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

| block | part | why |
|---|---|---|
| MCU | DFRobot FireBeetle 2 ESP32-C6 | native 802.15.4, 512 kB SRAM, USB-C, CN3165 charger, TPS62A02 buck, battery divider; EUR 7.50 (EDR-1, EDR-15) |
| PM, CO2, T, RH | Sensirion SEN63C | one factory-calibrated module at 3.3 V instead of SPS30 + SCD41 + boost converter (EDR-15) |
| VOC | Sensirion SGP40 (Adafruit breakout) | VOC Index with on-chip humidity compensation; the 10 s tripwire (EDR-5) |
| status | Adafruit 159 RGB LED + one tactile switch | the only local output (EDR-12) |
| battery | 1S LiPo 4000 mAh with protection | see docs/BATTERY_LIFE.md |

Full net list, pin map and power tree: `electronics/schematic/NETLIST.md`,
generated and rule-checked by `electronics/schematic/design.py`.

## Power

```
LiPo ─J2─┬─J3─► FireBeetle BAT ─► CN3165 charger (540 mA from USB-C)
         │                      └► TPS62A02 buck ─► +3V3 (always on):
         │                                          ESP32-C6, button pull-up
         └────► LED1 common anode (VBAT)

+3V3 ─[SW1 TPS22918, CT 4.7 nF, GPIO2]─► +3V3_SEN6X ─► SEN63C, its pull-ups
+3V3 ─[SW2 TPS22918, CT 1 nF,   GPIO3]─► +3V3_SENS  ─► SGP40 breakout, its pull-ups
VIN (USB 5 V) ─ 68k/100k ─► GPIO18   USB present
VBAT ─ 1M/1M (on the FireBeetle) ─► GPIO0 (ADC)   battery voltage
```

* Both sensor rails are **off most of the time**. The SEN63C idles at 3.3 mA
  and is only powered for its window (40 s an hour in ECO). The SGP40
  breakout carries its own LDO and power LED, ~185 µA, so its rail is only up
  for the 0.25 s a VOC sample takes (EDR-14).
* Each rail has a CT capacitor, so switching it on does not pull an amp-level
  spike out of the MCU's own 3.3 V, and QOD tied to VOUT, so it collapses when
  switched off.
* The cell plugs into the carrier and is passed through to the FireBeetle,
  because the FireBeetle has no VBAT pin and the LED needs its anode there.

## Two I2C buses

| bus | pins | pull-ups | device | powered |
|---|---|---|---|---|
| VOC | LP_I2C, GPIO6 SDA / GPIO7 SCL (fixed pads) | 4.7k to +3V3_SENS | SGP40 0x59 | ~0.25 s every 10 s |
| SEN | HP I2C, GPIO19 SDA / GPIO20 SCL | 4.7k to +3V3_SEN6X | SEN63C 0x6B | one window an hour (ECO) |

Each bus exists only while its rail is up. When a rail drops, the firmware
deletes the bus and releases both pins to inputs, so nothing drives current
into an unpowered sensor.

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
      ac_battery     battery percentage from the cell voltage
    ac_hal/          ESP-IDF drivers: SEN63C, SGP40 (pulsed rail), battery ADC
                     and USB sense, LED, button, NVS, two I2C buses
    sensirion_gas_index/   vendored VOC Index algorithm, BSD-3
  main/
    app_main.cpp     two tasks: measure, and button/LED
    ac_matter.cpp    the Matter data model
    ac_console.cpp   service console on USB (only while USB power is present)
  test/host/         509 checks that run on a workstation
```

`ac_core` decides *what happens and when*; `ac_hal` only does what it is told.
That split is why the scheduling, event detection, LED logic, battery
percentage and the 24 h statistics are testable without hardware.

## Measurement flow

```
   SEN63C window (PM + CO2 + T + RH)  ┐
   SGP40 sample (VOC)                 ┴─► ac_engine_submit ─► EMA ─► rate ─► statistics
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

A SEN63C window: power on, 100 ms to I2C, start measurement, read once a
second. PM is averaged over the part of the window after its 30 s start-up;
CO2 reads "unknown" for the first 22-24 s and the last valid reading is used;
then stop, power off. On USB (CONTINUOUS) the module stays on between windows.

## Device states

```
BOOT → WARMUP → NORMAL ⇄ ACTIVE → POST_PRINT → NORMAL
                  ├─► CHARGING          (USB present)
                  ├─► LOW_BATTERY       (≤ 20 %)
                  ├─► CRITICAL_BATTERY  (≤ 5 %, measurement stops)
                  ├─► ERROR             (both sensors failed)
                  ├─► COMMISSIONING     (3–8 s button press)
                  ├─► CO2 calibration   (8–12 s press, outdoors: 3 min run + FRC)
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

112 × 102 × 32 mm: a 28 mm deep PETG front shell and a 4 mm lid, four M2.5
screws into heat-set inserts from the back. The SEN63C stands on its side
against the left wall (seen from the front), its inlet and outlet windows
gasketed to slots in that wall, because Sensirion's design-in guide wants the
ports facing sideways. The SGP40 has its own sealed, vented bay in the
bottom-right corner. The carrier and FireBeetle sit above it; the cell lies in
a bay on the lid under a clip-on cover. Seen from the front, the USB-C port is
on the right-hand side.
