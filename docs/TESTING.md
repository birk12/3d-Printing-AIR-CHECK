# Testing

## Status, honestly

Three levels, and it matters which is which:

| level | meaning |
|---|---|
| **HARDWARE-VERIFIED** | run on an assembled AIR CHECK |
| **BUILD-VERIFIED** | compiles or renders on a workstation, and its output was inspected |
| **SIMULATED** | exercised against synthetic data, not real sensors |

**Nothing in this repository is HARDWARE-VERIFIED.** No AIR CHECK has been
built. Every claim below is BUILD-VERIFIED or SIMULATED, and the physical
protocol further down is what has to be done before any of it should be
believed.

| area | status | evidence |
|---|---|---|
| measurement core logic | SIMULATED, 509 checks | `firmware/test/host/test_ac_core.c` |
| status LED logic | SIMULATED | part of the 509; every pattern and its timing |
| battery percentage from voltage | SIMULATED | part of the 509; curve, charge state, no-climb tracking |
| energy model | BUILD-VERIFIED | `tools/battery_calculator/model.py`, inputs traced to datasheets |
| electrical design | BUILD-VERIFIED | 460 rule checks in `electronics/schematic/design.py`, and the checker was shown to catch injected faults |
| enclosure | BUILD-VERIFIED | OpenSCAD asserts, an intersection check of every part against every other, and `tools/diagnostics/stl_check.py`: all parts manifold, 2.0 % overhang on the front shell |
| firmware build | BUILD-VERIFIED | full ESP-IDF v5.5.5 + esp-matter v1.6 build for esp32c6: 1.68 MB image (15 % free in the OTA slot), 210 kB DIRAM (46.5 %) |
| sensor drivers | **untested** | register addresses and timings read from datasheets |
| Matter attribute IDs in DASHBOARD_INTERFACE.md | BUILD-VERIFIED | checked against the Matter SDK's generated `AttributeIds.h` / `ClusterId.h` |
| Thread / Matter / Apple Home | **untested** | no controller has ever seen this device |

## Automated tests

### Measurement core

```bash
cc -std=c99 -Wall -Wextra -O1 -Ifirmware/components/ac_core/include \
   firmware/components/ac_core/src/*.c firmware/test/host/test_ac_core.c \
   -lm -o /tmp/ac_test && /tmp/ac_test
```

509 checks in about 20 ms. What they cover:

| area | examples |
|---|---|
| config | defaults are self-consistent; every profile gives the SEN63C at least the 30 s its CO2 output needs; a corrupted blob is rejected rather than half-applied; out-of-range values are clamped and counted |
| filters | the EMA reaches 63 % of a step after one time constant and gives the same answer at 6 x 10 s as at 1 x 60 s; least-squares slope recovers a known ramp; spike detection refuses to fire without enough samples |
| baseline | converges on a steady room; a frozen baseline does not learn the event; a reset adopts the current air; an implausible stored baseline is rejected on restore; no baseline means no delta rather than a fake one |
| air quality | the worst channel decides; two channels at the same level are both named; a steep rise escalates GOOD but never further; nothing measured is UNKNOWN, not GOOD |
| events | ordinary room noise never triggers; a single spike arms but does not confirm; a short dip does not end an event; the completed record carries the right peaks and durations; nothing can trigger without a baseline |
| history | a spike survives aggregation in the max field; the ring wraps without corrupting; trends are right for a ramp and for a flat room |
| engine | a cold engine measures immediately but publishes nothing; warm-up ends after the SGP40's documented 60 s; ECO schedules the SEN63C one hour out and nothing else asks for CO2; an event escalates to ACTIVE; USB switches to continuous, which keeps the SEN63C running between windows; critical battery stops measuring; a dead SEN63C does not stop the VOC channel; both dead is an ERROR; no weekly fan cleaning; publishing is driven by change, not by the clock; factory reset restores every default |
| status LED | dark in normal operation; a press shows the air-quality colour for exactly 3 s; pairing blinks blue and times out with the commissioning window; warm-up, low battery, critical battery and fault each have their own pattern; the unprompted critical-battery blip stays under 50 ms per 10 s; holding the button shows blue / cyan / red at 3 / 8 / 12 s and nothing past 20 s; calibration blinks cyan for longer than its 3 min run, then green or red |
| 24 h statistics | no history means no value, not zero; a one-minute spike survives into the 24 h peak; a 1 h window ending before the spike does not see it; the bucket in progress is included |
| battery | the voltage curve is monotonic and clamped, a NaN from a dead ADC reads 0 %; the low (20 %) and critical (5 %) points sit on the steep part; the reported value never climbs back on battery, but a freshly charged cell and charging are followed |

The suite was checked against a deliberately injected bug (inverting one
air-quality comparison) to confirm it fails when it should.

### Electrical rule check

```bash
python3 electronics/schematic/design.py
```

460 checks: every pin exists on its part and every pin of every part is
connected, no net has one connection, no pin is on two nets, every supply is
within its part's range and the part really sits on that rail, logic levels
cross correctly, I2C addresses are unique per bus, each bus has exactly one
pull-up pair **on the switched rail that powers its devices**, the SGP40 bus
sits on the only pads the C6's LP_I2C can use, the net list and the GPIO map
agree on every MCU pin, no strapping pin is used, every load switch is on an
LP GPIO, has a CT capacitor that keeps its inrush under 100 mA and has QOD
tied to VOUT, the USB sense divider gives a valid high without exceeding the
pad, the LED anode is on VBAT, the charge current is a sane C-rate.

Checked against injected faults (a pull-up moved to the always-on rail, QOD
left floating, the SEN63C bus moved to another GPIO, the LED anode moved to
3.3 V, a CT capacitor removed): each one is caught.

What the ERC could **not** catch are the bugs in EDR-11 and EDR-14: the
Feather's pull-ups on a switched regulator, and the LDO and LED on the sensor
breakouts. In both cases a board was modelled as a black box from its product
page, and only its actual schematic showed otherwise. The rule check is only
as good as the facts fed into it - which is why the FireBeetle and the SGP40
breakout in v1.2 were modelled from their vendors' schematics.

### Enclosure

```bash
openscad -o /tmp/f.stl cad/openscad/aircheck_case.scad -D 'part="front"'
python3 tools/diagnostics/stl_check.py cad/stl/aircheck_front.stl
```

The OpenSCAD model asserts on every render: the FireBeetle stack clears the
battery cover, the SEN63C fits the depth with its gasket, its ports give at
least Sensirion's minimum open area (108 and 227 mm² against 56 and 148) and
every port slot lands on its port face below the lap joint, nothing - battery
bay, cover skirt, carrier - enters the space behind the SEN63C's connector,
the carrier and its notch clear the screw posts, no screw post or keyhole hits
anything, the gas bay does not reach the SEN63C, and the LED and button sit
over the carrier clear of the FireBeetle. On top of that, every part was
intersected with every other in the assembly (front shell, lid, battery
cover, carrier, FireBeetle, SEN63C and its plug keep-out, SGP40, cell): the
only contacts are the designed zero-volume ones, and the one real collision
this found (the cover skirt against two screw posts) is fixed. The STL
checker reports mesh closure, bounding box, mass and overhang area.

### Firmware build

```bash
cd firmware
. $HOME/esp/esp-idf/export.sh && . $HOME/esp/esp-matter/export.sh
idf.py set-target esp32c6 build
```

Result:

```
v1.2  aircheck.bin 0x199770 bytes, 0x46890 (15 %) free in the 0x1e0000 app slot
      DIRAM 210 084 bytes used (46.47 %)
v1.1  aircheck.bin 0x192160 bytes, 0x4dea0 (16 %) free in the 0x1e0000 app slot
      DIRAM 210 484 bytes used (46.56 %), 241 628 remaining of 452 112
v1.0  aircheck.bin 0x19bc40 bytes (14 % free), DIRAM 224 636 bytes (49.69 %)
```

Cross-compiling found five bugs the host compiler and the host tests did not:

| | |
|---|---|
| `%u` against a `uint32_t`, which is `unsigned long` on riscv32 | `ac_display.c` (v1.0) |
| `gpio_deep_sleep_hold_en()` does not exist on parts that hold individual pads | `ac_hal.c` |
| missing `PRIV_REQUIRES` - the Matter headers were not on the include path | `main/CMakeLists.txt` |
| the Power Source cluster validates its features in `create()`, so adding them afterwards left the cluster aborted | `ac_matter.cpp` |
| esp-matter's default data model is the legacy one, whose cluster namespaces differ from the generated one | `ac_matter.cpp` |

None of these were findable without an actual cross-compile, which is the
argument for doing one even when no hardware exists to run it on.

## Bench tests, before the case is closed

These need hardware and are the first thing to do once you have it.

| # | test | pass |
|---|---|---|
| B1 | flash and boot | the banner appears, no panic, no reset loop |
| B2 | FireBeetle parts | before soldering: the JST PH socket is no taller than 6.0 mm (`FB_TOP_PARTS`), the underside is flat enough for a 2.5 mm header spacer, the board is hardware v1.2 (36 µA) |
| B3 | SEN63C present | the boot log prints its serial number and "CO2 self calibration on" |
| B4 | SEN63C window | fan audible for ~40 s an hour; PM plausible, a puff of smoke moves it; CO2 400-600 ppm in a ventilated room and over 1500 after breathing at the inlet; the log shows CO2 as a number, not -1 (window long enough) |
| B5 | SGP40 self test | the boot log does not say "SGP40 init failed" (the init runs the 0xD400 self test) |
| B6 | SGP40 with a pulsed rail | breathing on it moves the raw signal and the index follows within a minute; compare an hour against the same breakout on a permanently powered rail (`CONTINUOUS` on USB): the index must track |
| B7 | status LED | white at boot, all three colours on a press, blue when pairing, readable through the 0.6 mm skin in daylight |
| B8 | button | short, long (3-8 s), calibrate (8-12 s) and very long all do the right thing, and the hold colour matches; over 20 s does nothing |
| B18 | service console | with USB in, `aircheck_config.py get` lists the settings, `set name ...` changes the Matter NodeLabel, `diag` shows live values; on battery, the console is not running |
| B9 | battery voltage | the logged voltage agrees with a multimeter at the cell within 30 mV; the percentage falls across a day and never climbs back on battery |
| B10 | charging | the FireBeetle's charge LED comes on, USB_SENSE reads high, the log shows CHARGING, it switches to CONTINUOUS and keeps measuring; unplugged, the SEN63C stops within one loop |
| B11 | rails | both switched rails really are 0 V between measurements (QOD), and +3V3 does not dip below 3.2 V when either switch turns on (CT capacitors) |
| B12 | **quiescent current** | with a PPK II: the idle floor, both rails off, Thread attached. This is the number the whole battery claim rests on. The model says about 70 µA (39 µA C6 floor + 29 µA board + switches). Anything near 150 µA or more: look for a powered LED or a pin back-feeding a rail |
| B13 | sleep/wake | the device survives a night; uptime and history are continuous |
| B14 | watchdog | hold a sensor line low; the device recovers rather than hanging |
| B15 | NVS | reboot; baseline and history come back |
| B16 | **CO2 self calibration** | a week in ECO beside a reference CO2 meter, with the room aired daily: the SEN63C's ASC must hold within its ±(100 ppm + 10 %). If it drifts, run a forced recalibration outdoors (`docs/CALIBRATION.md`) and repeat |
| B17 | SEN63C window current | a PPK II on one 40 s window: replaces the datasheet's "after 60 s" figure in the model |

## Network tests

| # | test | pass |
|---|---|---|
| N1 | commissioning | the QR code scans, the device joins Thread, it appears in Apple Home |
| N2 | attributes | PM2.5, PM10, CO2, VOC, temperature, humidity and battery all appear |
| N3 | reporting | a change at the sensor reaches the Home app within about 15 s |
| N4 | reconnect | power-cycle the HomePod; the device re-attaches on its own |
| N5 | offline | unplug the border router; the device keeps measuring and its history keeps filling |
| N6 | automation | an Apple Home threshold automation fires |
| N7 | factory reset | 12 s press; the device leaves the fabric and re-advertises |

## Physical validation protocol

The point of this device is comparative measurement, so the protocol is a
comparison. Two units, one beside the printer and one across the room.

### Baseline

1. Both units powered, in the room, printer **off**, for **24 hours**.
2. Record: PM1/2.5/4/10, VOC index, CO2, temperature, humidity, every 15 min.
3. This is your reference. Do not skip it - without it the print data means
   nothing.

### Per material

For each of PLA, PETG, ABS, ASA, TPU and PA, if you print them:

| phase | duration | what to record |
|---|---|---|
| pre-print | 30 min | both units, confirm they are back at baseline |
| heat-up | until the first layer | when does VOC first move? |
| printing | the whole print | peaks, and the difference between the two units |
| post-print | 2 hours | the decay curve and the recovery time |
| recovery | until baseline | how long, and does it actually return |

Record the material, the brand, the nozzle and bed temperatures, the enclosure
state, whether ventilation ran, the room volume, and the window state. Without
those, the numbers are not comparable to anyone else's - or to your own, next
month.

### What you can and cannot conclude

You **can** say: "ABS at 250 C raised PM2.5 at the printer by 22 ug/m3 over
room level and the VOC index by 140, and it took 70 minutes to recover."

You **cannot** say: "ABS is unsafe", or "PLA is safe". This device does not
see ultrafine particles, cannot identify a chemical, and has no basis for a
health judgement. It measures change. That is genuinely useful and it is not
the same thing.

## Two-device test

| # | test | pass |
|---|---|---|
| T1 | both commission independently | no pairing step exists between them |
| T2 | both appear in Apple Home | as two separate accessories |
| T3 | independent names and rooms | renaming one does not touch the other |
| T4 | both report | values differ when the air differs |
| T5 | no interference | putting them 10 cm apart changes nothing |
| T6 | both usable in one automation | the Shortcuts recipes in `docs/SHORTCUTS.md` |
| T7 | one offline | removing one does not affect the other |
| T8 | identical firmware | the same binary on both, no per-unit build |
