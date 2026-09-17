# Engineering decision record

One entry per choice that would be expensive to reverse. Each says what was
picked, what it was picked over, and what it costs.

---

## EDR-1: ESP32-C6, not ESP32-H2

**Decision.** ESP32-C6-MINI-1, on an Adafruit ESP32-C6 Feather.

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

**Why the C6 anyway.** 512 kB of SRAM against the H2's 320 kB. This node
carries four endpoints, six clusters on one of them, a 5 kB framebuffer, a
5.6 kB history structure and the Matter stack. The H2 can do it; the C6 does
it with room to change something later. The second reason is boring and
decisive for a DIY project: there is a well-supported, in-production Feather
board with a USB-C connector, a LiPo charger and a MAX17048 fuel gauge already
on it for the C6, and there is not one for the H2. Building those three blocks
badly would cost more than 70 uA.

**Cost.** About 8 % of the battery life. Documented, not hidden. If someone
later spins the custom carrier with a bare module on it, the H2 is the right
choice and nothing above `ac_hal` changes.

---

## EDR-2: the SPS30 talks UART, not I2C

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

## EDR-3: e-paper, not OLED or memory LCD

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

## EDR-4: the SCD41 is power cycled, and we implement our own ASC

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

## EDR-5: VOC every 10 s, PM every 4 hours

**Decision.** In ECO, the cheap channel watches continuously and the expensive
channel measures on demand.

**The numbers.** One 40 s SPS30 window costs about 0.9 mAh. A VOC sample in
Sensirion's low-power sequence costs about 0.017 mAh - fifty times less. Four
PM windows a day is already the biggest line in the budget.

**So.** VOC samples every 10 seconds (the upper end of the interval the Gas
Index Algorithm is validated at), and a VOC excursion escalates the device
into ACTIVE, where PM samples every two minutes. Printing raises VOC long
before a four-hourly PM sample would have noticed anything.

**Cost.** A particle event that produces no VOC - sweeping, a dusty draught -
can be missed in ECO. That is the honest limitation and it is in
`docs/BATTERY_LIFE.md` in as many words. NORMAL mode removes it at the cost of
roughly three weeks of runtime instead of six months.

---

## EDR-6: load switches on rails, not sensor sleep commands

**Decision.** Three TPS22918 load switches; the SPS30 rail is gated on the
boost converter's *input*.

**Alternative.** Leave everything powered and use each sensor's own sleep
command. The SPS30 sleeps at 38 uA, the SGP40 idles at 34 uA, the SCD41's
power_down is in the microamps.

**Why not.** 38 uA at 5 V through a 92 % efficient boost is 54 uA from the
battery, plus the TPS61023's own ~19 uA quiescent, plus the SGP40's 34 uA.
That is about 2.6 mAh a day of doing nothing, on a budget of roughly 15. It is
also 38 uA that Sensirion specify as *typical* with a maximum of 50.

**Also.** A load switch is the only reliable way out of a hung I2C sensor.
A device that has to be opened up and unplugged to recover is not a product.

**Cost.** Three parts, three GPIOs, and the SPS30 has to be given its 120 ms
to boot on every wake.

---

## EDR-7: modules on a carrier board, not a single custom PCB

**Decision.** An Adafruit Feather plus breakout modules on a simple two-layer
carrier, with the all-SMD version documented as an option.

**Why.** The brief asks for the fastest reliable first build and says to
prefer modular electronics unless a custom PCB clearly wins. It does not
clearly win here: the three blocks a custom board would have to get right -
USB-C, LiPo charging and fuel gauging - are exactly the three that are
hardest to debug without equipment, and they already exist, tested, on a
EUR 22 board.

**Cost.** About 8 mm of case depth and EUR 18 against the all-SMD variant.
Both are in `docs/BOM.md` and both use the same enclosure.

---

## EDR-8: the SPS30's ports face down, into a sealed split duct

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

**Cost.** The case is 114 mm tall, taller than the brief's 70-100 mm target,
because the sensor bay and the 60 x 90 mm cell cannot share the same band.

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
`docs/APPLE_HOME.md`, in the code, and on the device's own screen, which
labels the value "VOC INDEX" and prints "100 = 24 h average" next to it. The
config flag `voc_publish_index_as_ppb` turns it off for anyone who would
rather have no number than a mislabelled one.

**Cost.** A number in Apple Home labelled "VOC" whose unit is wrong. This is
the least comfortable decision in the project and it is deliberately the
easiest one to reverse.
