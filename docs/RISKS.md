# Risks and limitations

Read this before you trust a number from this device.

## The one-line version

This is a **comparative** indoor air monitor. It is good at telling you that
the air near your printer changed, and by how much, relative to how that
room normally is. It is not a reference instrument, not a safety device, and
not a health assessment.

---

## Measurement

### Particulate matter

* The SPS30 measures particles from **0.3 um upwards**. Most of what a hot
  plastic extruder emits is *ultrafine* - well below 0.3 um - and the sensor
  cannot see it at all. A print can produce a large number of ultrafine
  particles while PM2.5 barely moves. **A low PM2.5 reading is not evidence
  that a print is clean.**
* PM4 and PM10 are calculated from the measured particle size distribution,
  not measured independently. Their specified precision is much looser:
  +-25 ug/m3 below 100 ug/m3, against +-(5 ug/m3 + 5 %) for PM2.5.
* Calibration is against a TSI DustTrak DRX 8533 with a specific test aerosol.
  Plastic fume is not that aerosol. Absolute values for printer emissions carry
  a systematic uncertainty nobody has quantified.
* In ECO mode the particle channel samples once an hour. Short particle
  events that produce no VOC will be missed. This is a deliberate trade for
  battery life and is explained in `docs/BATTERY_LIFE.md`.

### VOC

* The SGP40 gives a **VOC Index**, 1 to 500, relative to this room's own last
  24 hours. It is not a concentration, it cannot name a chemical, and it
  cannot distinguish styrene from a cup of coffee.
* It re-baselines itself continuously. A room with a permanently raised VOC
  level will still report an index near 100 once the algorithm has settled.
  **The index cannot tell you that a room is persistently bad.**
* Published to Matter as a number in a ppb field, because Apple Home needs a
  number. The unit is wrong and this is stated everywhere it appears.

### CO2

* Power-cycled single-shot operation disables the sensor's own automatic self
  calibration. The firmware implements a substitute that assumes the room
  reaches near-outdoor air roughly weekly. In a room that is never ventilated
  the CO2 reading will drift low. See `docs/CALIBRATION.md`.
* CO2 is a proxy for ventilation, nothing more. It says nothing about
  particles or plastic fumes.

### Temperature and humidity

* Measured inside a sealed box by a sensor that heats itself. The offset is a
  configuration item and the default is a guess. Until you measure it, treat
  the temperature as an indicator.

---

## Battery life

* Every number in `docs/BATTERY_LIFE.md` comes from datasheets and from
  vendor-published measurements. **None of it has been measured on an
  assembled device**, because no device has been assembled.
* The least certain line is the carrier board's quiescent current, estimated
  at 61 uA. If the real board is at 150 uA, ECO falls from 3.0 months to about
  2.8. Setting particles to every 2 h recovers that with room to spare.
* The second least certain is the Thread radio, taken from Espressif's own
  ICD example. A different Thread network, a weaker link or more retries all
  cost more than the trace shows.
* LiPo capacity falls with age and with cold. A cell at 0 degC delivers well
  under its rated capacity. The three-month figure is for a room at 20 degC with
  a healthy cell.

---

## Enclosure and airflow

* The duct seal is the single most important mechanical detail, and it is the
  easiest to get wrong. If the foam strip is missing or the printed rib is
  under-extruded, exhaust air recirculates into the inlet and **PM readings
  read low with no other symptom**. `docs/ASSEMBLY.md` has the light test.
* The SPS30 must not sit in a forced airflow above 1 m/s. Do not put this
  device inside a printer enclosure or in front of a fan.
* PETG softens around 80 C. This case is for a room, not a heated chamber.
* The sensor bay opens to the outside. Dust gets in. The SPS30 cleans its own
  fan weekly; the gas sensors do not have that option and will eventually
  accumulate dust on their membranes.

---

## Battery safety

* A single-cell LiPo inside a sealed plastic box, near a machine that runs
  unattended for hours, close to a heat source.
* **Use a cell with an integrated protection circuit.** Overcharge,
  over-discharge, overcurrent and short-circuit protection. The Feather's
  MCP73831 provides charge control, not cell protection.
* Route the battery lead so it cannot be pinched by the closing shells. This
  is the most likely failure mode in this whole build.
* Do not charge unattended for the first few cycles.
* The battery bay is vented to the outside at the top, which will not save a
  cell in thermal runaway but does stop pressure building up.

---

## Network

* No Apple Thread border router, no Apple Home. The device has no Wi-Fi in the
  build and no fallback.
* As a Matter ICD with a 15 s poll, the device is slow to respond by design.
* Matter has no store-and-forward. Readings taken while the network is down do
  not appear in Apple Home afterwards. They are in the device's own history.
* Apple changes what the Home app surfaces between iOS releases. Everything in
  `docs/APPLE_HOME.md` should be verified against your own setup.

---

## Firmware

* `ac_core` - the scheduling, filtering, baseline, classification, event
  detection, history, 24 h statistics and LED logic - is covered by 466 host checks that run on
  every build.
* `ac_hal` - the actual sensor drivers - is **not covered by any automated
  test**. It cannot be, without hardware. Every register address, command
  code and timing in it was taken from a manufacturer datasheet and checked by
  reading, not by running.
* The event detector's thresholds are engineering judgement. They have not
  been validated against a real printer. Expect to adjust `ev_sensitivity`.
* NVS wear: history is flushed every 30 minutes, which is about 17 000 writes
  a year across a wear-levelled partition. That is well within flash
  endurance, but it is not zero.

---

## What this device must never be described as

Not a medical device. Not a safety detector. Not a certified instrument. Not
evidence that any particular material is safe or unsafe to print.

The correct sentence is *"air quality elevated"*, and the reason. Never *"this
air is unsafe"* - the device cannot know that, and the firmware never says it.
