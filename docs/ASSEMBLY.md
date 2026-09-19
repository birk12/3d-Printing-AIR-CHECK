# Assembly

v1.3 has no circuit board to make or order. Everything is a finished module;
you solder wires to module pins and ten through-hole resistors in line, under
heat shrink. An evening's work once the parts and prints are on the desk.

```
PARTS -> PRINT + BAKE -> SET THE REGULATOR -> WIRE -> FLASH -> INTO THE CASE -> CELLS -> PAIR -> SHARE
```

![Inside, lid and door removed](../cad/drawings/open_view.png)

The wiring list, the pin map and where each inline resistor goes are in
[`electronics/schematic/NETLIST.md`](../electronics/schematic/NETLIST.md).
That file is generated from `design.py` and checked by 510 rules; if this
guide and the netlist ever disagree, the netlist wins.

## 1. Parts

Order everything in [`docs/BOM.md`](BOM.md). One Mouser parcel covers the
SEN62, the Sunrise and the battery holder; Berrybase/Eckstein/Reichelt the
rest. Check off:

- [ ] FireBeetle 2 ESP32-C6 (DFR1075)
- [ ] Sensirion SEN62-SIN-T and a 6-way JST GH cable
- [ ] Senseair Sunrise 006-0-0008
- [ ] SparkFun Qwiic SGP40 (SEN-18345) and a Qwiic cable with open leads
- [ ] Seeed Grove SHT40 (101021032) and a Grove-to-jumper cable
- [ ] Pololu #2810 Mini MOSFET Switch LV
- [ ] Pololu #5719 S9V11E2A regulator
- [ ] Adafruit #5830 LM66200 ideal diode
- [ ] MPD BH36AAW 6 × AA holder, Bourns MF-R050 PTC fuse
- [ ] JST PH 2-way pigtail
- [ ] RGB LED 5 mm common anode, SMR1089 panel holder, T 250A push button
- [ ] resistors 1 M, 220 k, 100 k, 2 × 10 k, 2 × 4.7 k, 1 k, 2 × 330 Ω; one 100 nF
- [ ] 6 × M2.5 heat-set inserts; M2.5 × 8 screws (6), M2.5 self-tappers (8),
      M2 self-tappers (6); 26 AWG silicone wire; heat shrink
- [ ] 6 × Energizer Ultimate Lithium L91 (or eneloop pro, or alkaline - one type, one age)

## 2. Print and bake

[`manufacturing/print-settings.md`](../manufacturing/print-settings.md) has
the profile: four parts, no supports. Then **bake all printed parts for 24 h
at 50–60 °C** (oven with a thermometer, door ajar, or a filament dryer). Fresh
PETG gives off volatiles for weeks; the SGP40 would learn that as its
baseline. Let the parts air for a day afterwards.

Check the prints:

- [ ] the lid and the door each drop into the front shell with an even gap
- [ ] the SEN62 slides into its cradle, port face against the left wall (left
      seen from the front)
- [ ] every slot in that wall is open, with a clean bridge roof
- [ ] the gas bay's side-wall and front slots are open (right side, seen from the front)
- [ ] the SHT40 board drops into its fence snugly, the Sunrise between its four guides

## 3. Heat-set inserts

Six M2.5 inserts: four into the lid posts, two into the door posts in the
side walls of the battery compartment. Iron at 220 °C, straight down until
flush, let them cool.

## 4. Set the regulator - before anything is connected to it

1. Solder three wires to the S9V11E2A: VIN, GND, VOUT (leave EN open: it has
   its own pull-up to VIN).
2. Put the holder with six cells on VIN/GND through the PTC fuse (step 5.1).
3. Turn the trimpot until VOUT reads **4.00 V ± 0.03 V** on a multimeter.
   Clockwise raises it. **Above 4.2 V the FireBeetle is out of its rating.**
4. Take the cells out again.

## 5. Wiring

Solder and heat-shrink as you go. Wire colours: red = supply, black = GND,
blue = SDA, yellow = SCL, anything else for control lines.

### 5.1 Power path

| from | to | note |
|---|---|---|
| holder red lead | **F1** PTC fuse | 2 cm from the holder, in line, heat shrink over it |
| F1 | S9V11E2A VIN | through the partition notch |
| holder black lead | S9V11E2A GND | |
| S9V11E2A VOUT | LM66200 **VIN1** | |
| LM66200 **VIN2**, **ON**, GND | GND | all three to ground - this makes VIN1 the only input, always enabled |
| LM66200 VOUT | JST PH pigtail **+** | |
| pigtail **−** | GND | |
| pigtail **+** | Sunrise pin 2 (VBB) and the LED's common anode | spliced onto the + lead |
| pack divider: **R1 1 M** | from S9V11E2A VIN to FireBeetle **IO3** | |
| **R2 220 k** and **C1 100 nF** | IO3 to GND | at the FireBeetle |

**Check the pigtail polarity against the "+" on the FireBeetle** before you
plug it in. JST PH leads are not standardised. Then plug it into the
FireBeetle's battery socket.

### 5.2 SEN62 and its switch

| from | to |
|---|---|
| FireBeetle 3V3 | Pololu #2810 VIN |
| FireBeetle IO2 | #2810 ON |
| #2810 VOUT | SEN62 pins 1 and 6 (VDD) |
| GND | #2810 GND, SEN62 pins 2 and 5 |
| FireBeetle SDA (GPIO19) | SEN62 pin 3; **R3 4.7 k** to #2810 VOUT |
| FireBeetle SCL (GPIO20) | SEN62 pin 4; **R4 4.7 k** to #2810 VOUT |

**Leave the #2810's slide switch in OFF.** Only then does its ON pin have
control. The pull-ups go to the *switched* supply, so nothing feeds the SEN62
while it is off.

### 5.3 Sunrise

Handle it by its PCB tabs, never by the housing or the filter. Solder at
380 °C for at most 2 s per pin, from below (ANO4947).

| Sunrise pin | to |
|---|---|
| 1 GND | GND |
| 2 VBB | pigtail + (5.1) |
| 3 VDDIO | FireBeetle **IO18** |
| 4 SDA | FireBeetle **IO17**; **R5 10 k** across pins 3–4 |
| 5 SCL | FireBeetle **IO21**; **R6 10 k** across pins 3–5 |
| 6 COMSEL | GND (selects I2C) |
| 7 nRDY | nothing |
| 8 DVCC | nothing |
| 9 EN | FireBeetle **IO14**; **R7 100 k** from IO14 to GND at the FireBeetle |

VDDIO and the pull-ups come from GPIO18, which the firmware drives high only
while the sensor is enabled - Senseair forbid any signal on the bus while EN
is low.

### 5.4 SGP40 and SHT40 (always on)

| | SGP40 (Qwiic cable) | SHT40 (Grove cable) | FireBeetle |
|---|---|---|---|
| supply | red 3V3 | red VCC | 3V3 |
| ground | black | black | GND |
| SDA | blue | white | **IO6** |
| SCL | yellow | yellow | **IO7** |

On the SparkFun board **cut the PWR jumper** (its LED would draw more than
the sensor). Leave the I2C jumper closed: its 4.7 k pull-ups are the bus's.
GPIO6/7 are the only pins of the ESP32-C6's low-power I2C.

### 5.5 LED and button

| from | to |
|---|---|
| LED red cathode | **R8 1 k** → IO16 |
| LED green cathode | **R9 330 Ω** → IO22 |
| LED blue cathode | **R10 330 Ω** → IO23 |
| LED common anode (longest lead) | pigtail + (5.1) |
| button | IO1 and GND |

## 6. Flash and smoke test, on the bench

Cells in the holder, the boards on the desk. Plug in USB-C and flash:

```bash
cd firmware
. $HOME/esp/esp-idf/export.sh
. $HOME/esp/esp-matter/export.sh
idf.py set-target esp32c6
idf.py -p /dev/tty.usbmodem* flash monitor
```

Within a few seconds:

```
I (xxx) aircheck: 3D Printing AIR CHECK 1.3.0
I (xxx) aircheck: serial AC-XXXX-XXXX
I (xxx) sgp40: VOC index algorithm at a 10 s sampling interval
I (xxx) aircheck: SEN62 <serial>
I (xxx) sunrise: configuration OK: single mode, 32 samples, ABC on (180 h, 425 ppm)
I (xxx) aircheck: power: 6 x AA alkaline, 0 mWh used; site 0 m = 1013 hPa
I (xxx) ac_matter: endpoints: ...
I (xxx) aircheck: not commissioned; manual code ..., QR MT:...
```

then a CO2 reading (`sunrise: CO2 ... ppm`) and, with USB plugged in, the
SEN62's fan running (continuous mode). If an error line appears instead,
stop here: [`TROUBLESHOOTING.md`](TROUBLESHOOTING.md). It is far easier to
debug on the bench.

Tell the device what it runs on and where it stands:

```bash
python3 tools/configuration/aircheck_config.py --port /dev/tty.usbmodem* set cell_type lithium
python3 tools/configuration/aircheck_config.py --port /dev/tty.usbmodem* set altitude_m 520
```

Press the button once: the air-quality colour for 3 s. Hold it 3 s: blue,
pairing mode. Check red, green and blue each light.

## 7. Into the case

1. **SEN62.** Stick an EPDM frame around each of the two windows on its port
   face (one round both inlets, one round the outlet). This is the one
   gasket in the device, and it is outside the gas bay. Slide the SEN62 into
   its cradle, port face against the left wall, square inlet towards the
   front, and plug in its cable.
2. **Gas bay** (right side seen from the front, walled off): SHT40 into its
   fence, low and next to the vents; SGP40 onto its four posts with M2.5
   self-tappers; Sunrise onto its pedestal, filter towards the lid, pins
   hanging free at both ends. **No tape, glue, foam or hot glue in here.**
   Lead the three cables out over the bay's top wall, where the lid's rib
   closes over them.
3. **FireBeetle** onto its four posts, M2 self-tappers, USB-C in the opening
   in the right-hand wall.
4. **Power modules** into their pockets beside the FireBeetle: #2810 and
   S9V11E2A (trimpot facing the lid), the LM66200 on two M2 self-tappers.
5. **LED holder and button** through the front, nuts from the inside.
6. **Battery holder** into the compartment on its four bosses (M2.5
   self-tappers through the holder's floor), leads at the right-hand end
   seen from the back, out through the partition notch.

Keep every wire out of the SEN62's plug keep-out and away from the lid's
screw posts.

## 8. Close it

Lid: four M2.5 × 8 screws into the inserts, cross pattern, snug - the
inserts spin out of plastic long before a screw strips. Put the cells in
(match the + marks in the holder), then the door with its two screws.

## 9. Pair and share

[`APPLE_HOME.md`](APPLE_HOME.md): scan the QR code, name it, put it in a
room. Print the label with `tools/commissioning/make_label.py` and stick it
on the door. For the dashboard: Home app → the sensor → Accessory Settings →
**Turn On Pairing Mode** → give the code to the dashboard
([`DASHBOARD_INTERFACE.md`](DASHBOARD_INTERFACE.md)).

## 10. Configure

| setting | when |
|---|---|
| `cell_type` | always: `lithium`, `nimh` or `alkaline` - the battery % depends on it |
| `altitude_m` | always: the CO2 pressure correction (1.6 % per 10 hPa) |
| `default_mode` | NORMAL next to a busy printer, if you accept changing cells every 5 weeks |
| `name` | published as Matter NodeLabel so the dashboard can tell units apart |

## Placement

Beside the printer, not inside its enclosure and not in its fan draught
(the SEN62 must not sit in airflow above 1 m/s). Keep the left side (particle
ports) and the right side (gas bay vents) free, out of direct sun and away
from radiators. The CO2 self-calibration needs fresh air about once a week -
a room that is aired now and then is enough (`CALIBRATION.md`).

Two keyholes in the lid, 70 mm apart horizontally, for two pan-head screws
standing about 3 mm proud of the wall.

## Changing cells

Two screws, door off, six new cells of one type and age, door on. The device
notices the higher voltage and resets its energy counter by itself. Never mix
old and new cells, never mix chemistries. If you switch chemistry, set
`cell_type`. Rechargeable cells go into their own charger, outside the
device - nothing charges them in here.
