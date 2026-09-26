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
  battery life and is explained in `docs/BATTERY_LIFE.md`. On a USB-C charger
  the device runs CONTINUOUS and this limitation does not apply.

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
* **While the cells charge**, the #6091 is a linear charger and turns
  0.85–1.7 W into heat in its chamber. It sits as far from the gas bay as the
  case allows (54–93 mm from the SHT40, SGP40 and Sunrise), but temperature
  and humidity can still read a little high then. Nothing is compensated -
  there is no measured offset yet (TESTING T-L7). Matter's `BatChargeState`
  = IsCharging marks the window, and the dashboard marks those values. On
  permanent USB-C the charge pause limits this to a top-up about once a
  month.

---

## Battery life

* Every number in `docs/BATTERY_LIFE.md` comes from datasheets and from
  vendor-published or independent measurements. **None of it has been
  measured on an assembled device**, because no device has been assembled.
  TESTING T-E0..T-E3 are the measurements that would settle it.
* On USB-C the runtime is unlimited; the numbers below are for the cells
  alone (ECO, 2.8 months with a 25 % margin, 3.5 nominal, 2.4 in the worst
  case). The battery session's rougher estimate for general use is about 67
  days.
* The least certain line is the cells' self-discharge: 3 %/month is an
  assumption, there is no manufacturer figure for the AER18650m2A2
  (Power-Standard open point O5). At 1.5 %/month ECO reaches 3.0 months, at
  5 % 2.7.
* The Pololu regulator's quiescent current: Pololu give only "< 0.2 mA for
  most combinations", about 8 % of the ECO budget at that figure. At 0.4 mA
  ECO falls to 2.7 months; a particle window every 2 h (4.3 months) recovers
  that. Its efficiency when boosting the cells' 3.25 V to 3.90 V at light
  load is not published; the model assumes 85 %, the worst case 80 %.
* The FireBeetle's board quiescent current is a vendor figure minus the chip
  (29 uA). At 150 uA ECO falls to 2.7 months.
* The eremit BMS board's own quiescent current is unverified (Power-Standard
  open point O1); the model uses the HY2112's 3 uA.
* The #6091's green VSYSOK LED is not in the budget because **#6091-R3 is
  desoldered** (EDR-24). Left on, it draws 0.50–1.45 mA from the cells and
  ECO falls to 2.4 months or 7.8 weeks with margin, depending on the LED's
  undocumented forward voltage. ASSEMBLY 4.2 / TESTING PS-1.9 (green LED
  dark with USB-C) and PS-2.11 (≤ 15 µA at the cells) catch it.
* The Thread radio figure is an independent PPK2 measurement of an ESP32-C6
  ICD. A different Thread network, a weaker link or more retries all cost
  more than that trace shows.

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
  PLA softens around 55 C and is not for the final build - and never near the
  cells or the charger; ABS and ASA keep emitting next to the SGP40.
* The sensor ports open to the outside. Dust gets in. The SEN62 keeps its
  optics clean with a sheath flow; the SGP40's membrane has no such thing and
  will eventually accumulate dust.

---

## Battery safety

Since v1.4 the cells are charged inside the device: four LiFePO4 cells, the
Power-Standard's module C (EDR-21). The user guide - charging, what the
displays mean, what to do with a hot or swollen cell, storage, transport and
disposal - is [`NUTZUNG.md`](NUTZUNG.md) (German).

**Unattended charging and permanent operation on USB-C are allowed** for
module C (Power-Standard rule 6a); that is what the layers below are for. The
exception: a damaged or hot device or a deformed cell is not charged
(NUTZUNG.md §4).

* **Chemistry.** Lithium Werks AER18650m2A2, LiFePO4 3.2 V: no
  thermal-runaway chemistry. One type, one batch, charged together in an XTAR
  MX4 (LiFePO4) before the first fit, at most 20 mV apart; all four are
  replaced together, never one. No other cells - no 3.7 V Li-ion 18650, no AA.
* **Charge voltage 3.65 V, set by resistor.** The #6091's VS jumper selects
  it; it cannot fall back to 4.2 V in software. The board ships at 4.2 V, so
  it is measured without a cell before the first cell goes in (TESTING
  PS-1.2/1.3) and labelled "LFP 3,65 V".
* **Safety timer.** The BQ25185 stops charging after 6 h. 1S4P needs 8–9 h
  from flat, so the firmware restarts the timer once per USB session (one CE
  pulse, at most 12 h of charging) without reporting a fault; a second timer
  fault stays and is reported (`BatReplacementNeeded`, `BatChargeFaultChange`
  event). PWR-K cannot tell the timer from a
  recoverable fault; a fault after at least 5.5 h of charging is taken for the
  timer, and a CE pulse during a real NTC or overvoltage fault is harmless -
  the charger stays paused by its own logic.
* **Temperature.** An NTC (Semitec 103AT-2) on a cell in the middle of the
  pack, under Kapton, held by a finger on the door: the charger only charges
  between 0 and 60 °C at the cell. The #6091's TH jumper is cut for it - with
  it closed there is no temperature protection at all. Charge only at 0–45 °C
  room temperature; not on a radiator, not in full sun, not covered.
* **BMS.** An HY2112 board (not DW01, which cuts only at ≥ 4.25 V): over-charge
  3.75 V, over-discharge 2.1 V, over-current. It switches the minus side, so
  its P− is the ground of the whole device and B− goes nowhere else (ERC).
* **A fuse per cell.** A Littelfuse PICO II 2 A at each cell's + contact: a
  shorted cell cannot be fed by its three neighbours.
* **Undervoltage.** At 3.0 V the BQ25185 switches the device off (BUVLO)
  long before the BMS's 2.1 V; USB-C or cells back at 3.15 V restart it.
  Low (3.20 V) and critical (3.10 V, measuring stops) come first.
* **The FireBeetle's own charger never reaches the cells.** Its CN3165 holds
  its battery input at 4.2 V when a computer is on its USB-C; the LM66200
  blocks any current back towards the regulator once its output is more than
  70 mV above that input. The ERC checks the wiring; TESTING PS-4.4 confirms
  it on the build.
* **The compartment is separate.** A 4 mm partition to the lid, one lead
  notch, its own pressure-relief slots in the bottom wall so a venting cell
  cannot pressurise the case. The #6091 sits in its own chamber at the end of
  the compartment, at least 5 mm from every wall, vented low and high. PETG
  only.
* **The door.** Two screws (hex key 2 mm), so children do not get at the
  cells; ribs 1 mm above the cells keep them in their holders in a fall; the
  engraved label reads "LiFePO4 3,2 V / nur 4 x AER18650m2A2 / gleiche
  Zellen, max. 20 mV / Polaritaet: siehe Halter / Laden nur 0-45 C".
* **Wiring.** 22 AWG for everything that carries the cells' or the charger's
  current. Route the leads through the notches so the lid and the door
  cannot pinch them, and heat-shrink every joint.
* **If a cell is hot, swollen, dented or leaking**, or the device was dropped
  and the compartment is damaged: unplug, do not charge again, and follow
  NUTZUNG.md §4.

### USB-C operation

* **Use a CE-marked USB-C charger** from a regular shop, 5 V / 1.5 A or
  more, with a C-to-C cable. J2's 5.1 k resistors on CC ask for plain 5 V
  without any negotiation, so any USB-C charger will do, and an 18 W phone
  charger is plenty. The charger is the only part on mains voltage, and its
  quality is outside this design. The #6091 has an input overvoltage
  protection at 18.5 V.
* **Charge pause.** After a full charge the firmware holds the charger's CE
  high: the cells rest and the device runs from USB-C. Charging is released
  when USB-C was unplugged, below 3.30 V, or after 30 days. If the firmware
  stops, CE falls back low through the #6091's pull-down and the charger
  charges normally - fail-safe, because every limit is in hardware.
* J2 is screwed to two posts, so plugging and unplugging does not load the
  solder joints.
* **Standards.** IEC 62133-2 and UN 38.3 apply to the cells and are the
  manufacturers' job: buy branded cells from akkuteile.de, nkon or Mouser, no
  marketplace cells. The EU Battery Regulation 2023/1542 applies to whoever
  places a battery-powered product on the market; a private build is not
  placed on the market. Selling the device would need a CE/EMC/RED assessment
  (the ESP32-C6 module is RED-certified, the device is not); the cells stay
  user-replaceable in their holders. None of this has been assessed by a test
  lab; it is the reasoning behind the design, not a certificate.

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
  detection, history, 24 h statistics, LED logic and the power module's
  decisions (PWR-K decoding, charge pause, safety-timer restart) - is covered
  by 537 host checks that run on every build, plus the Power-Standard's own
  `pwr_std` test.
* `ac_hal` - the actual sensor drivers - is **not covered by any automated
  test**. It cannot be, without hardware. Every register address, command
  code and timing in it was taken from a manufacturer datasheet and checked by
  reading, not by running. The least exercised path is the Sunrise's
  host-held ABC state: read after every measurement, written back before the
  next one, kept in flash (TESTING T-S3).
* The event detector's thresholds are engineering judgement. They have not
  been validated against a real printer. Expect to adjust `ev_sensitivity`.
* NVS wear: history, baseline and the Sunrise state are flushed every 30 minutes, about 17 000 times a year, across a
  wear-levelled partition. That is well within flash endurance, but it is
  not zero. The Sunrise's own EEPROM settings are only written when they
  differ, normally once.

---

## What this device must never be described as

Not a medical device. Not a safety detector. Not a certified instrument. Not
evidence that any particular material is safe or unsafe to print.

The correct sentence is *"air quality elevated"*, and the reason. Never *"this
air is unsafe"* - the device cannot know that, and the firmware never says it.
