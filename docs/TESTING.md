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
| measurement core logic | SIMULATED, 466 checks | `firmware/test/host/test_ac_core.c` |
| status LED logic | SIMULATED | part of the 466; every pattern and its timing |
| energy model | BUILD-VERIFIED | `tools/battery_calculator/model.py`, inputs traced to datasheets |
| electrical design | BUILD-VERIFIED | 344 rule checks in `electronics/schematic/design.py` |
| enclosure | BUILD-VERIFIED | OpenSCAD asserts + `tools/diagnostics/stl_check.py`: manifold, fits the bed, 1.7 % overhang |
| firmware build | BUILD-VERIFIED | full ESP-IDF v5.5.5 + esp-matter v1.6 build for esp32c6: 1.65 MB image (16 % free in the OTA slot), 210 kB DIRAM (46.6 %) |
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

466 checks in about 20 ms. What they cover:

| area | examples |
|---|---|
| config | defaults are self-consistent; every profile obeys the sensor datasheets; a corrupted blob is rejected rather than half-applied; out-of-range values are clamped and counted |
| filters | the EMA reaches 63 % of a step after one time constant and gives the same answer at 6 x 10 s as at 1 x 60 s; least-squares slope recovers a known ramp; spike detection refuses to fire without enough samples |
| baseline | converges on a steady room; a frozen baseline does not learn the event; a reset adopts the current air; an implausible stored baseline is rejected on restore; no baseline means no delta rather than a fake one |
| air quality | the worst channel decides; two channels at the same level are both named; a steep rise escalates GOOD but never further; nothing measured is UNKNOWN, not GOOD |
| events | ordinary room noise never triggers; a single spike arms but does not confirm; a short dip does not end an event; the completed record carries the right peaks and durations; nothing can trigger without a baseline |
| history | a spike survives aggregation in the max field; the ring wraps without corrupting; trends are right for a ramp and for a flat room |
| engine | a cold engine measures immediately but publishes nothing; warm-up ends after the SGP40's documented 60 s; ECO schedules the SPS30 one hour out; an event escalates to ACTIVE; USB switches to continuous; critical battery stops measuring; one dead sensor does not stop the others; all three dead is an ERROR; the fan is cleaned weekly; publishing is driven by change, not by the clock; factory reset restores every default |
| status LED | dark in normal operation; a press shows the air-quality colour for exactly 3 s; pairing blinks blue and times out with the commissioning window; warm-up, low battery, critical battery and fault each have their own pattern; the unprompted critical-battery blip stays under 50 ms per 10 s |
| 24 h statistics | no history means no value, not zero; a one-minute spike survives into the 24 h peak; a 1 h window ending before the spike does not see it; the bucket in progress is included |

The suite was checked against a deliberately injected bug (inverting one
air-quality comparison) to confirm it fails when it should.

### Electrical rule check

```bash
python3 electronics/schematic/design.py
```

344 checks: every pin exists on its part, no net has one connection, no pin is
on two nets, every supply is within its part's range, logic levels cross
correctly, I2C addresses are unique per bus, each bus has exactly one pull-up
pair **on the rail that powers its devices**, the sensor bus sits on the only
pads the C6's LP_I2C can use, no strapping pin is used, every load switch is on
an RTC GPIO, the boost can start the fan, the charge current is a sane C-rate.

What the ERC could **not** catch is the v1.0 bug in EDR-11: the Feather's I2C
pull-ups live on a regulator that the firmware switched off. The Feather was
modelled as a black box with "pull-ups to 3V3", taken from the learn guide.
Only reading Adafruit's actual schematic showed otherwise. The rule check is
only as good as the facts fed into it.

### Enclosure

```bash
openscad -o /tmp/f.stl cad/openscad/aircheck_case.scad -D 'part="front"'
python3 tools/diagnostics/stl_check.py cad/stl/aircheck_front.stl
```

The OpenSCAD model asserts on every render: the Feather stack cannot collide
with the battery, the SPS30 cannot cross the shell seam or the sensor bay
divider, the carrier board fits, the battery bay fits both ways, no screw post hits the carrier, the SPS30 cradle, a gas breakout or the battery bay, no keyhole puts a screw head against the cell, the LED and button sit over the carrier and cannot overlap
the button. The STL checker reports mesh closure, bounding box, mass and
overhang area.

### Firmware build

```bash
cd firmware
. $HOME/esp/esp-idf/export.sh && . $HOME/esp/esp-matter/export.sh
idf.py set-target esp32c6 build
```

Result:

```
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
| B2 | SPS30 serial number | `ac_sps30_serial()` returns a plausible string |
| B3 | SPS30 measurement | fan audible, PM values plausible, a puff of smoke moves them |
| B4 | SGP40 self test | `ac_sgp40_self_test()` returns 0xD400 |
| B5 | SGP40 response | breathing on it moves the raw signal, the index follows within a minute |
| B6 | SCD41 single shot | 400-600 ppm in a ventilated room; breathing on it goes over 2000 |
| B7 | status LED | white at boot, all three colours on a press, blue when pairing, readable through the 0.6 mm skin in daylight |
| B8 | button | short, long and very long all do the right thing; over 20 s does nothing |
| B9 | battery gauge | the percentage tracks a charge and a discharge **across a whole day** with GPIO20 only pulsed (EDR-11: MAX17048 bus-low sleep) |
| B10 | charging | the Feather's charge LED comes on, the log shows CHARGING, it keeps measuring |
| B11 | rails | measure that the 5 V rail really is 0 V between PM measurements |
| B12 | **quiescent current** | with a PPK II: the idle floor. This is the number the whole battery claim rests on. The model says about 100 uA (39 uA MCU floor + 61 uA carrier). A reading near 1 mA means the Feather's WS2812B is powered (EDR-11). |
| B13 | sleep/wake | the device survives a night; uptime and history are continuous |
| B14 | watchdog | hold a sensor line low; the device recovers rather than hanging |
| B15 | NVS | reboot; baseline and history come back |

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
