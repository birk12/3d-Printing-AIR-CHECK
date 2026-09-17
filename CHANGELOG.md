# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer](https://semver.org/spec/v2.0.0.html).

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

- 453 host checks over the measurement core
- 371 electrical rule checks
- OpenSCAD asserts plus an STL checker (manifold, bounding box, overhang)
- full ESP-IDF + esp-matter build for esp32c6

### Known limitations

- no part of this has been verified on assembled hardware
- the sensor drivers have no automated test coverage
- in ECO mode the particle channel samples every four hours
- PM1 is published to Matter but Apple Home has nowhere to show it
- the VOC Index is published in a concentration cluster; the unit is wrong and
  is documented as such everywhere
