# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/spec/v2.0.0.html).

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
