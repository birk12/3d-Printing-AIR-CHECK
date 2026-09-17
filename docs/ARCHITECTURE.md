# Architecture

## What the device is

One battery-powered box that measures particulate matter, VOC, CO2,
temperature and humidity, and publishes them to Apple Home over Thread and
Matter. There is exactly one hardware design. If you want to watch two places,
you build two identical boxes and name them differently in the Home app.

## System

```
        ROOM AIR
            |
   +--------v---------+          +-------------------+
   |  sensor bay      |          | electronics bay   |
   |                  |          |                   |
   |  SPS30  (PM)     |  UART    |  ESP32-C6         |
   |  SGP40  (VOC)    +----I2C---+  Feather          |
   |  SCD41  (CO2/T/RH)          |  + ACC-1 carrier  |
   +------------------+          |  1.54" e-paper    |
        |                        |  one button       |
        v                        +---------+---------+
     EXHAUST                               |
                                           | 802.15.4
                                           v
                                    +-------------+
                                    |   Thread    |
                                    +------+------+
                                           |
                                    +------v------+
                                    | HomePod /   |
                                    | Apple TV    |  (border router)
                                    +------+------+
                                           |
                                    +------v------+
                                    | Apple Home  |
                                    +-------------+
```

No cloud, no account, no hub beyond the HomePod or Apple TV you already own,
no Home Assistant, no MQTT broker, no Raspberry Pi. If your network is down,
the device keeps measuring and the screen keeps working.

## Hardware

| block | part | why |
|---|---|---|
| MCU | ESP32-C6-MINI-1 on an Adafruit ESP32-C6 Feather | native 802.15.4, 512 kB SRAM, mature esp-matter support |
| PM | Sensirion SPS30 | factory calibrated, PM1/2.5/4/10 plus number concentration |
| VOC | Sensirion SGP40 | VOC Index with on-chip humidity compensation |
| CO2 | Sensirion SCD41 | true NDIR photoacoustic, and a single-shot mode that makes the power budget work |
| display | 1.54 in 200x200 e-paper, SSD1681 | holds an image at zero current |
| battery | 1S LiPo 4000 mAh with protection | see docs/BATTERY_LIFE.md |
| charger | MCP73831 on the Feather, 196 mA | USB-C, no extra parts |
| gauge | MAX17048 on the Feather | voltage alone does not tell you a LiPo's state of charge |

Full net list, pin map and the power tree: `electronics/schematic/NETLIST.md`,
generated and rule-checked by `electronics/schematic/design.py`.

## Power architecture

```
USB-C  -->  MCP73831 charger  -->  VBAT (1S LiPo, protected)
       -->  RT9080 LDO        -->  +3V3  (always on: MCU, gauge, I2C pull-ups)

VBAT --[SW1]--> TPS61023 boost --> +5V ------> SPS30
+3V3 --[SW2]--> +3V3_SENS ----------------- -> SGP40, SCD41
+3V3 --[SW3]--> +3V3_EPD --------------------> e-paper
```

Three TPS22918 load switches, all driven from RTC-capable GPIOs so they hold
their state through deep sleep. SW1 sits on the *input* of the boost converter
rather than its output, which removes the boost's own quiescent current as
well as the load - a boost with its enable pin low still passes its input
through to its output through the inductor and catch diode, so switching the
enable pin alone would leave the SPS30 sitting at about 3.4 V, below its 4.5 V
minimum and in an undefined state.

## Firmware

```
firmware/
  components/
    ac_core/        pure C99, no ESP-IDF headers, runs on a workstation
      ac_types      value types and state enums
      ac_config     configuration, validation, CRC, NVS serialisation
      ac_filter     time-aware EMA, least-squares rate, spike statistics
      ac_baseline   persistent room baseline, frozen during events
      ac_airquality threshold + trend classification with a stated reason
      ac_event      IDLE -> POSSIBLE_PRINT -> ACTIVE -> POST_PRINT -> NORMALIZED
      ac_history    5 min and 1 h aggregation rings, min/mean/max
      ac_engine     the scheduler: decides what to sample and when to sleep
      ac_display    200x200 1-bit renderer and the six screens
    ac_hal/         ESP-IDF drivers: SPS30 (UART/SHDLC), SGP40, SCD41,
                    SSD1681, MAX17048, button, NVS
    sensirion_gas_index/   vendored VOC Index algorithm, BSD-3
  main/
    app_main.cpp    two tasks: measure and UI
    ac_matter.cpp   the Matter data model
  test/host/        455 checks that run on a workstation
```

The split is the important part. `ac_core` decides *what happens and when*;
`ac_hal` only does what it is told. That is what makes the scheduling, the
event detection and the whole display layout testable without hardware, and
`firmware/test/host` exercises all of it in about 20 ms.

## Measurement flow

```
   RAW sample from a sensor
        |
        v
   ac_engine_submit ---> EMA filter ---> rate of change ---> running statistics
        |
        v
   ac_engine_tick
        |-- classify        (ac_airquality)
        |-- detect events   (ac_event)     --> freezes the baseline
        |-- update baseline (ac_baseline)  --> NVS every 30 min
        |-- aggregate       (ac_history)   --> 24 h fine, 7 d coarse
        |-- pick a mode     ECO / NORMAL / ACTIVE / POST_PRINT / CONTINUOUS
        `-- return a plan   what to sample next, how long we may sleep
        |
        v
   publish to Matter (only on a real change) and redraw the panel (only if awake)
```

## Device states

```
BOOT -> WARMUP -> NORMAL <--> ACTIVE -> POST_PRINT -> NORMAL
                    |
                    +--> CHARGING        (USB present)
                    +--> LOW_BATTERY     (<= 20 %)
                    +--> CRITICAL_BATTERY(<= 5 %, measurement stops)
                    +--> ERROR           (all three sensors failed)
                    +--> COMMISSIONING   (long button press)
                    `--> FACTORY_RESET   (very long button press)
```

Nothing fails silently: every sensor error increments a counter, three or five
consecutive failures mark that channel down, and a channel that is down is
visible on the diagnostics screen and excluded from the air-quality verdict
rather than reported as zero.

## Matter model

| endpoint | device type | clusters |
|---|---|---|
| 0 | Root Node | Power Source (battery) |
| 1 | Air Quality Sensor `0x002C` | Air Quality, PM2.5, PM10, PM1, CO2, TVOC |
| 2 | Temperature Sensor `0x0302` | Temperature Measurement |
| 3 | Humidity Sensor `0x0307` | Relative Humidity Measurement |

All standard device types and clusters. No proprietary extensions. What Apple
Home actually shows is in `docs/APPLE_HOME.md`, including the two places where
it does not show everything.

## Two devices

There is no pairing, no master and no slave. Two AIR CHECKs are two
independent Matter nodes that happen to run the same firmware. Apple Home sees
them as two accessories; you name one "3D Printer" and one "Room", and
Shortcuts can compare their values. Nothing in the firmware knows or cares
that another unit exists.
