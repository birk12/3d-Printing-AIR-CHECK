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
| measurement core logic | SIMULATED, 537 checks | `firmware/test/host/test_ac_core.c` |
| status LED logic | SIMULATED | part of the 537; every pattern and its timing |
| LFP power module (`ac_power` over `pwr_std`) | SIMULATED | part of the 537, plus `firmware/test/host/test_pwr_std.c`: PWR-K decoding, charge pause, safety-timer restart, voltage levels, Matter's charge states |
| energy model | BUILD-VERIFIED | `tools/battery_calculator/model.py`, inputs traced to datasheets |
| electrical design | BUILD-VERIFIED | 748 rule checks in `electronics/schematic/design.py`, one standing warning (T-P3) |
| enclosure | BUILD-VERIFIED | OpenSCAD asserts over every module, post and zone, and `tools/diagnostics/stl_check.py`: all four parts manifold, 1.2 % overhang on the front shell |
| firmware build | BUILD-VERIFIED | full ESP-IDF v5.5.5 + esp-matter v1.6 build for esp32c6: 1.69 MB image (14 % free in the OTA slot), 210 kB DIRAM (46.5 %) |
| sensor drivers | **untested** | register addresses and timings read from datasheets |
| Matter attribute IDs in DASHBOARD_INTERFACE.md | BUILD-VERIFIED | checked against the Matter SDK's generated `AttributeIds.h` / `ClusterId.h` |
| Thread / Matter / Apple Home | **untested** | no controller has ever seen this device |
| power module C | **untested** | the Power-Standard's inspection protocol below (PS-1.1 .. PS-4.7) has not been run on an AIR CHECK |

## Automated tests

### Measurement core and power module

```bash
cc -std=c99 -Wall -Wextra -O1 -Ifirmware/components/ac_core/include \
   -Ifirmware/components/pwr_std/include \
   firmware/components/ac_core/src/*.c firmware/components/pwr_std/src/pwr_std.c \
   firmware/test/host/test_ac_core.c -lm -o /tmp/ac_test && /tmp/ac_test
cc -std=c99 -Wall -Wextra -Ifirmware/components/pwr_std/include \
   firmware/test/host/test_pwr_std.c firmware/components/pwr_std/src/pwr_std.c \
   -o /tmp/pwr_test && /tmp/pwr_test
```

537 checks in about 10 ms, then the Power-Standard's own `pwr_std` test
(`all tests passed`). What they cover:

| area | examples |
|---|---|
| config | defaults are self-consistent; every profile gives the SEN62 its 30 s start-up plus at least 30 s averaged; the CO2 interval leaves room for a 32-sample Sunrise measurement; the SIT ICD slow poll stays at or under 15 s; a corrupted blob is rejected rather than half-applied; out-of-range values (window, battery interval, altitude) are clamped and counted |
| filters | the EMA reaches 63 % of a step after one time constant and gives the same answer at 6 x 10 s as at 1 x 60 s; least-squares slope recovers a known ramp; spike detection refuses to fire without enough samples |
| baseline | converges on a steady room; a frozen baseline does not learn the event; a reset adopts the current air; an implausible stored baseline is rejected on restore; no baseline means no delta rather than a fake one |
| air quality | the worst channel decides; two channels at the same level are both named; a steep rise escalates GOOD but never further; nothing measured is UNKNOWN, not GOOD |
| events | ordinary room noise never triggers; a single spike arms but does not confirm; a short dip does not end an event; the completed record carries the right peaks and durations; nothing can trigger without a baseline |
| history | a spike survives aggregation in the max field; the ring wraps without corrupting; trends are right for a ramp and for a flat room |
| engine | a cold engine measures immediately but publishes nothing; warm-up ends after the SGP40's documented 60 s; ECO over two hours gives exactly 2 SEN62 windows, 24 CO2 shots and 720 VOC samples; VOC keeps its 10 s grid through a 60 s particle window; an event escalates to ACTIVE; USB switches to continuous, which keeps the SEN62 running between windows; low and critical battery come from the voltage levels, and critical stops measuring; a dead SEN62 does not stop the other channels; SEN62, Sunrise and SGP40 all dead is an ERROR; publishing is driven by change, not by the clock; factory reset restores every default |
| status LED | dark in normal operation; a press shows the air-quality colour for exactly 3 s; pairing blinks blue and times out with the commissioning window; warm-up, low battery, critical battery and fault each have their own pattern; the unprompted critical-battery blip stays under 50 ms per 10 s; holding the button shows blue / cyan / red at 3 / 8 / 12 s and nothing past 20 s; calibration blinks cyan for longer than its 3 min settling, then green or red |
| 24 h statistics | no history means no value, not zero; a one-minute spike survives into the 24 h peak; a 1 h window ending before the spike does not see it; the bucket in progress is included |
| power module (`ac_power`) | on the cells: battery source, charging allowed, not external; a computer on the FireBeetle counts as external for the engine only; USB-C charging → full → charge pause (CE held); the pause is released below 3.30 V; a fault after 5.5 h of charging is the safety timer and gets one CE pulse, only once per USB session, and unplugging starts a new session; an early fault (NTC hot/cold) is not taken for the timer; USB-C without cells reports no battery; no ladder reading is not external and never pulses CE |
| `pwr_std` | the LFP voltage-to-percent table; levels 3.20 / 3.10 V with hysteresis; fresh cells from the charger are recognised; charge, full, pause, release below 3.30 V and after 30 days, no pause without USB; recoverable and latched faults; the timer retry once per USB session, not when full, not for a recoverable fault; the PWR-K band edges |

The suite was checked against a deliberately injected bug (inverting one
air-quality comparison) to confirm it fails when it should.

### Electrical rule check

```bash
python3 electronics/schematic/design.py
```

```
ERC: 748 checks, 0 error(s), 1 warning(s)
  WARN  SW1 has no soft start: the SEN62's switch-on step lands on the FireBeetle's 3.3 V buck - verify on the bench (TESTING T-P3)
```

What the 748 cover: every pin exists on its part and every pin of every part
is connected or declared open, no net has one connection, no pin is on two
nets, every supply is within its part's range and the part really sits on
that rail; **the cell chain** (Power-Standard checklist K1/K2): each cell's +
goes only to its own fuse, the fused cells meet only at the BMS's B+, B−
connects nothing but the cells and the BMS, P− is the system ground, BMS P+
feeds the #6091's BATT+, one fuse per cell and two cells per holder, the NTC
sits between TH and GND; **the power path**: the LM66200 is the only link
between +VREG and VSYS and the FireBeetle charger's 4.2 V is far enough above
+VREG for it to block (the CN3165 never reaches the cells), VIN2 and ON are
at GND, LOAD feeds the regulator and nothing else, the regulator starts at the
BQ25185's lowest LOAD voltage, VSYS keeps the FireBeetle's buck in regulation
and the SEN62's rail at or above 3.15 V over the whole cell range; the
regulator and the #2810 carry their loads with margin and the LOAD peak at
3.0 V stays under 2 A; VBAT_S stays inside the 6 dB ADC range, the PWR-K node
never exceeds 3.6 V, its top threshold lies below the ADC's clipping, the
STAT pins reach it only through their Schottky diodes and "USB present"
clears `pwr_std`'s threshold (read from `pwr_std.h`); I2C addresses are
unique, each switched bus has exactly one pull-up pair on the rail that
powers its devices and the LP bus uses the modules' own, the Sunrise's VDDIO
shares GPIO18 with its pull-ups, COMSEL is grounded and EN has its pull-down,
the LP bus is on GPIO6/7, no strapping pin is used except GPIO4/5 (SDIO
timing only, approved), only an LED sits on GPIO16, the net list, the GPIO
map and the firmware's `AC_PIN_` table agree, and no LED glows with its GPIO
high.

The one warning stands on purpose: the Pololu #2810 has no soft start, and
whether the SEN62's switch-on step upsets the FireBeetle's 3.3 V is a bench
measurement, not a rule (T-P3).

What the ERC cannot catch is a module that differs from what was fed into it
(EDR-11, EDR-14: in both cases a board was modelled from its product page and
only its schematic showed otherwise). Every module is modelled from its
vendor's drawings and board files; the Grove SHT40 and the Sunrise's pin
rows are not published and are measured before printing (T-M1, T-M2). The
#6091's jumper defaults are checked on the board itself (PS-1.1, PS-1.2).

### Enclosure

```bash
for p in front back door stand; do
  openscad -o /tmp/$p.stl cad/openscad/aircheck_case.scad -D "part=\"$p\""
  python3 tools/diagnostics/stl_check.py /tmp/$p.stl --build 250x210x220
done
```

The OpenSCAD model asserts on every render. Each module is a box in the
model (the two holders with their cells and their pins underneath, the NTC
bead, the #6091 and its solder side, the BMS, SEN62 and its plug keep-out,
SHT40, SGP40, Sunrise and its filter clearance, FireBeetle and its solder
side, the #2810, the S9V11E2A, the LM66200, the USB-C socket, LED holder,
button, USB-C plug), and:

* no two of them overlap, every one is inside the case, and each sits in its
  zone - Sunrise, SGP40 and SHT40 in the gas bay, the holders, the #6091 and
  the BMS in the battery compartment, everything else in the electronics
  zone;
* no lid post or door post hits any of them;
* the SEN62 cradle clears the posts, its ports get at least Sensirion's
  minimum open area (108 and 226.8 mm² against 56 and 148), every port row
  lands on the port face below the lap joint;
* the Sunrise keeps 1.0 mm to the walls and 1.5 mm from its filter to the lid
  (ANO4947), the SHT40 is clear of its neighbours, every gas-bay vent is
  inside the bay and the bay has at least 200 mm² open;
* **battery compartment** (Power-Standard checklist E1-E5): there is room over
  the cells for the door's retaining ribs and the NTC, the door posts stay
  inside the compartment and clear of holder 1 and the chamber vents, holder 2
  stays out of the charger chamber, the #6091 is at least 5 mm from every
  wall, the chamber has vents low and high, and the charger is at least 30 mm
  from the SHT40, the SGP40 and the Sunrise (70.7, 54.5 and 92.6 mm);
* the partition core is at least 1.6 mm, the lead notch opens into neither
  the gas bay nor the SEN62, and the lid / door split sits over the
  partition;
* LED, button, both USB-C openings and keyholes stay clear of the gas bay and
  the lid posts; walls are at least four extrusions; no insert pocket breaks
  through the front face; no bridge is longer than 12-13 mm.

The STL checker reports mesh closure, bounding box, mass and overhang area
(front shell 1.2 %, lid 1.0 %, door 1.5 %, stand 0.8 %).

### Firmware build

```bash
cd firmware
. $HOME/esp/esp-idf/export.sh && . $HOME/esp/esp-matter/export.sh
idf.py set-target esp32c6 build
```

Result:

```
v1.4  aircheck.bin 0x19c0e0 bytes, 0x43f20 (14 %) free in the 0x1e0000 app slot
      DIRAM 210 224 bytes used (46.5 %)
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
| T-M4 | first print | the checks in `manufacturing/print-settings.md`: lid and door drop in, the SEN62 slides into its cradle, every port and vent slot is open with a clean roof, the SHT40 sits snug in its fence and the Sunrise between its guides, LED holder and button fit their holes; the two battery holders sit flat on their bosses and pads, the #6091 on its four posts, the charger chamber's vents are open, the door closes with its ribs and NTC finger clear of the cells |

## Bench tests, before the case is closed

These need hardware and are the first thing to do once you have it. Unless a
test says otherwise, run it on the cells with USB unplugged and nothing in
J2: on external power (a computer on the FireBeetle's USB-C, or a charger in
J2) the device switches to CONTINUOUS, and with a computer on the FireBeetle
VSYS comes from the FireBeetle's own charger at 4.2 V.

### Power module C (Power-Standard inspection protocol)

The Power-Standard's `PRUEFPROTOKOLL.md`, sections 1, 2 and 4 (module C),
with AIR CHECK's pins and nets. The numbers match the protocol line by line
(PS-1.1 = 1.1), so a filled-in protocol can be filed with the device.
Instruments: multimeter, a USB-C charger, a 10 Ω / 5 W load resistor, a µA
meter (PPK2 or µCurrent), a lab supply, freeze spray, a 33 kΩ resistor.

**1. The #6091 without cells** (ASSEMBLY step 4):

| # | test | pass |
|---|---|---|
| PS-1.1 | jumpers (photo): VS cut, 3.65V bridged; also 1 Amp bridged and TH cut | yes |
| PS-1.2 | USB-C in J2, **no cells**: voltage at #6091 BATT+ / BATT− | 3.60–3.70 V (may wander briefly) |
| PS-1.3 | the same, if it reads 4.1–4.25 V: **stop**, a jumper is wrong | – |
| PS-1.4 | #6091 LOAD with USB-C, PS1 disconnected | 4.41–4.59 V |
| PS-1.5 | S1/S2 really are STAT1/STAT2: (a) CE open (GPIO5 not driven), no cells → S2 toggles high/low (BQ25185 datasheet §6.3.10); (b) TH jumper open, **33 kΩ** at TH instead of the NTC (= cold) → S1 low, the PWR-K node at GPIO4 reads 0.99–1.34 V. **Never** bridge TH to GND: that is the button function, and holding it leads to the factory mode (SYS off) | as described |
| PS-1.6 | !CE has its pull-down: GPIO5 an input (FireBeetle unpowered) → the #6091's CE pad | < 0.4 V |
| PS-1.6a | CE through a reset: hold the FireBeetle in reset (EN / RST low) with USB-C in J2 and measure the #6091's CE pad | < 0.4 V (charging allowed; nothing on GPIO5 pulls it up) |
| PS-1.7 | DCIN+ carries USB 5 V (R20 taps the PWR-K ladder there) | ≈ 5 V |
| PS-1.8 | the board is labelled **"LFP 3,65 V"** | yes |

**2. The cells** (four in parallel, holders, fuses, BMS):

| # | test | pass |
|---|---|---|
| PS-2.1 | cells: AER18650m2A2, one batch, each voltage after the XTAR MX4 (LiFePO4) | Δ ≤ 20 mV |
| PS-2.2 | the BMS IC reads HY2112, **not DW01** | yes |
| PS-2.3 | polarity at every holder contact measured before the cells go in | yes |
| PS-2.4 | BMS P− is the ground of the whole device, B− goes nowhere else | yes |
| PS-2.5 | the NTC on holder 2's outer cell (next to the charger chamber) under Kapton, held by the door's finger; resistance at room temperature | 9–11 kΩ at 25 °C |
| PS-2.6 | TH jumper cut (visual) | yes |
| PS-2.7 | cold test: freeze spray on the NTC, below 0 °C → FLT_N low (PWR-K node 0.99–1.34 V, log `fault (temperature/OVP)`), charging stops | yes |
| PS-2.8 | charge current into flat cells, ammeter between BMS P+ and #6091 BATT+, PS1 disconnected. With the device running, its own draw comes out of the 1.1 A input limit (about 0.8 A left for the cells, EDR-21) | 1000 mA ± 10 % |
| PS-2.9 | end of charge: cell voltage at the end, CHG_N goes high (log `full`, then `charge paused`) | 3.60–3.70 V |
| PS-2.10 | pull USB-C during a SEN62 window (fan running) | no reset: uptime continues, the window completes, the log shows `power: cells` |
| PS-2.11 | the cells' quiescent current without USB-C, PS1's input disconnected, µA meter in the BMS P+ lead (charger, BMS and the 470k/470k VBAT_S divider) → open points O1/O2 | ≤ 15 µA |
| PS-2.12 | short-circuit test at the BMS output (through 1 Ω, briefly): the BMS switches off | yes |
| PS-2.13 | **thermocouples on the #6091's IC and on holder 2's outer cell** (the one next to the chamber, where the NTC sits), device upright, closed case, 30 min charging at 1 A from half-empty cells → open point O3. Note both temperatures and the NTC cell against holder 1's cells | IC < 70 °C (release for use only then); the NTC's cell is the warmest of the four |

**4. The interface at the FireBeetle** (PWR-K: VBAT_S on GPIO3, the ladder on
GPIO4, CE on GPIO5):

| # | test | pass |
|---|---|---|
| PS-4.1 | GPIO3 (VBAT_S) = half the cell voltage; `diag` (`LFP cells X.XX V`) agrees with a multimeter across BMS P+ / P− | ± 1 % |
| PS-4.2 | GPIO4 (PWR-K) with / without USB-C in J2, cells full | 2.85–3.15 V / 0 V (all four bands: T-L1) |
| PS-4.3 | no wire to a FireBeetle GPIO above 3.6 V against GND; only the battery input (VSYS) is higher | yes |
| PS-4.4 | the FireBeetle's own Li-ion charger (CN3165) is blocked: a computer on its USB-C, cells in, J2 empty, an ammeter in the LM66200's VIN1 lead - VSYS about 4.2 V, +VREG 3.90 V, and no current flows from VSYS back towards PS1 | yes |
| PS-4.5 | what Matter reports is right: source, charging, level | T-L6 |
| PS-4.6 | brown-out: the device runs at the lowest battery voltage (lab supply instead of the cells), radio peaks included | T-P2 |
| PS-4.7 | permanent USB-C, 7 days: count the `charging` lines in the log → open point O6. With the charge pause (T-L3) there is one top-up per release, not several a day | recorded |

### Power, AIR CHECK specific

| # | test | pass |
|---|---|---|
| T-P1 | regulator set | **before the LM66200 is connected** (ASSEMBLY step 4.3): PS1 reads **3.90 V ± 0.03 V** from #6091 LOAD with USB-C in J2 and no cells; later, running, +VREG stays within 3.82–3.98 V on the cells and on USB-C |
| T-P2 | **SEN62 rail at the lowest battery voltage** | a lab supply at **3.0 V** directly on PS1 VIN (the #6091's LOAD disconnected), J2 empty, USB unplugged, a SEN62 window running and Thread attached: +VREG 3.90 V, +3V3_SEN at or above **3.15 V** (the SEN62's minimum), no reset during radio peaks |
| T-P3 | **SEN62 switch-on dip** | a scope on the FireBeetle's 3.3 V while the #2810 switches the SEN62 on: the rail stays at or above 3.20 V (NETLIST.md), the ESP32-C6 does not reset, and the SGP40/SHT40 reading taken during the window succeeds. This is the ERC's standing warning; the #2810 has no soft start |
| T-P4 | rails | +3V3_SEN is off between windows and 3.2-3.4 V during one; the #2810's slide switch is in OFF; GPIO18 (Sunrise VDDIO) and GPIO14 (EN) are high only during a Sunrise measurement, and GPIO17/21 are quiet while EN is low; the red LED only flickers during boot (GPIO16 is U0TXD) |
| T-P5 | computer on the FireBeetle | a computer on the FireBeetle's USB: the log shows `power: cells, ... VSYS 4.2x V` and `state CHARGING, mode CONTINUOUS` (the state means external power, not that the cells charge), no battery alarm, and the SEN62 runs continuously; unplugged, back to ECO within one battery read (`battery_interval_s`, 5 min by default) |
| T-L1 | **PWR-K node voltages** | at GPIO4 against GND: no USB-C **0 V**; USB-C and a fault (PS-1.5b or PS-2.7) **0.99–1.34 V**; charging **2.08–2.35 V**; full or paused **2.85–3.15 V**. Never above 3.15 V |
| T-L2 | **reset with 3.15 V on GPIO4** | GPIO4 held at 3.15 V (lab supply on the PWR-K node): reset and power-cycle the FireBeetle several times - it boots normally every time (banner, no reset loop). GPIO4/5 are strapping pins only for the SDIO slave timing, which is not used |
| T-L3 | **charge pause** | USB-C in J2 until full: the log shows `full`, then `charge paused`; GPIO5 (CE) is driven high, the node sits at 2.85–3.15 V, the device keeps running from USB-C. Released - GPIO5 an input again, CE below 0.4 V through the #6091's pull-down - when USB-C is unplugged, when the cells fall below 3.30 V, or after 30 days. Unplug and replug to see the release; the 3.30 V and 30-day releases are covered by the host tests, on hardware note when they happen |
| T-L4 | **safety-timer restart** | from flat cells, USB-C in J2, the device running: the BQ25185 stops after 6 h; the log shows `safety timer ran out before full: charging restarted once`, one 150 ms pulse on GPIO5, and `charging` again - **no** fault published (`BatReplacementNeeded` stays false, no charge-fault event). A second timer fault in the same USB session stays: `FAULT (timer?)`, Matter `BatReplacementNeeded` true and a `BatChargeFaultChange` event with SafetyTimeout. Unplug and replug: cleared |
| T-L5 | **restart after BUVLO** | a lab supply in place of the BMS at #6091 BATT+ / BATT−, J2 empty, USB unplugged. Lower it slowly from 3.3 V: at 3.0 V the #6091 switches LOAD off and the device goes dark. Raise it again: LOAD returns by 3.15 V, the S9V11E2A starts and the device boots |
| T-L6 | **Matter power sources** | read by a controller (`chip-tool`, DASHBOARD_INTERFACE.md). On the cells: endpoint 0 `Status` 1 (Active), `BatChargeState` 3 (IsNotCharging); USB-C endpoint `Status` 3, `WiredPresent` false. USB-C in J2, charging: endpoint 0 `Status` 2 (Standby), `BatChargeState` 1 (IsCharging); USB-C endpoint `Status` 1 (Active), `WiredPresent` true (a computer on the FireBeetle counts too). Full or paused: `BatChargeState` 2 (IsAtFullCharge). USB-C without cells: endpoint 0 `Status` 3 (Unavailable), `BatPresent` false, `BatPercentRemaining` null, no battery alarm. `BatVoltage` agrees with `diag`. With a lab supply in place of the cells: `BatChargeLevel` 1 below 3.20 V (yellow blink on a press), 2 below 3.10 V (red blip every 10 s, measuring stops) |
| T-L7 | **T/RH offset while charging** | closed case, a reference thermometer/hygrometer beside the device, a room that does not change. 1 h charging at 1 A (cells well below full, log `charging`, `BatChargeState` 1), then 1 h with charging over or USB-C out: record the temperature and humidity difference to the reference in both hours. Nothing is compensated: the result tells how much the dashboard's charging mark matters (EDR-21) |
| T-L8 | **NTC seat with the door closed** | door screwed shut; warm the door over the NTC's cell with a hand (or read TH to GND with the cells out of the charger's circuit) - the resistance must fall below its room-temperature value within a minute. If it does not move, the bead is not on the cell: open the door and put it back in its seat | the resistance follows the warmth |
| T-L9 | **critical cells: deep sleep, no restart loop** | lab supply in place of the pack (behind the BMS), USB-C unplugged. Lower it to 3.08 V: a last report with `BatChargeLevel` = Critical, then `cells critical on battery: deep sleep for 3600 s` in the log and the current drops to the sleep floor. At 3.12 V after the wake-up: `boot check: cells at 3.12 V` and straight back to sleep, no Thread traffic. Raise to 3.30 V or plug in USB-C: after the next wake-up the device starts normally | sleeps, re-checks hourly, never loops |

### Firmware and controls

| # | test | pass |
|---|---|---|
| T-B1 | flash and boot | the banner `3D Printing AIR CHECK 1.4.0` appears, no panic, no reset loop |
| T-B2 | status LED | white at boot, all three colours on a press, blue when pairing, readable in daylight in its panel holder |
| T-B3 | button | short, long (3-8 s), calibrate (8-12 s) and very long all do the right thing, and the hold colour matches; over 20 s does nothing |
| T-B4 | service console | with USB in, `aircheck_config.py get` lists the settings, `set name ...` changes the Matter NodeLabel, `diag` shows live values and the health of all four sensors and the power line (`LFP cells X.XX V, N %, charging, external power yes`); on the cells alone, the console is not running |
| T-B5 | sleep/wake | the device survives a night; uptime and history are continuous |
| T-B6 | watchdog | hold a sensor line low; the device recovers rather than hanging |
| T-B7 | NVS | reboot; baseline, history and the Sunrise state come back. The charge pause is not stored: after a reboot CE starts released (charging allowed) and the firmware learns "full" again |

### Sensors

| # | test | pass |
|---|---|---|
| T-S1 | SEN62 | the boot log prints `SEN62 <serial>`; the fan runs 60 s an hour in ECO; each window logs `window 60s: PM2.5 ... averaged over ... s`, about 30 s averaged after 30 s discarded; a puff of smoke at the left-wall ports moves PM |
| T-S2 | Sunrise, single measurement | the boot log shows `configuration OK: single mode, 32 samples, ABC on (180 h, 425 ppm)` (on the very first boot `EEPROM configuration updated ... sensor reset` instead, once); `sunrise: CO2 ... ppm (... hPa)` every 5 min in ECO; plausible in a well-aired room (400-600 ppm), clearly up after breathing at the gas-bay vents |
| T-S3 | Sunrise ABC state across power cycles | run for more than 30 min (the state goes to flash every 30 min), then power-cycle the device (USB-C out, all four cells out and back in): no EEPROM update at boot, and the readings carry on where they left off. The log does not show the restored state itself - this path is the least verified in the driver |
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
its least certain lines with measurements. Cells in, USB-C unplugged,
nothing on the FireBeetle's USB, Thread attached. Put the PPK2 in the
**3.9 V line between PS1's VOUT and the LM66200's VIN1** (everything the
device draws, at 3.9 V), and a µA meter in the **BMS P+ lead** for what the
cells deliver (T-E0).

| # | test | model | pass |
|---|---|---|---|
| T-E0 | **quiescent at the cells**: PS1's 3.9 V output disconnected, the µA meter in the BMS P+ lead (it then sees the regulator, the charger and the BMS, and the 940 kΩ VBAT_S divider) | < 0.2 mA for the regulator (Pololu), 4 + 3 µA charger and BMS, ~3.5 µA divider | recorded; above 0.2 mA for the regulator the ECO runtime drops as in BATTERY_LIFE.md "What would change these numbers" |
| T-E1 | **idle floor**: SEN62 off, averaged between two windows (VOC samples, Sunrise shots and Thread polls included) | about 2.6 mW at the cells | recorded, and fed back into `model.py`. Well above the model: look for the #2810's slide switch in ON, the SparkFun LED jumper not cut, the FireBeetle's green LED (GPIO15), or a pin back-feeding an unpowered sensor |
| T-E2 | **one full ECO hour**, SEN62 window included | 8.1 mWh at the cells | recorded; this is the number the 2.9 months rest on |
| T-E3 | **one SEN62 window** on its own | 5.5 mWh at the cells | recorded; replaces the datasheet figure |

To compare with the model at the cells: the energy in the 3.9 V line divided
by the regulator's efficiency (85 % in the model, not measured at this
operating point), plus T-E0.

If the regulator's quiescent current comes in worse than Pololu's "< 0.2 mA",
a particle window every 2 h recovers it without any hardware change.

## Network tests

| # | test | pass |
|---|---|---|
| T-N1 | commissioning | the QR code scans, the device joins Thread, it appears in Apple Home |
| T-N2 | attributes | PM2.5, PM10, CO2, VOC, temperature, humidity and battery all appear; the battery shows as rechargeable and replaceable (4 × AER18650m2A2 LiFePO4); both Power Sources follow T-L6 |
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
