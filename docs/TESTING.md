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
| measurement core logic | SIMULATED, 546 checks | `firmware/test/host/test_ac_core.c` |
| status LED logic | SIMULATED | part of the 546; every pattern and its timing |
| battery percentage from the AA pack | SIMULATED | part of the 546; per-chemistry curves, energy counter, fresh cells detected |
| power source (cells / external / holder empty) | SIMULATED | part of the 546; VSYS thresholds with the ADC error on both sides, Matter Status per source |
| energy model | BUILD-VERIFIED | `tools/battery_calculator/model.py`, inputs traced to datasheets |
| electrical design | BUILD-VERIFIED | 567 rule checks in `electronics/schematic/design.py`, one standing warning (T-P3) |
| enclosure | BUILD-VERIFIED | OpenSCAD asserts over every module, post and zone, and `tools/diagnostics/stl_check.py`: all four parts manifold, 1.1 % overhang on the front shell |
| firmware build | BUILD-VERIFIED | full ESP-IDF v5.5.5 + esp-matter v1.6 build for esp32c6: 1.69 MB image (14 % free in the OTA slot), 210 kB DIRAM (46.5 %) |
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

546 checks in about 10 ms. What they cover:

| area | examples |
|---|---|
| config | defaults are self-consistent; every profile gives the SEN62 its 30 s start-up plus at least 30 s averaged; the CO2 interval leaves room for a 32-sample Sunrise measurement; the SIT ICD slow poll stays at or under 15 s; a corrupted blob is rejected rather than half-applied; out-of-range values (window, cell type, altitude) are clamped and counted |
| filters | the EMA reaches 63 % of a step after one time constant and gives the same answer at 6 x 10 s as at 1 x 60 s; least-squares slope recovers a known ramp; spike detection refuses to fire without enough samples |
| baseline | converges on a steady room; a frozen baseline does not learn the event; a reset adopts the current air; an implausible stored baseline is rejected on restore; no baseline means no delta rather than a fake one |
| air quality | the worst channel decides; two channels at the same level are both named; a steep rise escalates GOOD but never further; nothing measured is UNKNOWN, not GOOD |
| events | ordinary room noise never triggers; a single spike arms but does not confirm; a short dip does not end an event; the completed record carries the right peaks and durations; nothing can trigger without a baseline |
| history | a spike survives aggregation in the max field; the ring wraps without corrupting; trends are right for a ramp and for a flat room |
| engine | a cold engine measures immediately but publishes nothing; warm-up ends after the SGP40's documented 60 s; ECO over two hours gives exactly 2 SEN62 windows, 24 CO2 shots and 720 VOC samples; VOC keeps its 10 s grid through a 60 s particle window; an event escalates to ACTIVE; USB switches to continuous, which keeps the SEN62 running between windows; critical battery stops measuring; a dead SEN62 does not stop the other channels; SEN62, Sunrise and SGP40 all dead is an ERROR; publishing is driven by change, not by the clock; factory reset restores every default |
| status LED | dark in normal operation; a press shows the air-quality colour for exactly 3 s; pairing blinks blue and times out with the commissioning window; warm-up, low battery, critical battery and fault each have their own pattern; the unprompted critical-battery blip stays under 50 ms per 10 s; holding the button shows blue / cyan / red at 3 / 8 / 12 s and nothing past 20 s; calibration blinks cyan for longer than its 3 min settling, then green or red |
| 24 h statistics | no history means no value, not zero; a one-minute spike survives into the 24 h peak; a 1 h window ending before the spike does not see it; the bucket in progress is included |
| AA pack | every cell curve (alkaline, NiMH, L91) is monotonic and clamped, a NaN from a dead ADC reads 0 %; flat L91 cells are carried by the energy counter; a voltage near the end wins over an optimistic counter; the value never climbs on noise, but fresh cells reset it; the same cells taken out on mains and put back keep their counter |
| power source | VSYS at 3.98 V + ADC error is still the cells, 4.17 V - ADC error is already external power; an enumerated USB host is external power without any reading; a pack under 3.0 V on external power is an empty holder; Matter Status 1 / 2 / 3 for cells / external with backup / external, holder empty |

The suite was checked against a deliberately injected bug (inverting one
air-quality comparison) to confirm it fails when it should.

### Electrical rule check

```bash
python3 electronics/schematic/design.py
```

```
ERC: 567 checks, 0 error(s), 1 warning(s)
  WARN  SW1 has no soft start: the SEN62's switch-on step lands on the FireBeetle's 3.3 V buck - verify on the bench (TESTING T-P3)
```

What the 567 cover: every pin exists on its part and every pin of every part
is connected or declared open, no net has one connection, no pin is on two
nets, every supply is within its part's range and the part really sits on
that rail, the pack reaches nothing except through the PTC fuse, **nothing
can charge the cells** (the LM66200 is the only link between +VREG and VSYS,
and the FireBeetle charger's 4.2 V is far enough above +VREG for it to
block), **USB 5 V never touches the pack** (VBUS_EXT and VPACK_F share
nothing), PS2 feeds VIN2 and nothing else, +VEXT is far enough above +VREG
for external power to take over and stays inside the FireBeetle's rating,
the firmware's external-power threshold (`AC_EXT_POWER_V`, read from
`ac_battery.h`) is clear of both branches including the ADC's error, the
undervoltage lockout (R11 on PS1's EN) switches off at no less than 1.0 V per
cell and restarts from a fresh NiMH pack, the regulator, the #2810 and the
fuse carry their loads with margin, the pack divider stays inside the ADC range, I2C
addresses are unique, each switched bus has exactly one pull-up pair on the
rail that powers its devices and the LP bus uses the modules' own, the
Sunrise's VDDIO shares GPIO18 with its pull-ups, COMSEL is grounded and EN
has its pull-down, the LP bus is on GPIO6/7, no strapping pin is used, only
an LED sits on GPIO16, the net list, the GPIO map and the firmware's
`AC_PIN_` table agree, and no LED glows with its GPIO high.

The one warning stands on purpose: the Pololu #2810 has no soft start, and
whether the SEN62's switch-on step upsets the FireBeetle's 3.3 V is a bench
measurement, not a rule (T-P3).

What the ERC cannot catch is a module that differs from what was fed into it
(EDR-11, EDR-14: in both cases a board was modelled from its product page and
only its schematic showed otherwise). In v1.3 every module was modelled from
its vendor's drawings and board files; the Grove SHT40 and the Sunrise's pin
rows are not published and are measured before printing (T-M1, T-M2).

### Enclosure

```bash
for p in front back door stand; do
  openscad -o /tmp/$p.stl cad/openscad/aircheck_case.scad -D "part=\"$p\""
  python3 tools/diagnostics/stl_check.py /tmp/$p.stl --build 250x210x220
done
```

The OpenSCAD model asserts on every render. Each module is a box in the
model (holder with cells, SEN62 and its plug keep-out, SHT40, SGP40, Sunrise
and its filter clearance, FireBeetle and its solder side, the #2810, the
S9V11E2A, the LM66200, LED holder, button, USB-C plug), and:

* no two of them overlap, every one is inside the case, and each sits in its
  zone - Sunrise, SGP40 and SHT40 in the gas bay, the holder in the battery
  compartment, everything else in the electronics zone;
* no lid post or door post hits any of them;
* the SEN62 cradle clears the posts, its ports get at least Sensirion's
  minimum open area (108 and 226.8 mm² against 56 and 148), every port row
  lands on the port face below the lap joint;
* the Sunrise keeps 1.0 mm to the walls and 1.5 mm from its filter to the lid
  (ANO4947), the SHT40 is clear of its neighbours, every gas-bay vent is
  inside the bay and the bay has at least 200 mm² open;
* there is room over the cells for the door lip, the partition core is at
  least 1.6 mm, the lead notch opens into neither the gas bay nor the SEN62,
  and the lid / door split sits over the partition;
* LED, button, USB-C opening and keyholes stay clear of the gas bay; walls are
  at least four extrusions; no insert pocket breaks through the front face;
  no bridge is longer than 12-13 mm.

The STL checker reports mesh closure, bounding box, mass and overhang area
(front shell 1.1 %, lid 1.0 %, door and stand 0.8 %).

### Firmware build

```bash
cd firmware
. $HOME/esp/esp-idf/export.sh && . $HOME/esp/esp-matter/export.sh
idf.py set-target esp32c6 build
```

Result:

```
v1.3  aircheck.bin 0x19bb30 bytes, 0x444d0 (14 %) free in the 0x1e0000 app slot
      DIRAM 210 208 bytes used (46.5 %)
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

## Before printing

Two parts have no published drawing. Measure them before the case is
printed; if a number differs, change it in `cad/openscad/aircheck_params.scad`
and re-render - the asserts re-check the gas bay.

| # | test | pass |
|---|---|---|
| T-M1 | Grove SHT40 board | outline 40 × 20 mm and parts no taller than 2.5 mm (`SHT_L`, `SHT_W`, `SHT_PARTS`); the fence is built on these |
| T-M2 | Sunrise | body 33.5 × 19.7 × 11.5 mm and the two pin rows 30.48 mm apart (`SR_L`, `SR_W`, `SR_H`, `SR_PIN_PITCH_ROWS`); the rows come from a third-party footprint, not from Senseair's drawing 740-00993 |
| T-M3 | FireBeetle | the JST PH socket is no taller than 4.8 mm (`FB_TOP_PARTS`), the joints and wires underneath fit the 5 mm standoff, the board is hardware v1.2 (36 µA) |
| T-M4 | first print | the checks in `manufacturing/print-settings.md`: lid and door drop in, the SEN62 slides into its cradle, every port and vent slot is open with a clean roof, the SHT40 sits snug in its fence and the Sunrise between its guides, LED holder and button fit their holes |

## Bench tests, before the case is closed

These need hardware and are the first thing to do once you have it. Unless a
test says otherwise, run it on cells with USB unplugged and nothing in J2:
on external power (a computer on the FireBeetle's USB-C, or a charger in J2)
the device switches to CONTINUOUS and VSYS no longer comes from the cells.

### Power

| # | test | pass |
|---|---|---|
| T-P1 | regulators set | **before anything is connected to them** (ASSEMBLY step 4): PS1 reads **3.90 V ± 0.03 V** from the cells through the fuse, with R11 fitted; PS2 reads **4.20 V ± 0.03 V** from a charger in J2. PS2 above 4.23 V puts the FireBeetle out of its rating; PS1 above about 4.04 V can be read as external power |
| T-P2 | **cells never charged** | cells in, an ammeter (or the PPK2) in the pack lead, once with a computer on the FireBeetle's USB and once with a charger in J2: current only ever flows out of the pack, never into it - also while the SEN62 switches and the Sunrise measures. VSYS (FireBeetle BAT) reads about 4.2 V on either and about 3.9 V on the cells alone. Repeat with NiMH cells if you use them |
| T-P3 | **SEN62 switch-on dip** | a scope on the FireBeetle's 3.3 V while the #2810 switches the SEN62 on: the rail stays at or above 3.20 V (NETLIST.md), the ESP32-C6 does not reset, and the SGP40/SHT40 reading taken during the window succeeds. This is the ERC's standing warning; the #2810 has no soft start |
| T-P4 | rails | +3V3_SEN is off between windows and 3.2-3.4 V during one; the #2810's slide switch is in OFF; GPIO18 (Sunrise VDDIO) and GPIO14 (EN) are high only during a Sunrise measurement, and GPIO17/21 are quiet while EN is low; the red LED only flickers during boot (GPIO16 is U0TXD) |
| T-P5 | battery voltage | `diag` shows the AA pack voltage; it agrees with a multimeter across the pack (note the difference - the 1M/220k divider multiplies any ADC error by 5.5); with `cell_type` set, the percentage falls across a day and never climbs back on battery; fresh cells bring it back near 100 |
| T-P6 | USB | a computer on the FireBeetle's USB: the log shows `power: external, cells as backup (VSYS 4.2x V, pack ... V)` and `state CHARGING, mode CONTINUOUS` (the state name is historical - nothing charges), and the SEN62 runs continuously; unplugged, `power: cells` and back to ECO within one battery read (`battery_interval_s`, 5 min by default). Since v1.3.1 the mode follows VSYS, not only an enumerated USB host |
| T-P7 | **mains priority** | cells in, a charger in J2, USB unplugged: VSYS reads about 4.2 V; the current in the pack lead is only in the µA range (the regulator's quiescent current and the resistors across the pack, T-E0), not mA; the log shows `power: external, cells as backup`; a controller reads Power Source `Status` 2 (Standby) |
| T-P8 | **seamless switchover** | as T-P7, then pull the charger during a SEN62 window (fan running): no reset (uptime continues, no boot banner), the window completes, the log shows `power: cells` at the next battery read and Matter `Status` returns to 1 (Active). Plug it back in: `external, cells as backup` again |
| T-P9 | **holder empty on mains** | cells out, a charger in J2: the device runs, the log shows `power: external, no cells`, no battery alarm (no yellow blink, no red blip), Matter `Status` 3 (Unavailable), `BatPresent` false, `BatPercentRemaining` null, `BatReplacementNeeded` false |
| T-P10 | **undervoltage lockout** | a lab supply in place of the pack (through the fuse), J2 empty, USB unplugged. Lower it slowly from 8.7 V: the device switches off between ~5.9 and ~6.3 V; raise it again: it restarts between ~6.8 and ~7.2 V, not before. Nominal 6.1 / 7.0 V from Pololu's EN thresholds (off below 0.7 V, on above 0.8 V) with the internal 100 k and R11 13 k; the tolerance band is the uncertainty of those EN levels. Switched off, the supply still delivers about 60 µA (~55 µA through the EN path, ~5 µA through the pack divider) |

### Firmware and controls

| # | test | pass |
|---|---|---|
| T-B1 | flash and boot | the banner `3D Printing AIR CHECK 1.3.1` appears, no panic, no reset loop |
| T-B2 | status LED | white at boot, all three colours on a press, blue when pairing, readable in daylight in its panel holder |
| T-B3 | button | short, long (3-8 s), calibrate (8-12 s) and very long all do the right thing, and the hold colour matches; over 20 s does nothing |
| T-B4 | service console | with USB in, `aircheck_config.py get` lists the settings, `set name ...` changes the Matter NodeLabel, `diag` shows live values and the health of all four sensors; on battery, the console is not running |
| T-B5 | sleep/wake | the device survives a night; uptime and history are continuous |
| T-B6 | watchdog | hold a sensor line low; the device recovers rather than hanging |
| T-B7 | NVS | reboot; baseline, history, the pack's energy counter and the Sunrise state come back |

### Sensors

| # | test | pass |
|---|---|---|
| T-S1 | SEN62 | the boot log prints `SEN62 <serial>`; the fan runs 60 s an hour in ECO; each window logs `window 60s: PM2.5 ... averaged over ... s`, about 30 s averaged after 30 s discarded; a puff of smoke at the left-wall ports moves PM |
| T-S2 | Sunrise, single measurement | the boot log shows `configuration OK: single mode, 32 samples, ABC on (180 h, 425 ppm)` (on the very first boot `EEPROM configuration updated ... sensor reset` instead, once); `sunrise: CO2 ... ppm (... hPa)` every 5 min in ECO; plausible in a well-aired room (400-600 ppm), clearly up after breathing at the gas-bay vents |
| T-S3 | Sunrise ABC state across power cycles | run for more than 30 min (the state goes to flash every 30 min), then take a cell out and put it back: no EEPROM update at boot, and the readings carry on where they left off. The log does not show the restored state itself - this path is the least verified in the driver |
| T-S4 | altitude / pressure | the boot line reads `site 0 m = 1013 hPa`; after `set altitude_m 520` the CO2 lines show about 952 hPa. Compared with a barometer's station pressure, the difference is weather (about ±2 kPa, ±3 % of the CO2 reading) |
| T-S5 | fresh-air calibration | outdoors, hold the button 8-12 s and let go on cyan: the LED blinks cyan, the log shows `fresh-air CO2 calibration to 425 ppm: 3 min settling first`, three `settling:` lines, `target calibration to 425 ppm: done` and `CO2 calibrated to 425 ppm (it read ...)`, then green for 2 s. A failure is fast red and `NOT confirmed`. `co2 frc 425` on the console does the same |
| T-S6 | SGP40 and SHT40 in the gas bay | no `SGP40 init failed` (the init runs the 0xD400 self test) and no `SHT40 not responding`; the SparkFun PWR LED is dark (jumper cut); `diag` shows temperature and humidity every 10 s; against a reference thermometer beside the closed device, note the offset - there is no offset setting |
| T-S7 | **CO2 over a week** | a week in ECO beside a reference CO2 meter, the room aired daily: agreement within the Sunrise's ±(30 ppm + 3 %) plus the weather term. If it drifts, run a fresh-air calibration (T-S5) and repeat |

### Gas bay, with the lid on

| # | test | pass |
|---|---|---|
| T-G1 | **pen test** | open a felt-tip or whiteboard marker near the gas-bay vents (right side seen from the front, and the front face): the VOC index rises within about a minute. If it does not, the bay is not seeing room air |
| T-G2 | bake-out | after assembly, in a room that does not change, the VOC index settles and stays near 100 instead of drifting over days. A slow drift after closing the case points at unbaked parts or something in the bay that emits (`manufacturing/print-settings.md`) |
| T-G3 | leak check | the bay's walls meet the lid ribs over their full length; the three cables pass over the bay wall where a rib closes over them without lifting it; nothing in the bay is taped, glued or foamed |

### Energy

The battery claim rests on a model (`docs/BATTERY_LIFE.md`). These replace
its least certain lines with measurements. Cells in, USB unplugged, J2 empty,
Thread attached. The PPK2's ammeter mode only works up to 5 V, so it cannot
sit in the 5.4-10.8 V pack lead: put it in the **3.9 V line between PS1's
VOUT and the LM66200's VIN1** (everything the device draws, at 3.9 V), and
measure what hangs on the pack itself separately (T-E0).

| # | test | model | pass |
|---|---|---|---|
| T-E0 | **regulator quiescent**: PS1's 3.9 V output disconnected, a multimeter on its µA range in the pack lead (it then sees the regulator, the 1.22 MΩ pack divider and the 113 kΩ EN path of the lockout) | < 0.2 mA + ~84 µA at 8.7 V (Pololu; divider ~7 µA, EN path ~77 µA) | recorded; above 0.2 mA for the regulator the ECO runtime drops as in BATTERY_LIFE.md "What would change these numbers" |
| T-E1 | **idle floor**: SEN62 off, averaged between two windows (VOC samples, Sunrise shots and Thread polls included) | about 0.85 mW at 3.9 V, i.e. the 3.5 mW at the cells (≈ 0.40 mA at 8.7 V) minus the regulator's quiescent current and the resistors across the pack (T-E0) and the regulator's conversion loss | recorded, and fed back into `model.py`. Well above the model: look for the #2810's slide switch in ON, the SparkFun LED jumper not cut, the FireBeetle's green LED (GPIO15), or a pin back-feeding an unpowered sensor |
| T-E2 | **one full ECO hour**, SEN62 window included | 9.0 mWh at the cells = about 5.5 mWh at 3.9 V plus the regulator's losses and T-E0 | recorded; this is the number the 3.5 months rests on |
| T-E3 | **one SEN62 window** on its own | 5.5 mWh at the cells, about 4.7 mWh at 3.9 V | recorded; replaces the datasheet figure |

If the regulator's quiescent current comes in worse than Pololu's "< 0.2 mA",
a particle window every 2 h recovers it without any hardware change.

## Network tests

| # | test | pass |
|---|---|---|
| T-N1 | commissioning | the QR code scans, the device joins Thread, it appears in Apple Home |
| T-N2 | attributes | PM2.5, PM10, CO2, VOC, temperature, humidity and battery all appear; the battery shows as replaceable, 6 × AA; Power Source `Status` follows T-P7..T-P9 (1 on cells, 2 on mains with cells, 3 on mains without) |
| T-N3 | reporting | a change at the sensor reaches the Home app within about 15 s |
| T-N4 | reconnect | power-cycle the HomePod; the device re-attaches on its own |
| T-N5 | offline | unplug the border router; the device keeps measuring and its history keeps filling |
| T-N6 | automation | an Apple Home threshold automation fires |
| T-N7 | factory reset | 12 s press; the device leaves the fabric and re-advertises |

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
| T-D1 | both commission independently | no pairing step exists between them |
| T-D2 | both appear in Apple Home | as two separate accessories |
| T-D3 | independent names and rooms | renaming one does not touch the other |
| T-D4 | both report | values differ when the air differs |
| T-D5 | no interference | putting them 10 cm apart changes nothing |
| T-D6 | both usable in one automation | the Shortcuts recipes in `docs/SHORTCUTS.md` |
| T-D7 | one offline | removing one does not affect the other |
| T-D8 | identical firmware | the same binary on both, no per-unit build |
