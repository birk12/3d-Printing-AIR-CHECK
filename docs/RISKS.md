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

* The SEN62 measures particles from **0.3 um upwards**. Most of what a hot
  plastic extruder emits is *ultrafine* - well below 0.3 um - and the sensor
  cannot see it at all. A print can produce a large number of ultrafine
  particles while PM2.5 barely moves. **A low PM2.5 reading is not evidence
  that a print is clean.**
* PM4 and PM10 are calculated from the measured particle size distribution,
  not measured independently. Their specified precision is much looser:
  25 ug/m3 below 100 ug/m3, against 5 ug/m3 and 5 % for PM1 and PM2.5
  (SEN6x datasheet).
* Calibration is against a TSI DustTrak DRX 8533 with a specific test aerosol.
  Plastic fume is not that aerosol. Absolute values for printer emissions carry
  a systematic uncertainty nobody has quantified.
* In ECO mode the particle channel samples once an hour: a 60 s window, of
  which the first 30 s are discarded while the module settles. Short particle
  events that produce no VOC will be missed. This is a deliberate trade for
  battery life and is explained in `docs/BATTERY_LIFE.md`.

### VOC

* The SGP40 gives a **VOC Index**, 1 to 500, relative to this room's own last
  24 hours. It is not a concentration, it cannot name a chemical, and it
  cannot distinguish styrene from a cup of coffee.
* It re-baselines itself continuously. A room with a permanently raised VOC
  level will still report an index near 100 once the algorithm has settled.
  **The index cannot tell you that a room is persistently bad.**
* The same applies to the device itself: anything in the gas bay that emits -
  unbaked PETG, tape, glue, foam - becomes part of the baseline and blunts the
  index. That is why the bay has none of them and the parts are baked.
* Published to Matter as a number in a ppb field, because Apple Home needs a
  number. The unit is wrong and this is stated everywhere it appears.

### CO2

* The Senseair Sunrise is specified to ±(30 ppm + 3 %). It measures every
  5 minutes in ECO and is switched off in between.
* Its self-calibration (ABC, 180 h period, 425 ppm target) assumes the room
  reaches near-outdoor air **about once a week**. The firmware keeps the ABC
  state in its own flash across power cycles, so switching the sensor off
  does not reset it - but in a room that is never ventilated the reading will
  still drift. Turn `co2_self_calibration` off there and calibrate at fresh
  air instead (`docs/CALIBRATION.md`).
* NDIR reads molecules per volume, 1.6 % per kPa. There is no barometer: the
  pressure comes from the configured `altitude_m`. Weather moves the real
  pressure by about ±2 kPa, which leaves a **±3 % residual** on the reading.
  A wrong `altitude_m` (the default is 0 m) costs 1.6 % per kPa of the
  difference, on top of that.
* A fresh-air calibration done indoors makes every reading afterwards wrong
  by the room's excess CO2.
* CO2 is a proxy for ventilation, nothing more. It says nothing about
  particles or plastic fumes.

### Temperature and humidity

* Measured by an SHT40 inside the case, low in the gas bay next to the vents,
  away from the ESP32 and the Sunrise's lamp. It is still inside a box with
  electronics in it. There is no offset setting yet; until you have compared
  it with a thermometer you trust, treat the temperature as an indicator.

---

## Battery life

* Every number in `docs/BATTERY_LIFE.md` comes from datasheets and from
  vendor-published or independent measurements. **None of it has been
  measured on an assembled device**, because no device has been assembled.
  TESTING T-E1..T-E3 are the measurements that would settle it.
* The least certain line is the Pololu regulator's quiescent current. Pololu
  give only "< 0.2 mA for most combinations", and at that figure it is about
  a fifth of the ECO budget. At 0.4 mA ECO on L91 falls from 3.7 to 3.1
  months; a particle window every 2 h recovers that.
* The second least certain is the FireBeetle's board quiescent current, a
  vendor figure minus the chip (29 uA). At 150 uA ECO falls to 3.5 months.
* The Thread radio figure is an independent PPK2 measurement of an ESP32-C6
  ICD. A different Thread network, a weaker link or more retries all cost
  more than that trace shows.
* The three-month target is met with L91 cells only. eneloop pro and alkaline
  give 2.2 and 2.1 months in ECO, and need a particle window every 2 h to
  clear three months.

---

## Enclosure and airflow

* The EPDM gasket between the SEN62 and the side wall is the single most
  important mechanical detail, and it is the easiest to get wrong. If the
  frames around the inlet and outlet windows are missing or leak, the sensor
  draws air from inside the case and blows its exhaust back in, and **PM
  readings go wrong with no other symptom**. `docs/ASSEMBLY.md` step 7.
* The gas bay (Sunrise, SGP40, SHT40) must see room air through its own
  vents in the right side wall and the front face, and is closed off from the
  rest of the case only by its printed walls meeting ribs on the lid, with no
  gasket (EDR-17). The pen test (TESTING T-G1) checks that the bay sees room
  air; T-G3 checks the walls and ribs.
* The SEN62 must not sit in a forced airflow above 1 m/s. Do not put this
  device inside a printer enclosure or in front of a fan, and keep its left
  side (particle ports) and right side (gas-bay vents) free and out of direct
  sunlight.
* PETG softens around 80 C. This case is for a room, not a heated chamber.
  PLA softens around 55 C and is not for the final build; ABS and ASA keep
  emitting next to the SGP40.
* The sensor ports open to the outside. Dust gets in. The SEN62 keeps its
  optics clean with a sheath flow; the SGP40's membrane has no such thing and
  will eventually accumulate dust.

---

## Battery safety

Since v1.3 the device runs from six AA cells, not a LiPo, because the charger
was the one part of v1.2 that could start a fire (EDR-18).

* **Nothing charges inside the device.** With USB plugged in, the FireBeetle's
  charger holds its battery input at 4.2 V; the LM66200 ideal diode blocks any
  current back towards the regulator once its output is more than 70 mV above
  its input. The cells never see a charge current, not even rechargeable
  ones. The ERC checks the wiring for this; the bench test (TESTING T-P2)
  has to confirm it on the real build.
* **Fault energy is limited.** A PTC fuse (Bourns MF-R050, 1 A trip) sits in
  the holder's red lead, 2 cm from the holder, so a pinched wire or a failed
  module downstream is limited. The design stays within IEC 62368-1 power
  source class PS1 (≤ 15 W) for any fault after the fuse. The MF-R030 is the
  stricter choice for a clear margin at a fresh 10.8 V pack; verify its trip
  time before swapping.
* **The compartment is separate.** A 4 mm partition to the lid, one 6 × 6 mm
  lead notch, its own pressure-relief slots in the bottom wall so a venting
  cell cannot pressurise the case, and a door held by two screws so children
  do not get at the cells.
* **Cells.** Energizer Ultimate Lithium L91 are recommended: primary, no
  charging, no leaking. NiMH are charged outside, in a charger made for them.
  Alkaline cells can leak when left flat: take them out when empty. Never mix
  old and new cells, never mix chemistries. 1.5 V Li-ion AA cells with USB-C
  are **not recommended**: they are Li-ion again, and their regulated 1.5 V
  hides the state of charge.
* Route the leads through the partition notch so the lid and the door cannot
  pinch them, and heat-shrink every joint.
* **Standards.** IEC 62133-2 and UN 38.3 apply to the cells and are the
  manufacturers' job: buy branded cells from a regular shop. The EU Battery
  Regulation 2023/1542 applies to whoever places a battery-powered product on
  the market; a private build is not placed on the market. Selling the device
  would need a CE/EMC/RED assessment (the ESP32-C6 module is RED-certified,
  the device is not). None of this has been assessed by a test lab; it is the
  reasoning behind the design, not a certificate.

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
  detection, history, 24 h statistics, LED logic and battery percentage - is
  covered by 529 host checks that run on every build.
* `ac_hal` - the actual sensor drivers - is **not covered by any automated
  test**. It cannot be, without hardware. Every register address, command
  code and timing in it was taken from a manufacturer datasheet and checked by
  reading, not by running. The least exercised path is the Sunrise's
  host-held ABC state: read after every measurement, written back before the
  next one, kept in flash (TESTING T-S3).
* The event detector's thresholds are engineering judgement. They have not
  been validated against a real printer. Expect to adjust `ev_sensitivity`.
* NVS wear: history, baseline, the pack's energy counter and the Sunrise state
  are flushed every 30 minutes, about 17 000 times a year, across a
  wear-levelled partition. That is well within flash endurance, but it is
  not zero. The Sunrise's own EEPROM settings are only written when they
  differ, normally once.

---

## What this device must never be described as

Not a medical device. Not a safety detector. Not a certified instrument. Not
evidence that any particular material is safe or unsafe to print.

The correct sentence is *"air quality elevated"*, and the reason. Never *"this
air is unsafe"* - the device cannot know that, and the firmware never says it.
