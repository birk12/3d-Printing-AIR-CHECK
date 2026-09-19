# Engineering decision record

One entry per choice that would be expensive to reverse. Each says what was
picked, what it was picked over, and what it costs.

---

## EDR-1: ESP32-C6, not ESP32-H2

**Decision.** ESP32-C6, on an Adafruit ESP32-C6 Feather in v1.0/v1.1 and on a
DFRobot FireBeetle 2 ESP32-C6 since v1.2 (EDR-15).

**Alternative.** ESP32-H2, which is Espressif's dedicated 802.15.4 part and
has no Wi-Fi radio at all.

**The number that decided it.** Espressif publish measured current traces for
their own Matter ICD example on both chips, at the same 5 s poll rate and
20 dBm TX power:

| | average | sleep floor | peak |
|---|---|---|---|
| ESP32-H2 | 104 uA | 27 uA | 119 mA |
| ESP32-C6 | 174 uA | 55 uA | 344 mA |

The H2 is genuinely better, by about 70 uA. Over 183 days that is 310 mAh,
roughly 8 % of the pack.

**Why the C6 anyway.** 512 kB of SRAM against the H2's 320 kB - and this turned
out to be the decisive number rather than a comfort margin. The finished
firmware reports:

```
v1.0 (with display)  DIRAM 224 636 bytes (49.7 %), image 1.69 MB
v1.1 (headless)      DIRAM 210 484 bytes (46.6 %), image 1.65 MB, 16 % OTA headroom
v1.2 (SEN63C)        DIRAM 210 084 bytes (46.5 %), image 1.68 MB, 15 % OTA headroom
```

That is with four endpoints, six clusters on one of them (four now with
24 h peak and average), a 5.6 kB history structure and the whole Matter stack.
Almost half the C6's usable RAM is gone before any runtime allocation. The H2
has 130 kB less to start with, so this build would be very tight on it - and
that is *before* someone adds a feature.

The second reason is boring and decisive for a DIY project: there are cheap,
in-production C6 boards with a USB-C connector and a LiPo charger already on
them, and there is no equivalent for the H2. Building those blocks badly would
cost more than 70 uA.

**Cost.** About 8 % of the battery life. Documented, not hidden. If someone
later spins the custom carrier with a bare module on it, the H2 is the right
choice and nothing above `ac_hal` changes.

---

## EDR-2: the SPS30 talks UART, not I2C — *superseded by EDR-15 in v1.2*

> Kept for the record. Since v1.2 there is no SPS30; the SEN63C only has I2C,
> and gets its own bus with pull-ups on its own switched rail instead.

**Decision.** SHDLC over UART1 on GPIO16/17, with 330 R in series on each line.

**Alternative.** I2C at 0x69 on the same bus as the other sensors, which is
what most projects do.

**Two reasons, both from the datasheet.**

1. Sensirion: *"For connection cables longer than 20 cm we recommend using the
   UART interface, due to its intrinsic robustness against electromagnetic
   interference... We recommend using the UART interface instead, whenever
   possible."* The SPS30 sits in the sensor bay on a cable.

2. The SPS30's 5 V rail is switched off between measurements, which is the
   whole basis of the energy budget. On I2C, the Feather's 5k1 pull-ups would
   keep pushing about 0.5 mA per line into the unpowered sensor's protection
   diodes - and critically, they would keep doing it through deep sleep, when
   the firmware is not running and cannot hold the lines low. GPIO18/19 are
   not RTC pins, so there is no way to hold them. On UART both lines go high
   impedance in deep sleep and nothing flows at all.

**Cost.** A more complicated driver: SHDLC framing, byte stuffing and a
checksum instead of `i2c_master_transmit_receive`. About 300 lines.

---

## EDR-3: e-paper, not OLED or memory LCD — *superseded by EDR-12 in v1.1*

> Kept for the record. Since v1.1 the sensor has no display; numbers are shown
> on a separate e-ink dashboard and in Apple Home.

**Decision.** 1.54 in 200x200, SSD1681 controller.

**Alternatives.** A 1.3 in OLED (about 10-20 mA while lit), or a Sharp memory
LCD (about 10 uA holding an image, but it needs a continuous VCOM toggle).

**Why.** The brief puts battery life first, and e-paper is the only one of the
three that costs *nothing at all* to keep an image on screen. That changes
what "display timeout" means: on this device the screen never goes blank, it
just stops being updated. You can walk past it six months after the last
button press and still read the last measurement.

At 184 dpi a 46 px numeral is 6.3 mm tall, which is readable across a desk.
The panel is 27.6 x 27.6 mm of active area, which is small - that is the price
of the "1.3 to 2.0 inch" envelope in the brief and of the power budget.

**Cost.** Slow. A full refresh takes about 2 s and a partial about 0.4 s, so
the UI is designed around discrete button presses, not scrolling.

---

## EDR-4: the SCD41 is power cycled, and we implement our own ASC — *superseded by EDR-15 in v1.2*

> Kept for the record, with a correction. The firmware-side self calibration
> described below was **never implemented**: the `co2_self_calibration` flag
> existed in the configuration, but no code read it. Since v1.2 the flag
> switches the SEN63C's own ASC instead, and the SCD41 is gone.

**Decision.** Power-cycled single-shot mode, one measurement an hour in ECO,
and a firmware implementation of automatic self calibration.

**The number.** Sensirion's low-power application note, Table 1, at 3.3 V:

| mode | average |
|---|---|
| high performance, 5 s | 15 mA |
| low power periodic, 30 s | 3.2 mA |
| power-cycled single shot, 10 min | 250 uA |
| power-cycled single shot, 1 h | 43 uA |

43 uA is 1.0 mAh a day. 3.2 mA is 77 mAh a day. The six month target is not
reachable in any other mode.

**The catch, stated plainly.** Sensirion: *"for power-cycled single shot
operation, ASC is not available in either case."* Without automatic self
calibration an NDIR CO2 sensor drifts. So the firmware tracks the lowest CO2
reading over a rolling seven day window and, when that minimum is stable and
the implied correction is small, performs a forced recalibration against
420 ppm - which is what Sensirion's own ASC does internally. It is on by
default, it is guarded, and it is switchable. `docs/CALIBRATION.md` explains
when to turn it off.

**Cost.** CO2 accuracy depends on the room being aired out occasionally. In a
room that is never ventilated, the CO2 channel will read low over time.

---

## EDR-5: VOC every 10 s as the tripwire, PM on a slow clock

**Decision.** In ECO, the cheap channel watches continuously and the expensive
channel measures on a slow clock: every 4 h in v1.0, **every hour since v1.1**
(EDR-12).

**The numbers.** One 40 s SPS30 window costs about 0.9 mAh. A VOC sample in
Sensirion's low-power sequence costs about 0.017 mAh - fifty times less. Four
PM windows a day is already the biggest line in the budget.

**So.** VOC samples every 10 seconds (the upper end of the interval the Gas
Index Algorithm is validated at), and a VOC excursion escalates the device
into ACTIVE, where PM samples every two minutes. Printing raises VOC long
before a four-hourly PM sample would have noticed anything.

**Cost.** A particle event that produces no VOC - sweeping, a dusty draught -
can fall between two particle samples in ECO. That is the honest limitation
and it is in `docs/BATTERY_LIFE.md` in as many words. NORMAL mode narrows it
to 15 minutes at the cost of roughly three weeks of runtime.

---

## EDR-6: load switches on rails, not sensor sleep commands

**Decision.** TPS22918 load switches on every sensor rail. Since v1.2 there
are two (SEN63C, SGP40 breakout) and no boost converter; in v1.1 the SPS30
rail was gated on the boost converter's *input*.

**Alternative.** Leave everything powered and use each sensor's own sleep
command. The SPS30 sleeps at 38 uA, the SGP40 idles at 34 uA, the SCD41's
power_down is in the microamps.

**Why not.** 38 uA at 5 V through a 92 % efficient boost is 54 uA from the
battery, plus the TPS61023's own ~19 uA quiescent, plus the SGP40's 34 uA.
That is about 2.6 mAh a day of doing nothing, on a budget of roughly 15. It is
also 38 uA that Sensirion specify as *typical* with a maximum of 50.

**Also.** A load switch is the only reliable way out of a hung I2C sensor.
A device that has to be opened up and unplugged to recover is not a product.

**Since v1.2 also:** the SEN63C idles at 3.3 mA, so for it there is no sleep
command worth using at all, and the SGP40 *breakout* draws ~185 uA on its own
(EDR-14) - the load switch is the only thing that turns that off.

**Cost.** Two parts, two GPIOs, and the SEN63C has to be given its 100 ms to
boot on every wake.

---

## EDR-7: modules on a carrier board, not a single custom PCB

**Decision.** A ready-made C6 board plus modules on a simple two-layer carrier
(since v1.2: FireBeetle 2 ESP32-C6, SEN63C, SGP40 breakout), with a
JLCPCB-assembled carrier as an option.

**Why.** The brief asks for the fastest reliable first build and says to
prefer modular electronics unless a custom PCB clearly wins. It does not
clearly win here: the three blocks a custom board would have to get right -
USB-C, LiPo charging and fuel gauging - are exactly the three that are
hardest to debug without equipment, and they already exist, tested, on a
EUR 7.50 board (EUR 22 for the Feather before v1.2).

**Cost.** The breakouts carry parts we do not want (EDR-14), which the
firmware has to work around by power-gating them.

---

## EDR-8: the SPS30's ports face down, into a sealed split duct — *superseded by EDR-15 in v1.2*

> Kept for the record. The SEN6x design-in guide says the opposite of the SPS30
> guide - ports must *not* face up or down - so since v1.2 the SEN63C stands on
> its side with its ports against the left wall.

**Decision.** Ports facing the bottom wall, inlet and outlet separated by a
sealed rib with a foam gasket, in a sensor bay that is walled off from the
electronics.

**Straight out of Sensirion's mechanical guidelines.**

* *"Placing the sensor with the inlets/outlet facing down avoids dust
  accumulation and accelerated sensor aging."* - so, down.
* *"A tightly sealed separation between inlet and outlet will result in the
  best performance"* and *"avoid designs which will make air flow from the
  outlet back to the inlets"* - hence the rib and the gasket.
* *"It should be avoided to design the SPS30 in close vicinity to heat
  sources... it is further recommended to place the SPS30 below heat
  sources"* - the sensor bay is the bottom third of the case and the
  electronics are above it.
* *"The sensor should be isolated from the airflow of the final device if the
  velocity of this flow is greater than 1 m/s"* - there is no other fan.

The battery is in the back layer, outside the air path entirely.

**Cost.** In v1.0 the case was 122 x 114 mm because the display, the sensor
bay and the cell each needed their own band. Without the display (v1.1) it
is **98 x 102 x 32 mm**: the cell stands upright in the back layer, behind the
sensor bay and the electronics, under a printed cover that keeps the
sensor-bay air off it.

---

## EDR-9: SIT ICD at a 15 s poll, with LIT as an experiment

**Decision.** Short Idle Time ICD, `CONFIG_ICD_SLOW_POLL_INTERVAL_MS=15000`.

**Why not LIT.** A Long Idle Time ICD can sleep far longer, and esp-matter
supports it. But Matter 1.4 says a LIT ICD *"SHALL operate as a SIT ICD if it
does not have at least one registration with any client on any fabric"* - so
if the controller does not register as an ICD client, you get SIT behaviour
from a more complicated device. Whether a given HomePod does is not something
this project can assert without testing it.

The LIT configuration is shipped as `sdkconfig.defaults.lit` and labelled
experimental.

**Cost.** The radio line stays at about 2.3 mAh/day instead of well under 1,
and the device takes up to 15 s to react to a command from the Home app.

---

## EDR-10: the VOC Index is published as a number, with a loud caveat

**Decision.** Publish the Sensirion VOC Index in the Matter TVOC concentration
cluster with the unit declared as PPB, on by default, switchable.

**The problem.** The SGP40 produces an *index*, 1 to 500, where 100 is this
room's own 24 hour average. It is not a concentration and cannot be converted
into one. Matter's TVOC cluster wants a concentration.

**The options.** (a) Publish only the cluster's LevelIndication feature, which
is honest but gives Apple Home no number to build an automation on - and the
brief asks explicitly for "IF VOC becomes elevated -> notification". (b)
Publish the index as a number in a unit it is not.

**We chose (b), and say so everywhere:** in `docs/MATTER.md`, in
`docs/APPLE_HOME.md`, `docs/DASHBOARD_INTERFACE.md` and in the code. The
config flag `voc_publish_index_as_ppb` turns it off for anyone who would
rather have no number than a mislabelled one.

**Cost.** A number in Apple Home labelled "VOC" whose unit is wrong. This is
the least comfortable decision in the project and it is deliberately the
easiest one to reverse.

---

## EDR-11: two I2C buses, because the Feather's pull-ups share a regulator with an LED

**Found in v1.1, and it is a real v1.0 bug.** Adafruit's own schematic for the
ESP32-C6 Feather shows that the board's I2C pull-ups (R3, a 10 kΩ network, not
the 5 kΩ the learn guide mentions) and its WS2812B RGB LED both sit on
**VSENSOR**, the output of the second LDO, which GPIO20 switches. The MAX17048
fuel gauge is on the same bus.

v1.0 drove GPIO20 low to save power, which removed the only pull-ups on the
bus the SGP40 and SCD41 were on. That firmware could not have talked to its gas
sensors or its fuel gauge. The fix is not simply "keep GPIO20 high" either: a
WS2812B idles at roughly a milliamp even when dark. That is 24 mAh/day, more
than everything except the particle sensor.

**Decision.**

* **Sensor bus**: SGP40 + SCD41 on the C6's **LP_I2C**. Its pads are fixed at
  GPIO6 (SDA) and GPIO7 (SCL) (`LP_I2C_SDA_IOMUX_PAD` / `..._SCL_...` in
  ESP-IDF's `hal/esp32c6/include/hal/i2c_ll.h`). The carrier has its own
  4.7 kΩ pull-ups there, to the **switched** sensor rail, so switching the
  rail off leaves nothing to back-feed the sensors.
* **Gauge bus**: the Feather's own bus on GPIO19/18 stays as it is. GPIO20 is
  switched on for the few milliseconds of a battery read, every 5 minutes, and
  off again. The WS2812B is lit for as long as a read takes, and dark (and
  unpowered) otherwise.
* The **button** moves from GPIO6 to **GPIO1**, which is also RTC-capable and
  still wakes the chip from deep sleep.

**Cost.** Two buses instead of one, and a caveat from ESP-IDF: LP_I2C has no
sleep-retention module. The LP domain stays powered in light sleep, so this
should not matter, but it is on the bench-test list (B5).

**Unverified.** The MAX17048 datasheet describes a sleep mode entered when SDA
and SCL are held low; with GPIO20 off, R3 pulls both lines to about 0 V.
Adafruit's own low-power guidance switches the same rail off, so the gauge
should keep tracking, but a bench test must confirm that the percentage keeps
moving across a day.

**Since v1.2** the Feather, its gauge and its switched LDO are gone (EDR-15).
The two-bus principle stays: one bus per switched rail, pull-ups on that rail.
The LP_I2C bus is now created for each VOC sample and deleted afterwards, so
its missing sleep retention no longer matters.

---

## EDR-12: the sensor has no display; a separate dashboard shows the numbers

**Decision (2026-09-18).** The sensor is a headless, battery-powered box with
one button and an RGB status LED. Numbers are shown on a separate e-ink
dashboard (built elsewhere) and in Apple Home. The battery target changes from
six months to **three**. That buys hourly particle measurements instead of
four-hourly.

**How the dashboard gets the data.** Apple Home has no local API, so the
dashboard cannot ask the HomePod. It uses **Matter multi-admin**: each sensor
is shared from the Home app ("Turn On Pairing Mode") into the dashboard's own
fabric, and the dashboard reads the sensors directly. The HomePod mini is the
Thread border router in between, so it routes the traffic but does not
interpret it. The full contract, attribute IDs included, is in
`docs/DASHBOARD_INTERFACE.md`.

**What the sensor adds for it.**

* PeakMeasuredValue and AverageMeasuredValue over 24 h on PM2.5, PM10, CO2 and
  VOC. These are standard features of the concentration clusters, so a
  dashboard that sleeps most of the time can still show "worst today".
* The configured name as Basic Information / NodeLabel. Apple Home's names
  never reach a second fabric.
* Identify blinks the LED white, which is how you find out which of two
  identical boxes you are looking at.

**What it removes.** The e-paper panel and its load switch, the SSD1681 driver,
three bitmap fonts and the screen renderer. The case goes from 122 x 114 to
98 x 102 mm, the BOM drops by about EUR 17, and RAM use from 49.7 % to 46.6 %.

**Cost.** Nothing to read at the printer without a phone or the dashboard.
The LED answers a button press with the air-quality colour (green, yellow,
red, purple), and otherwise stays dark.

---

## EDR-13: a measured ICD figure replaces the vendor one

**Input.** Microamp Home (YouTube `KE7bOYCYETM`, repo
`uamphome/matter_sensor_xiao_nrf52840`) measured an ESP32-C6 DevKit as a Matter
SIT ICD at **our exact configuration**, a 15 s slow poll, with a PPK2 over one
hour: **121.9 µA average, 39.3 µA sleep floor**. Our model had derived about
95 µA at 15 s from Espressif's 5 s trace.

**Decision.** Use the measured 121.9 µA. It is independent, at our poll rate,
and the more conservative of the two. It moved ECO from 3.1 to 3.0 months with
margin.

**Also from that video, considered and not adopted:**

* **Long Idle Time ICD with a 5 minute slow poll** got the author's
  nRF52840 + SHT41 sensor to 17.5 µA. For us the radio is about 9 % of the
  budget, so LIT would buy about 6 % more runtime. The catch: a LIT device
  without a registered ICD client must behave as SIT, and whether Apple Home
  registers is unverified. It stays in `sdkconfig.defaults.lit`, experimental.
* **nRF52840 instead of the ESP32-C6.** Better radio power, but it would mean
  rewriting the whole stack on nRF Connect SDK to save about 2.5 mAh/day on a
  31.5 mAh/day budget. Not worth it while the SPS30 is two thirds of the bill.
* **Board overhead matters more than the chip.** He measured XIAO- and other
  C6 boards at 226-549 µA against 122 µA for the bare DevKit. This is exactly
  the kind of overhead EDR-11 is about, and why bench test B12 comes first.

---

## EDR-14: the breakout boards' own LDO and LED, and a correction to v1.1

**Found in v1.2.** Adafruit's schematics for the SGP40 breakout (4829) and
the SCD41 breakout (5190) both show an **AP2112K-3.3 LDO** (about 55 µA
quiescent) and a **green power LED through 10 kΩ** (about 130 µA) on the
breakout's supply. v1.1 kept that supply switched on permanently, and its
energy model counted neither part. It also took the TPS22918's quiescent
current as 1.1 µA; TI's datasheet (6.5) gives **8.3 µA with the switch on**
and 0.5 µA off.

**What v1.1 would really have done.** Two breakouts at ~185 µA each plus the
load switch is about 9 mAh/day on top of the 31.5 the model showed: roughly
**2.3 months** with margin, not 3.0. Nobody built one, but the documentation
said 3.0 and it was wrong.

**Decision (v1.2).** The SCD41 breakout is gone (EDR-15). The SGP40 breakout
stays, and its rail is switched on only for each sample: about 0.25 s every
10 s, including a 5 ms settle for the load switch, the LDO and the sensor.
The SGP40 comes out of power-up in the same idle state the heater-off command
leaves it in, so the low-power measurement sequence does not change, and the
VOC Index algorithm state lives in the MCU. What the pulse costs is modelled
explicitly: the breakout's 185 µA for 2.5 % of the time, plus recharging its
21 µF of capacitance every sample - 0.94 mAh/day for the whole VOC channel.

**Alternative.** Remove the LED (a solder job on an 0603) or use a breakout
without LDO and LED. Both work; neither is something to ask of every
builder when the firmware can do it.

**Also.** Every load switch now has a CT capacitor (EDR-15), because switching
20 µF straight onto the 3.3 V rail every ten seconds would pull an
amp-level spike from the MCU's own supply. And TPS22918 pin 5 is QOD, not a
second VOUT as the v1.1 net list called it; tying it to VOUT is one of the
three configurations TI allow, so the v1.1 wiring was legal but misnamed.

---

## EDR-15: one SEN63C and a FireBeetle, instead of SPS30 + SCD41 + Feather — *sensor and power parts superseded by EDR-16..18 in v1.3*

**Decision (2026-09-18).** Replace the SPS30, the SCD41 breakout, the 5 V
boost converter and the Adafruit Feather with a **Sensirion SEN63C** (PM1 to
PM10, CO2, temperature and humidity in one module, 3.3 V) and a **DFRobot
FireBeetle 2 ESP32-C6**. Keep the SGP40 for VOC.

**Why.** Cost, without giving up a measurement. v1.1's electronics were
EUR 173, and two thirds of that was sensors bought one function at a time:
SPS30 EUR 38, SCD41 breakout EUR 52. The SEN63C is EUR 34 at Mouser - less
than the SPS30 alone - and IKEA ship the same module in a EUR 29 retail air
monitor, which is a reasonable sign of its volume and stability. The
FireBeetle is EUR 7.50 against EUR 19-27 for the Feather. v1.2 comes to about
EUR 95 (`docs/BOM.md`).

**Why the SEN63C and not the SEN66.** The SEN66 would also replace the SGP40,
but VOC is our 10-second tripwire (EDR-5), and a SEN6x cannot be kept
measuring on a battery: it has no low-power mode with the fan off and idles at
3.3 mA. Power-cycling it hourly would leave the VOC index meaningless between
windows. The SEN66 is also EUR 63, more than SEN63C + SGP40.

**What changes, and what it costs.**

* **CO2 accuracy** drops from ±(50 ppm + 5 %) to **±(100 ppm + 10 %)**. For
  "should I open a window" that does not matter; it is the one measured
  trade-off.
* **CO2 comes with the PM window.** The SEN63C's CO2 output reads "unknown"
  for the first 22..24 s of a measurement, so a window shorter than 30 s gives
  no CO2 (`AC_PM_MIN_WINDOW_S`). ECO runs 40 s an hour and takes the last
  valid CO2 reading of the window.
* **Self calibration is unproven in this duty cycle.** The SEN63C's ASC is on
  by default and persistent, but Sensirion do not say whether it converges
  when the module runs 40 s an hour. Bench test B16 checks it against outdoor
  air over a week; the fallback is a forced recalibration
  (`docs/CALIBRATION.md`).
* **No fuel gauge.** The FireBeetle measures the cell through a 1M/1M divider
  on GPIO0. The percentage comes from a resting-voltage curve and is honest to
  about ±10 % in the middle of the discharge, better at the ends
  (`ac_core/ac_battery.c`). USB presence comes from a 68k/100k divider on VIN.
* **No weekly fan cleaning.** The SEN6x keeps its optics clean with a sheath
  flow and Sensirion removed automatic cleaning from the family (design-in
  guide, FAQ 8).
* **Ports sideways.** The SEN6x guide says the ports must not face up or down
  (the SPS30 guide said down). The module stands on its side against the left
  wall, gasketed, and the case grows from 98 to **112 mm** wide to leave the
  depth behind its connector free. Sensirion's STEP model and datasheet v0.92
  fig. 12 put the connector in a 6.35 mm pocket at 17.2-29.2 mm along the
  module and 16.3-21.8 mm across it - with the square inlet at the front, that
  is towards the lid, level with the battery, so the keep-out is needed.
* **Board.** From DFRobot's own schematic: CN3165 charger at 540 mA, TPS62A02
  buck, the green LED on GPIO15 is only lit when driven. No WS2812B, no second
  LDO - none of the EDR-11 trouble. DFRobot quote 36 µA in deep sleep for the
  board (hardware v1.2); the model takes 29 µA of that as board overhead. That
  is a vendor figure, and C6 boards have measured far worse than their data
  sheets (EDR-13), so bench test B12 still comes first.
* **The FireBeetle has no VBAT pin.** The cell plugs into the carrier and is
  passed through to the FireBeetle, so the status LED can keep its anode on
  VBAT.

**Energy.** ECO comes out at 29.6 mAh/day, **3.2 months with margin** at the
SEN63C's typical current and 2.7 months if every window drew the datasheet
maximum (`docs/BATTERY_LIFE.md`).

---

## EDR-16: CO2 from a Senseair Sunrise, not from the SEN63C

**Decision (2026-09-19).** Measure CO2 with a **Senseair Sunrise
006-0-0008** (NDIR, ±(30 ppm + 3 %)) in single-measurement mode every 5
minutes. Replace the SEN63C by the **SEN62**, which is the same particle
sensor without the CO2 and T/RH parts.

**Why.** "Shit in, shit out": the SEN63C's CO2 number cannot be trusted in a
battery device that power-cycles it.

* The SEN6x datasheet (v0.92, section 1.5) makes the CO2 specification
  conditional on **continuous operation** with ASC on and fresh air once a
  week. v1.2 ran the module 40 s an hour.
* The STCC4 inside it (STCC4 datasheet, 1.1.2-1.1.4) stores its ASC state at
  most every **2 hours of continuous operation**, needs up to **1 hour** of
  operation after a longer power-off to reach its accuracy, and restarts its
  first-save timer when it is power-cycled within the first hour. With a
  1-minute window per hour the ASC state is never saved; the self-calibration
  never converges.
* The alternative - keeping the SEN63C running - costs 80 mA continuously and
  empties any battery in days.

The Sunrise is built for exactly this duty cycle. In single-measurement mode
it is switched off with its EN pin between measurements, and the host reads
its ABC (automatic baseline correction) state and filter state after each
measurement and writes them back before the next one (TDE5531 registers
0xC4-0xDB; `ac_hal/src/sunrise.c`). The ABC keeps working across power
cycles because the state lives in the ESP32's flash, not in the sensor. The
host adds one to the ABC time register for every hour that passes, as
Senseair's integration guide requires. A measurement costs 1.60 mC
(TDE7318): at 5 minutes that is 0.6 mWh a day, invisible in the budget.

**Pressure.** NDIR reads molecules per volume: 1.6 % per kPa (PSP12440). The
Sunrise accepts a pressure value with every measurement. There is no
barometer on board, so the firmware computes the station pressure from the
configured altitude (`altitude_m`, ISA formula). Weather moves the real
pressure by about ±2 kPa - ±3 % of reading. That is the one known residual,
and it is inside the sensor's own ±3 % term. A barometer can be added later
on the same always-on bus without changing anything else.

**Calibration.** ABC with a 180 h period and a 425 ppm target (register
0x9A, 0x9E; 425 is the 2026 outdoor background), on by default. A target
calibration at fresh air is available from the button (hold 8-12 s) and
from the service console (`co2 frc 425`). CALIBRATION.md has both.

**What it costs.** EUR 49 (Mouser, EUR 41.18 net) - the most expensive part.
The SEN62 is EUR 13 cheaper than the SEN63C, so the change costs about
EUR 36.

**Rejected.** SCD41/SCD43 in idle single-shot (about 11 mAh/day at 5 min per
Sensirion's low-power note, and the same ASC duty-cycle caveat), Senseair
Sunlight (lower power, but no EU distributor price could be confirmed),
Cubic CM1106SL-NS (±(50 ppm + 5 %)).

---

## EDR-17: temperature and humidity from an SHT40, the gas bay without adhesives

**Decision (2026-09-19).** Temperature and humidity come from a **Sensirion
SHT40** (Seeed Grove board), read every 10 s together with the SGP40, on the
always-on LP I2C bus. The SEN62's own T/RH is not used. Nothing inside the
gas bay is glued, taped or foamed.

**Why.** The SEN6x T/RH are compensated for a module that runs continuously;
power-gated for 60 s an hour, the reading comes from a module that has just
warmed its own fan and laser. The SHT40 (±0.2 °C, ±1.8 %RH) draws 1.2 µA idle
and feeds the SGP40's humidity compensation with a fresh value every sample,
which the VOC algorithm needs.

**The gas bay.** Sunrise, SGP40 and SHT40 share a walled bay in the lower
right corner (seen from the front), vented through the side wall and the
front face (1094 mm² open area). Rules, from Sensirion's handling
instructions for its gas sensors and Senseair's ANO4947:

* no foam, tape, glue, potting or silicone anywhere in the bay - all emit
  VOCs for months and poison the SGP40's baseline; the boards sit in printed
  fences and on screws;
* the SHT40 sits low, next to the vents, furthest from the ESP32 and the
  Sunrise's lamp;
* the Sunrise keeps ≥1.5 mm from its filter and ≥1.0 mm from its housing to
  anything; its pins hang free beside a printed pedestal;
* the bay is closed from the rest of the device by printed walls that meet
  ribs on the lid (0.2 mm gap, no gasket);
* the printed parts are baked at 50-60 °C for 24 h before assembly, so PETG
  residual volatiles are gone before the SGP40 learns its baseline
  (`manufacturing/print-settings.md`);
* a pen test (TESTING.md) checks that the bay sees room air within a minute.

The SEN62's gasket (EPDM) stays: it is outside the gas bay, and it seals the
particle sensor's own flow path, not the gas bay.

---

## EDR-18: six AA cells, nothing is charged inside the device — *extended by EDR-20 in v1.3.1*

**Decision (2026-09-19).** Run from **6 × AA** in a screwed compartment
(MPD BH36AAW holder), through a **PTC fuse**, a **Pololu S9V11E2A**
buck-boost set to 4.0 V and an **LM66200 ideal diode** into the FireBeetle's
battery input. Recommended cells: **Energizer Ultimate Lithium L91**. NiMH
(eneloop pro) and alkaline also work. Nothing charges inside the device.

**Why.** The requirement: the highest German/EU fire and safety standards.
The v1.2 pouch LiPo was charged inside a 3D-printed case, by a charger
(CN3165) whose temperature input is tied to ground on the FireBeetle -
there is no cell temperature monitoring. That is legal for a hobby build but
it is the one part of the device that can start a fire.

* **No in-device charging** removes the charge-fault scenarios entirely.
  With USB plugged in the FireBeetle's charger holds its battery input at
  4.2 V; the LM66200 blocks any current back towards the regulator as soon as
  the output is more than 70 mV above its input (TI SLVSG04), and 4.2 V is
  0.1-0.3 V above the 4.0 V rail. The cells can never be charged, not even
  the rechargeable ones. ERC check 6 in `design.py` guards this.
* **Primary lithium-iron-disulfide cells** (L91) are the most benign
  chemistry in the list: 1.5 V, no charging, UN 38.3 tested, no leaking.
  NiMH cells are charged outside, in a charger made for them.
* **Energy limit.** The PTC (Bourns MF-R050) limits a short circuit
  downstream of the holder to 1 A (trip), and the pack itself cannot deliver
  much more than 9 V × a few amps. The whole device stays within IEC 62368-1
  power-source class **PS1** territory (≤ 15 W) for any fault after the
  fuse, which needs no fire enclosure. For a clear margin under PS1 at a
  fresh 10.8 V pack, the MF-R030 (0.3 A hold, 0.6 A trip) is the stricter
  choice; the load (70 mA sustained, 0.5 A peaks for milliseconds) fits
  either - verify its trip time before swapping.
* **Separation.** The compartment is walled off from the electronics and
  the gas bay by a 4 mm partition to the lid, with one 6 × 6 mm lead notch,
  and it has its own pressure-relief slots in the bottom wall so a venting
  cell cannot pressurise the case.
* **Tool-secured door**, two M2.5 screws: coin cells are not involved, but
  a child should not get at six AA cells either.

**Standards this touches (for a private build):** IEC 62133-2 and UN 38.3
apply to the cells, and are the manufacturers' job - buy branded cells from
a regular shop. EU Battery Regulation 2023/1542 applies to whoever places a
battery-powered product on the market; a private build is not placed on the
market. Should the device ever be sold, a CE/EMC/RED assessment would be
needed anyway (the ESP32-C6 module is RED-certified, the device is not).

**Rejected.** LiFePO4 32700 (3.2 V, safe chemistry, but needs a charger and a
self-printed holder; ~3 months), power banks (they switch off at low load -
most below 50-100 mA - and are Li-ion again), 1.5 V Li-ion AA with USB-C
(regulated 1.5 V hides the state of charge; ~2.3 Wh measured; not
recommended, still works).

**Cost in runtime.** L91: 3.7 months ECO with 25 % margin; NiMH 2.2; alkaline
2.1 (`docs/BATTERY_LIFE.md`). The regulator's < 0.2 mA quiescent current is
about a fifth of the ECO budget - the price of an off-the-shelf module.

---

## EDR-19: no custom circuit board

**Decision (2026-09-19).** v1.3 has no custom PCB. Every electronic part is a
finished module; the ten resistors and one capacitor that remain are
through-hole parts soldered at a module's pins under heat shrink
(`electronics/schematic/NETLIST.md`, "Inline parts").

**Why.** A first-spin board has errors, and each respin is weeks of waiting.
The v1.2 carrier existed for two load switches, pull-ups and a connector; all
of that now comes on modules:

| function | module | size (mm) | mounting |
|---|---|---|---|
| controller | DFRobot FireBeetle 2 ESP32-C6 (DFR1075) | 60 × 25.4 | 4 × M2 on posts |
| PM | Sensirion SEN62 | 55.2 × 25.6 × 21.3 | cradle + side-wall gasket |
| CO2 | Senseair Sunrise 006-0-0008 | 33.5 × 19.7 × 11.5 | pedestal + guides |
| VOC | SparkFun SGP40 (SEN-18345) | 25.4 × 25.4 | 4 × M2.5 on posts |
| T/RH | Seeed Grove SHT40 (101021032) | 40 × 20 | fence |
| SEN62 switch | Pololu #2810 | 15.24 × 15.24 | pocket |
| regulator | Pololu S9V11E2A (#5719) | 10.9 × 16.5 | pocket |
| ideal diode | Adafruit LM66200 (#5830) | 16.51 × 10.16 | 2 × M2 on posts |
| cells | MPD BH36AAW, 6 × AA | 110 × 49.5 × 17.8 | 4 × M2.5 on bosses |
| LED | 5 mm RGB in a Signal Construct SMR1089 holder | Ø8 hole | panel nut |
| button | Reichelt T 250A | Ø7 hole | panel nut |

Positions and pin rows are from the manufacturers' drawings and board files
(FireBeetle dimension PDF, Pololu drawing, Adafruit and SparkFun `.brd`,
PSP12440). Two are not published and are marked for measurement before
printing: the Grove SHT40's hole pattern (hence the fence), and the
Sunrise's drawing 740-00993 (the pin rows come from a third-party footprint).

**Pin choice that fell out of it.** GPIO16 is the ESP32-C6's U0TXD, and the
ROM boot log toggles it after every reset. The Sunrise must see no signal on
its bus while EN is low (TDE7318), so the Sunrise bus moved to GPIO17/21 and
the red LED went to GPIO16, where the boot log only makes it flicker.

---

## EDR-20: continuous operation from a USB-C charger; undervoltage lockout; one power block for every project

**Decision (2026-09-19).** Add a second input: a **USB-C power socket**
(Adafruit #6050, sunken breakout) feeding a **second Pololu S9V11E2A at
4.20 V** into the LM66200's second input. Lower the cells' regulator to
**3.90 V**. Add a **13 kΩ resistor from the cells' regulator's EN pin to GND**
as an undervoltage lockout. This chain is the common power block for the
user's battery devices (AIR CHECK, Sleeper Frame, and whatever comes next).

**Why the socket.** The user wants to run the device permanently from an
18 W USB-C charger. The FireBeetle's own USB-C would work with cells in, but
without cells the Sunrise and the LED would hang on the FireBeetle's LiPo
charger (CN3165) - a charger used as a power supply, with no specified
behaviour without a battery. Feeding the header's VCC pin instead would keep
the CN3165 powered all the time: 350-660 µA (CN3165 datasheet, operating
current). A separate socket with its own regulator is deterministic:

* The #6050's 5.1 kΩ resistors on CC ask any USB-C charger for plain 5 V (up
  to 1.5 A) with a C-to-C cable, without negotiation. The device draws at
  most about 0.6 A at 5 V.
* The LM66200 passes the higher of its inputs. 4.20 V beats 3.90 V by at
  least 0.19 V after tolerances, far past its 70 mV threshold, so external
  power always wins and the cells are the backup, switched in without a gap
  when the power goes (SLVSG04: no soft start during switchover).
* USB 5 V never reaches the pack, and the cells still cannot be charged: the
  only way into them would be backwards through a buck-boost regulator and an
  ideal diode.
* One part number for both regulators - one spare fits either.

**How the firmware knows.** VSYS, the FireBeetle's battery input, already
has a 1M/1M divider to GPIO0. It reads 3.82-3.98 V on the cells and
4.17-4.24 V on external power (4.2 V also when a computer is on the
FireBeetle's port, from its charger). `ac_power_classify()` puts the
threshold at 4.08 V, clear of the calibrated ADC error (~40 mV at VSYS) on
both sides; `design.py` checks those margins against the header. On external
power the device runs in CONTINUOUS mode, raises no battery alarms, and books
only the regulator's quiescent current and the resistor leakage against the
cells. Matter: the battery Power Source reports `Status` 1 Active (on the
cells), 2 Standby (external power, cells present) or 3 Unavailable (holder
empty, `BatPresent` false). No second, wired Power Source is declared.

**Why the lockout.** The S9V11E2A keeps running down to 2 V input - 0.33 V
per cell. Long before that the weakest cell of a series string is driven into
reversal; alkaline cells then leak, NiMH cells are damaged. The regulator's
EN pin has a 100 kΩ pull-up to VIN inside and switches off below 0.7 V and on
above 0.8 V (Pololu). With 13 kΩ to GND: off at 6.1 V (1.0 V per cell), on
again only above 7.0 V - with fresh cells, so it cannot oscillate on a
recovering empty pack. The price is the current through those resistors,
77 µA at 8.7 V, about 8 % of the ECO budget. The state-of-charge curves now
end at 1.00 V per cell. After the lockout about 60 µA still flow (pull-up,
R11, divider): **take empty cells out promptly**, especially NiMH (hint from
the battery coordination session).

**Rejected.** A MOSFET that switches the cells' regulator off on external
power: EN low would still draw VIN/100 kΩ ≈ 87 µA through the internal
pull-up, so the backup would last about 40 instead of about 15 months (L91),
for one more discrete part in a module-only chain. For permanent mains
operation the holder can simply stay empty.

**Cost.** EUR 10 (socket + second regulator); the BOM is EUR 171. ECO on L91
3.5 months with margin (NiMH 2.1, alkaline 1.8); as mains backup L91 ~15
months (`docs/BATTERY_LIFE.md`).

**Shared with other projects.** The user's battery devices follow one
standard set by the cross-project battery session: AA cells are never
charged in a device. Devices that need in-device charging use a different
class (2 × 18650 LiFePO4, charged in the device with temperature
monitoring); the Sleeper Frame and the curtain motor moved to that class on
2026-09-19. This AA power block (holder, PTC, 2 × S9V11E2A at 3.90/4.20 V,
LM66200, #6050, 13 kΩ lockout, 1M/220k pack divider, 4.08 V threshold) is
therefore used by AIR CHECK only - one block per unit. `ac_battery` stays
plain C so other projects can reuse its structure.
