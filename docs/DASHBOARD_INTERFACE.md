# Dashboard interface

Everything a separate display needs to show AIR CHECK data **locally**,
without Home Assistant, a cloud service or any hub beyond the HomePod mini you
already have. Written to be handed to whoever builds the dashboard.

## How the data gets there

Apple Home has **no local API**. A third-party device cannot ask the HomePod
for a sensor's values. What does work is **Matter multi-admin**: the same
sensor belongs to Apple Home *and* to the dashboard at the same time, and the
dashboard reads it directly.

```
 AIR CHECK "3D Printer" ──┐                        ┌──► Apple Home (fabric 1)
                          ├── Thread ──► HomePod ──┤
 AIR CHECK "Room" ────────┘   (sleepy    mini      └──► Dashboard (fabric 2)
                               end       (Thread        reads the sensors directly,
                               devices)  border         via IPv6, routed by the
                                         router)        HomePod
```

- The **HomePod mini is the network bridge**. As Thread border router it
  routes IPv6 between the Thread mesh and your Wi-Fi LAN. The data does pass
  through it, but it never interprets or stores it.
- The **dashboard is a second Matter controller** on its own fabric. A
  Wi-Fi ESP32 on the same LAN is enough: it reaches the Thread sensors through
  the HomePod's routing, like any other Matter controller does.
- **Apple Home keeps working** exactly as before. The two fabrics do not
  interfere. The sensor supports 5 fabrics (`CONFIG_MAX_FABRICS=5`).

### Pairing the dashboard with a sensor (once per sensor)

1. In the Home app: long-press the sensor → **Accessory Settings** →
   **Turn On Pairing Mode** → copy the code. The code is freshly generated
   and expires after a few minutes.
   ([Apple Support](https://support.apple.com/en-us/102135))
2. Give that code to the dashboard, which commissions the sensor
   **on-network**. The sensor is already on Thread, so no BLE is needed.
   With esp-matter's controller component:
   ```
   esp_matter::controller::pairing_command::pairing_code(node_id, "34970112332");
   ```
   (`components/esp_matter_controller/commands/esp_matter_controller_pairing_command.h`;
   the CLI equivalent is `matter esp controller pairing code <node-id> <code>`).
3. Pick a distinct `node_id` per sensor on the dashboard's fabric, and store it.

On the sensor there is nothing to configure: multi-admin is standard Matter.
Alternatively, a 3–8 s button press opens the sensor's own commissioning
window (5 minutes, the LED blinks blue). Its code is in the serial log and on
the label.

### If the controller is a Raspberry Pi (python-matter-server)

A dashboard design can move the Matter work off the battery-powered display
onto an always-on Raspberry Pi running the Open Home Foundation's
`python-matter-server`. The Pi becomes the second controller, renders the
pages, and the display only fetches them. That works with this sensor
unchanged, and is the better fit: a mains-powered controller can simply hold a
subscription.

- **Commission with the shared code** from "Turn On Pairing Mode":
  `commission_with_code(code, network_only=True)`. The sensor is already on
  Thread, so it is on-network commissioning and no Bluetooth is involved.
- **The Pi must learn the route to the Thread network.** The HomePod
  advertises the Thread prefix in IPv6 router advertisements (Route
  Information Option). Linux ignores those by default, and then the Pi simply
  cannot reach any Thread device, with no clear error. On the Pi's LAN
  interface:
  ```
  net.ipv6.conf.<iface>.accept_ra = 1          # 2 if IPv6 forwarding is on
  net.ipv6.conf.<iface>.accept_ra_rt_info_max_plen = 64
  ```
- **Keep the subscription the server sets up.** Apple Home already holds one;
  a second subscriber means one extra report per attribute change. Changes
  are already rate-limited by the deadbands below. That is roughly the same
  traffic as the 15-minute reads the energy model budgets (1.4 mWh/day), and
  well inside the margin.

## What to read

All values are standard clusters on five endpoints. No vendor extensions.

### Endpoint 0: device

| cluster | attribute | id | type / unit | meaning |
|---|---|---|---|---|
| Basic Information `0x0028` | NodeLabel | `0x0005` | string ≤ 32 | **the device name** ("3D Printer", "Room"). Use this to label tiles. Apple Home's own names are not visible to a second fabric. |
| Power Source `0x002F` | Status | `0x0000` | enum8 | the cells: **1 Active** = running on them, **2 Standby** = on USB-C, cells as backup, **3 Unavailable** = on USB-C, no cells |
| Power Source | Description | `0x0002` | string | "LiFePO4 1S4P" |
| Power Source | BatPresent | `0x0011` | bool | false = no cells |
| Power Source | BatPercentRemaining | `0x000C` | uint8, **half-percent** (0–200), nullable | divide by 2; coarse, from the cell voltage; null without cells |
| Power Source | BatVoltage | `0x000B` | uint32, mV | cell voltage (four cells in parallel) |
| Power Source | BatChargeLevel | `0x000E` | enum8 | 0 OK, 1 Warning (below 3.20 V, about 10 %), 2 Critical (below 3.10 V, about 6 %; the sensor stops measuring) |
| Power Source | BatChargeState | `0x001A` | enum8 | 0 Unknown, 1 **IsCharging**, 2 IsAtFullCharge (also during the charge pause), 3 IsNotCharging |
| Power Source | BatReplacementNeeded | `0x000F` | bool | true on a latched charger fault (the safety timer ran out twice, or an ISET / over-current fault): show it as a fault - unplug and replug USB-C, then `docs/TROUBLESHOOTING.md` |
| Power Source | BatReplacementDescription | `0x0013` | string | "4 x AER18650m2A2 LiFePO4, all at once" |
| Power Source | BatQuantity | `0x0019` | uint8 | 4 |

### The USB-C endpoint

The USB-C input is a second Power Source, on its own endpoint after the
humidity endpoint (device type Power Source `0x0011`; the sensor's boot log
prints its number as `usb-c=N`). Find it by its Description rather than by a
fixed number.

| cluster | attribute | id | type / unit | meaning |
|---|---|---|---|---|
| Power Source `0x002F` | Status | `0x0000` | enum8 | **1 Active** while powered from USB-C, **3 Unavailable** otherwise |
| Power Source | Description | `0x0002` | string | "USB-C" |
| Power Source | Order | `0x0001` | uint8 | 0 (preferred; the cells are 1) |
| Power Source | WiredPresent | `0x0009` | bool | true while powered from USB-C |
| Power Source | WiredCurrentType | `0x0005` | enum8 | 1 DC |

A computer on the sensor's FireBeetle USB-C (for configuration) also shows as
USB-C Active, although it does not charge the cells.

#### USB-C or battery

The sensor runs from USB-C with the cells as the backup, charged in the
device (EDR-21). Read the two sources together:

| USB-C `Status` | cells `Status` | `BatPresent` | show |
|---|---|---|---|
| 3 Unavailable | 1 Active | true | **battery**: percentage, and a warning at `BatChargeLevel` ≥ 1 |
| 1 Active | 2 Standby | true | **USB-C**; optionally the cells' percentage, and "charging" while `BatChargeState` is 1 |
| 1 Active | 3 Unavailable | false | **USB-C**, no cells: no battery tile, no warning |

On USB-C the sensor raises no low or critical battery state of its own, but
`BatChargeLevel` still reports the cells - worth a quiet hint, not an alarm.
The sensor updates both sources on every battery read (every 5 min by
default), so a change can take that long to show.

#### Mark temperature and humidity while the cells charge

The charger is a linear one and turns 0.85–1.7 W into heat inside the case
while it charges. It sits in its own vented chamber, 54–93 mm from the
sensors, but **temperature and humidity can read a little high while
`BatChargeState` = 1 (IsCharging)**. The sensor compensates nothing; there is
no measured offset yet (TESTING T-L7). The dashboard should mark those two
values in that window, e.g. greyed or with a "charging" note, and may leave
them out of daily min/max. On permanent USB-C the firmware pauses charging
after "full" and tops up about once a month, so the window is rare; from flat
it lasts 8–9 h.

### Endpoint 1: air quality

| cluster | attribute | id | unit | notes |
|---|---|---|---|---|
| Air Quality `0x005B` | AirQuality | `0x0000` | enum8 | 0 Unknown, 1 Good, 3 Moderate, 4 Poor, 5 VeryPoor |
| PM2.5 `0x042A` | MeasuredValue | `0x0000` | float, µg/m³ | |
| PM2.5 | PeakMeasuredValue | `0x0003` | float, µg/m³ | **highest in the last 24 h** |
| PM2.5 | PeakMeasuredValueWindow | `0x0004` | uint32, s | 86400 |
| PM2.5 | AverageMeasuredValue | `0x0005` | float, µg/m³ | **mean of the last 24 h** |
| PM2.5 | AverageMeasuredValueWindow | `0x0006` | uint32, s | 86400 |
| PM10 `0x042D` | Measured / Peak / Average | as PM2.5 | float, µg/m³ | "peak" is the highest 5-min mean |
| PM1 `0x042C` | MeasuredValue | `0x0000` | float, µg/m³ | no peak/average |
| CO₂ `0x040D` | Measured / Peak / Average | as PM2.5 | float, ppm | |
| TVOC `0x042E` | Measured / Peak / Average | as PM2.5 | float, "ppb" | **VOC Index 1–500, not a concentration.** 100 = this room's 24 h average. Label it "VOC index" on the dashboard. |

The peak and average attributes exist so a dashboard that sleeps most of the
time can still show "worst today" without keeping its own history.

### Endpoints 2 and 3

| endpoint | cluster | attribute | type | scaling |
|---|---|---|---|---|
| 2 | Temperature Measurement `0x0402` | MeasuredValue `0x0000` | int16 | °C × 100 |
| 3 | Relative Humidity `0x0405` | MeasuredValue `0x0000` | uint16 | % × 100 |

### Null values

Before the sensor has finished warming up (about 60 s after power-on for VOC,
and until the first particle window has run), measured values are **null**,
not zero. Show "—". A null means "not measured yet", never "clean air".

## How fresh the data is

| channel | ECO (default) | during a detected print (ACTIVE) |
|---|---|---|
| PM1 / PM2.5 / PM10 | **every 60 min** | every 2 min |
| VOC index | every 10 s | every 10 s |
| CO₂ | every 5 min | every 2 min |
| temperature, humidity | every 10 s | every 10 s |
| battery, USB-C, charge state | every 5 min | every 5 min |

Since v1.3 each quantity has its own sensor and its own clock: particles from
the SEN62, CO₂ from the Senseair Sunrise, temperature and humidity from the
SHT40 together with the VOC sample. In NORMAL, particles come every 15 min;
CO₂ stays at 5 min. On external power (a USB-C charger, or a computer on the
FireBeetle's USB-C) the device runs CONTINUOUS (particles continuously, CO₂
every minute), whatever the configured mode; the USB-C source's `Status` 1
tells you so. The battery percentage is a coarse reading of the cell voltage.
LiFePO4 is flat between about 3.26 and 3.33 V, so it is good for "full" and
"nearly empty", not in between; `BatChargeLevel` carries the warnings. It is
not a fuel gauge.

The sensor switches to ACTIVE by itself when VOC or PM rises above its
baseline, and back afterwards via POST_PRINT (45 min). So a stale PM value in
ECO means "nothing is happening", not a fault.

Attributes only change when the value moves by at least: PM 0.5 µg/m³ (PM10
1.0), CO₂ 15 ppm, VOC 5 points, temperature 0.2 °C, humidity 1 %, battery 1 %.

## Reading from a battery-powered dashboard

The sensor is a **sleepy Thread device**: a Matter SIT ICD with a **15 s slow
poll**. Its parent router holds any message for it until it next wakes up.

- **Any request can take up to 15 s to be answered.** Once the sensor has
  answered one message it switches to fast polling for a moment, so the rest
  of the exchange (CASE handshake, read) is quick.
- **Do one read per wake-up**, not one per attribute: a single ReadRequest
  with several attribute paths (or wildcards per endpoint). Wake, read both
  sensors, render, sleep.
- **Use CASE session resumption** so each wake does not repeat the full
  handshake.
- **Reading is better than subscribing** for a dashboard that sleeps.
  Holding a subscription means staying on Wi-Fi. If the dashboard is on mains
  power, subscribe with a max interval of a few minutes instead.
- **Every 15 min is plenty.** PM only changes hourly in ECO and CO₂ every
  5 min; temperature and humidity move slowly.
- If a mains-powered controller sits in between (the Raspberry Pi case above),
  let *it* talk Matter and let the battery display fetch rendered pages. The
  display then never has to wait out the sensor's 15 s poll.

### What it costs the sensor

The sensor's energy model already includes a dashboard reading **every 15
minutes**, costing about 10 fast polls per read: **1.4 mWh/day, under 1 % of
the ECO budget**. At every 5 minutes the runtime drops by about 2 %, which is
still acceptable. Continuous polling every few seconds is not: it would keep
the sensor's radio in active mode.

## What is *not* available over Matter

| data | why | workaround |
|---|---|---|
| event state (printing detected / post-print) | no standard cluster fits, and inventing one would break compatibility | infer it: VOC index well above 100 plus rising PM means a print is running |
| air-quality *reason* ("PM2.5 + VOC") | no standard attribute | compute it on the dashboard from the values and thresholds below |
| event log (peak, duration and recovery of past prints) | no standard cluster | kept in the sensor's flash; readable over USB serial |
| NOx | the SGP40 does not measure it | – |

### Thresholds the sensor uses, so the dashboard can match them

| channel | Elevated | High | Very high |
|---|---|---|---|
| PM2.5 µg/m³ | 15 | 35 | 75 |
| PM10 µg/m³ | 45 | 100 | 150 |
| VOC index | 150 | 250 | 350 |
| CO₂ ppm | 1000 | 1500 | 2000 |

PM2.5 anchors: WHO 2021 24 h guideline 15, interim target 1 75. These are
reporting thresholds for a consumer device, not health limits.

## Status of this interface

The cluster and attribute layout is **build-verified**: it is what
`firmware/main/ac_matter.cpp` creates, and the firmware compiles. It has
**not** been exercised by a real second controller, because no AIR CHECK has
been built yet. The first time a dashboard reads a sensor, check in this
order: NodeLabel, one MeasuredValue, one PeakMeasuredValue, battery, both
Power Sources' `Status` with and without a charger in the sensor's USB-C
socket, and `BatChargeState` while it charges.
