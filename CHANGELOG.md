# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/spec/v2.0.0.html).

## [1.4.4] - 2026-09-20

### Changed

- `pwr_std` taken over again: its PWR-K comment now carries **both** ladders
  with their bands, the shift between them and the open point (TI specify
  VOL at 5 mA, not at the 0.28 mA that actually flows). Byte-identical to
  the Power-Standard's copy, as always.
- **Swept the repository for the old wording** rather than only writing the
  new one in - the Power-Standard's new checklist point K18, after a
  correction of its own ended up contradicting itself in the same file. Out:
  the 748 rule checks in `ci/README.md` and `electronics/pcb/README.md`, the
  "one standing warning" that C3 removed, 1.4.0 in `firmware/README.md`,
  the old band 2.85-3.15 V in PS-4.2 and T-L3, the 150k/33k of the old
  ladder in the SP-1 exporter, and the 60 k source impedance in D22's own
  reason for existing.

## [1.4.3] - 2026-09-20

A correction to the reasoning behind 1.4.2, not to the circuit.

### Changed

- **The PWR-K bands are not identical between the two ladders.** "Only
  ratios, so nothing moves" holds for the resistors, not for the diodes: ten
  times the current lifts the BAT43's forward drop from about 0.13 V to
  0.28 V, so the fault bands move up by up to 60 mV and the idle band down.
  From the audit's Monte-Carlo for the 10k ladder (3000 runs per state):
  fault **1.05-1.54 V**, charging **2.09-2.43 V**, idle **2.84-3.17 V**.
  The thresholds hold (0 misdecodings in 15000 runs), but the smallest
  margin is now 149 mV at the idle band's lower edge against 2.65 V, where
  it used to be 182 mV at the fault band. The ERC checks that margin
  explicitly (at least 100 mV, ADC error included), and the numbers in
  TESTING, NETLIST and SP-1 follow. T-L1's "never above 3.15 V" becomes
  "never above 3.20 V".

## [1.4.2] - 2026-09-20

Follow-up to the audit, all of it from the two cross-project sessions: the
PWR-K ladder gets ten times lower impedance, the calibration moves into the
Power-Standard, and C3's rule is now K17. No change to the topology - same
nets, same parts, four different resistor values.

### Changed

- **PWR-K ladder 10k / 15k / 15k / 3k3** instead of 100k / 150k / 150k / 33k
  (the standard's new dimensioning, audit observation O2). Only ratios set
  the bands, so they stay the same to three digits and `PWR_K_CHG_MIN` stays
  at 1.85 V - but the source impedance falls from 60 k to 6 k, so a
  microamp of diode leakage lifts the node by 6 mV instead of 60. The
  ladder hangs on VBUS and draws nothing from the cells; the STAT pins sink
  0.28 mA, far below the 5 mA the 0.4 V VOL figure is given for. D22 stays
  as the backstop (K16: mandatory with the 100k ladder, recommended with
  this one). T-L1b now expects **below 3.20 V** instead of 3.45 V.
- **Calibration arithmetic taken from `pwr_std`** (`pwr_cal_factor`,
  `pwr_cal_apply`, protocol step 4.1b). More than 5 % off is no longer
  clamped but refused with `PWR_CAL_OUT_OF_RANGE`: at that distance it is
  not the ADC but a wrong or badly soldered divider, and half a correction
  would hide it. `cal battery` says so and stores nothing.
- The ERC checks the clamp the way it actually works - rail-referenced, at
  least 100 mV of margin at every operating point, not only at the nominal
  rail (the audit's precision).

## [1.4.1] - 2026-09-20

The electronics audit (PA-01) went over all four household projects and
blocked every release until each hands in a machine-checkable schematic.
Ours is generated from `design.py`, was handed in and is accepted. Along the
way four findings against AIR CHECK and two cross-project corrections.

### Added

- **C3, 470 uF at SW1's VIN** (audit NC-05): Sensirion specify no input
  capacitance for the SEN62 anywhere, so the rail now carries the switch-on
  step for any value up to **33 uF**, which T-P3 measures before assembly.
  The ERC computes the dip (3.03 V at the limit, against a 3.00 V VDD
  minimum) instead of carrying a warning.
- **D22, a third BAT43** from the PWR-K node to +3V3 (Power-Standard rule
  K16, audit observation O2): with both STAT pins high, D20/D21 are reverse
  biased next to the warm charger and their leakage lifts GPIO4 by 60 mV per
  microamp. The clamp holds it under VDD + 0.3 V.
- **One-point calibration of the cell measurement** (audit NC-03):
  `cal battery <V>` stores a factor per device, which takes out the divider's
  1 % and most of the ADC's +-23 mV. `docs/CALIBRATION.md`, acceptance T-L1c.
  The arithmetic moved into the Power-Standard on the same day
  (`pwr_cal_factor` / `pwr_cal_apply`, protocol step 4.1b); this side keeps
  the console command and the NVS key.
- `tools/audit/sp1_export.py`: turns `design.py` into the audit's submission
  (`Elektronik-Audit/einreichung/SP-1_air-check.toml`), so the two cannot
  drift apart. `cad/render.sh`: every drawing's camera, in the repository
  instead of in a shell history.
- New tests: T-P3a (3.3 V stable with 470 uF, TI's tested combinations stop
  at 2 x 22 uF), T-L1b (PWR-K node with a warm charger), T-L1c.

### Changed

- **PWR-K bands** follow the Power-Standard's correction (audit NC-02): the
  STAT pins' own VOL was missing, so fault is 1.02-1.60 V, charging
  2.09-2.46 V and `PWR_K_CHG_MIN` moves 1.70 V -> 1.85 V. `pwr_std` taken
  over unchanged; the ERC now reads the thresholds out of the header.
- **ERC** counts the resistors' tolerance at both ADC dividers (NC-04:
  1894 mV instead of the nominal 1875 mV against the 1900 mV range) and
  checks the PWR-K node against the pad's limit. 777 checks, 0 warnings.
- ECO on the cells 2.9 -> **2.8 months**: C3 leaks up to 30 uA.
- Drawings re-rendered, `back_view.png` added.

### Note

**Nothing is ordered, printed or flashed.** The battery session's release of
2026-09-19 is suspended by the user's decision until the audit's other
documents are through.

## [1.4.0] - 2026-09-19

User decision: the power system moves to the cross-project Power-Standard's
**module C** - LiFePO4, charged in the device over USB-C (EDR-21). Replaces
EDR-18 and EDR-20.

### Changed

- **Power:** USB-C socket → Adafruit #6091 (TI BQ25185, LFP 3.65 V, 1 A, NTC,
  6 h timer, power path) ⇄ **1S4P Lithium Werks AER18650m2A2** in two Keystone
  1049 holders, a PICO II 2 A fuse per cell, HY2112 BMS → the Pololu
  S9V11E2A (3.90 V) → LM66200 → FireBeetle. AA holder, PTC, second regulator
  and the 13 kΩ lockout removed.
- **Charge pause** on permanent USB-C (CE held after full; released on
  unplug, below 3.30 V or after 30 days), **one safety-timer restart** per
  USB session (1S4P needs 8-9 h from flat).
- Low/critical battery from the **cell voltage** (3.20 V / 3.10 V), not from a
  percentage. Settings `cell_type`, `cells`, `low_battery_pct`,
  `critical_battery_pct` removed (config version 5).
- **Matter:** battery on endpoint 0 is Rechargeable + Replaceable (LiFePO4,
  18650, 4 cells) with `BatChargeState`; new endpoint "USB-C" (Wired, DC).
- Case **136 × 174 × 34 mm**: battery compartment with a vented charger
  chamber (≥ 30 mm from the gas bay's sensors), cell-retaining ribs and an
  NTC finger on the door, engraved battery label.
- ECO on the cells: 2.9 months with margin. BOM about EUR 200.

### Added

- `components/pwr_std` (the Power-Standard's C99 module, unchanged),
  `ac_core/ac_power` (PWR-K decoding, timer detection, charge-pause decision),
  `docs/NUTZUNG.md` (user guide, German).
- ERC checks for the cell chain (fuse per cell, BMS, P− = GND), the SEN62
  rail over the whole battery range, GPIO levels and the PWR-K thresholds;
  OpenSCAD checks for the charger's distance to walls and sensors.

## [1.3.1] - 2026-09-19

### Added

- **Continuous operation from any USB-C charger** (EDR-20): a USB-C power
  socket (Adafruit #6050) on the top, a second Pololu S9V11E2A at 4.20 V into
  the LM66200's second input. External power always wins; cells left in are
  the backup, switched in without a gap (L91 about 15 months as backup). The
  holder may also stay empty.
- **Undervoltage lockout** at 1.0 V per cell (13 kΩ on the cells' regulator's
  EN): no cell reversal, no leaking alkalines. Restarts only with fresh cells.
- Firmware recognises external power from VSYS (≥ 4.08 V) and an empty
  holder; CONTINUOUS mode and no battery alarms on external power. Matter
  Power Source `Status` 1/2/3 and `BatPresent`.
- `ac_power_classify()` in `ac_battery` (plain C, reusable by other projects).

### Changed

- The cells' regulator is trimmed to 3.90 V (was 4.00 V).
- ECO on L91: 3.5 months with margin (was 3.7): the lockout's resistors cost
  about 8 %. Alkaline cells now count only down to 1.0 V per cell.
- BOM EUR 171.

## [1.3.0] - 2026-09-19

Measurements you can trust, a battery you cannot overcharge, and no circuit
board to make. About EUR 160 in parts including the first set of cells.

### Changed

- **CO2 from a Senseair Sunrise** (NDIR, ±(30 ppm + 3 %)) every 5 minutes
  instead of the SEN63C's hourly reading. The SEN63C's CO2 is only specified
  in continuous operation, and its self-calibration never saves its state when
  the module is power-cycled hourly (EDR-16). The Sunrise runs in
  single-measurement mode with EN shutdown; the firmware keeps its ABC and
  filter state across power cycles and adds the ABC hours itself. Pressure
  compensation from the configured altitude.
- **SEN62** replaces the SEN63C: particles only, 60 s windows with the first
  30 s discarded (Sensirion's settling time), switched off by a Pololu #2810.
- **Temperature and humidity from an SHT40** every 10 s (EDR-17), which also
  compensates the SGP40 with a fresh value every sample.
- **SGP40 always on** (SparkFun board, no regulator, LED jumper cut). VOC
  keeps its exact 10 s grid while the SEN62 or the Sunrise are measuring.
- **6 × AA instead of a LiPo** (EDR-18): Pololu S9V11E2A at 4.0 V, LM66200
  ideal diode, PTC fuse. Nothing charges inside the device - the FireBeetle's
  charger is blocked by the ideal diode. Energizer L91 recommended; NiMH and
  alkaline work. Battery % per chemistry, fresh cells detected automatically.
- **No custom PCB** (EDR-19): modules and ten inline resistors.
- **Case 130 × 146 × 34 mm** with three zones: a battery compartment with its
  own screwed door and pressure-relief slots, a walled gas bay (Sunrise,
  SGP40, SHT40) with side and front vents and **no foam, tape or glue**, and
  the electronics. Printed parts are baked out before assembly.
- Sunrise bus on GPIO17/21, red LED on GPIO16 (the ROM boot log toggles
  U0TXD).
- ECO: **3.7 months** with margin on L91 (3.1 worst case), 2.2 on eneloop
  pro, 2.1 on alkaline. The model now works in mWh per chemistry and counts
  the regulator's quiescent current.
- Matter Power Source: Battery + Replaceable, 6 × AA, BatReplacementNeeded.

### Added

- Settings `cell_type`, `cells`, `altitude_m`.
- ERC checks that the cells can never be charged, that every inline part is
  where the netlist says, and that the firmware's pin table matches the wiring.
- OpenSCAD collision checks over every module, post and zone.

### Removed

- The ACC-1 carrier board, the LiPo, the battery cover, the button cap,
  `ECO_LONG`.

## [1.2.0] - 2026-09-18

Cost-down: about EUR 95 in parts instead of EUR 173, with every measurement
kept. Still one hardware design, identical for every unit.

### Changed

- **Sensirion SEN63C** replaces the SPS30, the SCD41 breakout and the 5 V
  boost converter: PM1/2.5/4/10, CO2, temperature and humidity from one
  3.3 V module, measured in one window (EDR-15). CO2 accuracy is now
  ±(100 ppm + 10 %) instead of ±(50 ppm + 5 %), and CO2 follows the
  particle cadence (hourly in ECO).
- **DFRobot FireBeetle 2 ESP32-C6** replaces the Adafruit Feather (EUR 7.50
  instead of about EUR 22; 540 mA charger, ~8 h to charge). Battery level from
  the cell voltage - there is no fuel gauge any more.
- **SGP40 rail pulsed per sample** (0.25 s every 10 s), because the breakout's
  own LDO and power LED draw ~185 µA (EDR-14).
- **Case 112 x 102 x 32 mm**: a 28 mm front shell and a 4 mm lid. The SEN63C
  stands on its side with its ports gasketed to slots in the left wall, as
  Sensirion's SEN6x design-in guide requires. Battery on the lid under a
  clip-on cover; keyholes as a vertical pair.
- Carrier ACC-1 rev C: two load switches with CT capacitors and QOD tied to
  VOUT, a USB sense divider, battery pass-through for the LED anode; 460 rule
  checks.
- ECO: **3.2 months** with margin (2.7 at the SEN63C's worst-case current).
- No weekly fan cleaning (the SEN6x does not need it).

### Added

- **Fresh-air CO2 calibration**: hold the button 8-12 s outdoors. While the
  button is held, the LED shows what letting go would do.
- **Service console on USB** (`ac_console.cpp`), only while USB power is
  present. `tools/configuration/aircheck_config.py` was written against a
  console that did not exist until now; it works, and gained `frc`.
- `ac_core/ac_battery`: state of charge from the resting cell voltage, never
  climbing back on battery. 509 host checks.
- Drawings: `open_view.png`, `front_iso.png`, `side_view.png`.

### Fixed

- **v1.1's battery claim was too high.** Its model missed the LDO and power
  LED on both Adafruit breakouts and took the TPS22918 as 1.1 µA when on
  (8.3 µA per TI). Real ECO would have been about 2.3 months, not 3.0
  (EDR-14).
- TPS22918 pin 5 is QOD, not a second VOUT.
- The docs described a firmware CO2 self calibration (EDR-4) that was never
  implemented. Corrected; the flag now controls the SEN63C's own ASC.
- The configuration tool listed a v1.0 display setting that no longer exists.

## [1.1.0] - 2026-09-18

The sensor becomes a headless, three-month device that feeds a separate
e-ink dashboard. Same single hardware design, still identical for every unit.

### Changed

- **No display on the sensor.** Numbers live in Apple Home and on a separate
  e-ink dashboard that reads the sensors over Matter multi-admin, locally,
  through the HomePod mini (EDR-12).
- **Three-month target instead of six**, spent on hourly particle readings in
  ECO instead of four-hourly. ECO: 3.0 months with a 25 % margin, including a
  dashboard read every 15 minutes. The six-month setting stays available as
  `ECO_LONG`.
- **Enclosure 98 x 102 x 32 mm** (was 122 x 114 x 32): the cell stands
  upright in the back layer under a printed cover; four corner screws.
- Button moved from GPIO6 to GPIO1. One RGB status LED (Adafruit 159) shines
  through a 0.6 mm skin of the front face, with no hole.
- The energy model uses an independent PPK2 measurement of an ESP32-C6 Matter
  ICD at a 15 s poll (121.9 µA) instead of the lower figure derived from
  Espressif's 5 s trace (EDR-13).

### Added

- `docs/DASHBOARD_INTERFACE.md`: pairing via multi-admin, every attribute
  with its ID (checked against the Matter SDK headers), timing, and what a
  read costs the sensor's battery.
- 24 h PeakMeasuredValue and AverageMeasuredValue on PM2.5, PM10, CO2 and VOC.
- The configured name is published as Basic Information / NodeLabel.
- Identify blinks the LED white.
- State, mode and air-quality changes are logged, since there is no screen.

### Fixed

- **I2C could not have worked in v1.0.** On the Adafruit ESP32-C6 Feather, the
  I2C pull-ups and a WS2812B share the LDO that GPIO20 switches; v1.0 switched
  it off. The gas sensors now have their own LP_I2C bus (GPIO6/7) with pull-ups
  on the switched sensor rail. GPIO20 is only pulsed for battery reads (EDR-11).
- The engraved wordmark was mirrored on the part: the model's X axis runs
  right to left seen from the front. Text is now mirrored in the model, and
  the docs say which side is which.

### Removed

- e-paper display, its load switch, driver, fonts and screen renderer.

### Verification

- 466 host checks (was 455), including the LED logic and 24 h statistics
- 344 electrical rule checks, now including one pull-up pair per bus on the
  rail that powers it, and the C6's fixed LP_I2C pads
- firmware: 1.65 MB (16 % OTA headroom), 46.6 % of RAM
- still nothing verified on assembled hardware

## [1.0.0] - 2026-09-17

First release. V1 is the final hardware design: one device, built as many
times as you want places to measure.

### Hardware

- ESP32-C6 (Adafruit Feather) with Thread, USB-C, a MCP73831 charger and a
  MAX17048 fuel gauge
- Sensirion SPS30 over UART for PM1/PM2.5/PM4/PM10 and number concentration
- Sensirion SGP40 for the VOC Index, SCD41 for CO2, temperature and humidity
- 1.54 in 200 x 200 e-paper, six screens plus four status screens
- one multifunction button
- three load switches so the SPS30, the gas sensors and the display can all be
  switched off independently
- 1S LiPo 4000 mAh, user replaceable

### Firmware

- platform-independent measurement core: config, filters, baseline, air
  quality classification, event detection, history, scheduling, display
- ESP-IDF HAL: SPS30 (SHDLC over UART), SGP40, SCD41, SSD1681, MAX17048,
  button, NVS
- Matter: Air Quality Sensor, Temperature Sensor, Humidity Sensor and Power
  Source endpoints, all standard device types
- Thread Sleepy End Device, Matter SIT ICD at a 15 s poll
- adaptive power management: ECO / NORMAL / ACTIVE / POST_PRINT / CONTINUOUS,
  escalating by itself on a detected emission event
- persistent baseline that freezes during events
- 24 h and 7 d aggregated history with min/mean/max so spikes survive
- rolling event log in flash
- factory reset, configuration, diagnostics

### Mechanical

- 122 x 114 x 32 mm two-part PETG enclosure
- sealed split duct under the SPS30, built to Sensirion's mechanical
  guidelines: ports down, inlet and outlet separated, sensor below the heat
- gas sensor bay walled off from the particle duct and from the electronics
- battery outside the air path
- wall mount and desk stand

### Verification

- 455 host checks over the measurement core
- 371 electrical rule checks
- OpenSCAD asserts plus an STL checker (manifold, bounding box, overhang)
- full ESP-IDF v5.5.5 + esp-matter v1.6 build for esp32c6 (1.69 MB image,
  14 % free in the OTA slot, 49.7 % of RAM), which found five bugs the host
  tests could not

### Known limitations

- no part of this has been verified on assembled hardware
- the sensor drivers have no automated test coverage
- in ECO mode the particle channel samples every four hours
- PM1 is published to Matter but Apple Home has nowhere to show it
- the VOC Index is published in a concentration cluster; the unit is wrong and
  is documented as such everywhere
