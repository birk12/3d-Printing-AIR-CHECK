# Assembly

v1.3.1 has no circuit board to make or order. Everything is a finished module;
you solder wires to module pins and eleven through-hole resistors in line, under
heat shrink. An evening's work once the parts and prints are on the desk.

```
PARTS -> PRINT + BAKE -> SET THE REGULATORS -> WIRE -> FLASH -> INTO THE CASE -> CELLS -> PAIR -> SHARE
```

![Inside, lid and door removed](../cad/drawings/open_view.png)

The wiring list, the pin map and where each inline resistor goes are in
[`electronics/schematic/NETLIST.md`](../electronics/schematic/NETLIST.md).
That file is generated from `design.py` and checked by 567 rules; if this
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
- [ ] 2 × Pololu #5719 S9V11E2A regulator (PS1 for the cells, PS2 for the USB-C power socket)
- [ ] Adafruit #5830 LM66200 ideal diode
- [ ] Adafruit #6050 sunken USB-C breakout (J2, the power socket)
- [ ] MPD BH36AAW 6 × AA holder, Bourns MF-R050 PTC fuse
- [ ] JST PH 2-way pigtail
- [ ] RGB LED 5 mm common anode, SMR1089 panel holder, T 250A push button
- [ ] resistors 1 M, 220 k, 100 k, 13 k, 2 × 10 k, 2 × 4.7 k, 1 k, 2 × 330 Ω; one 100 nF
- [ ] 6 × M2.5 heat-set inserts; M2.5 × 8 screws (6), M2.5 self-tappers (8),
      M2 self-tappers (8: two of them for J2); 26 AWG silicone wire; heat shrink
- [ ] 6 × Energizer Ultimate Lithium L91 (or eneloop pro, or alkaline - one type, one age)
- [ ] for continuous operation: any CE-marked USB-C charger (an 18 W phone
      charger is plenty) and a USB-C cable

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

## 4. Set the regulators - before anything is connected to them

Two identical S9V11E2A modules, set to two different voltages. Mark them
(PS1, PS2) as soon as they are set.

**PS1, the cells' regulator: 3.90 V**

1. Solder three wires to it: VIN, GND, VOUT. Solder **R11 13 k** directly
   across its **EN** and **GND** pins, on the module, under heat shrink. With
   the module's internal 100 k pull-up this is the undervoltage lockout: off
   below ~6.1 V pack (1.0 V per cell), on again only above ~7.0 V.
2. Put the holder with six **fresh** cells on VIN/GND through the PTC fuse
   (step 5.1). With R11 fitted it does not start from a pack below ~7.0 V: if
   VOUT stays at 0 V, check the cells first.
3. Turn the trimpot until VOUT reads **3.90 V ± 0.03 V** on a multimeter.
   Clockwise raises it. Not higher: above about 4.04 V (the firmware's 4.08 V
   threshold minus the ADC's error) the device may take its own cells for
   external power.
4. Take the cells out again.

**PS2, the USB-C power branch: 4.20 V**

1. Solder J2 **VBUS** to PS2 **VIN**, J2 **GND** to PS2 **GND**, and a lead to
   PS2 **VOUT**. PS2's EN stays open. J2's data, CC and SBU pins stay open.
2. Plug a USB-C charger into J2.
3. Turn the trimpot until VOUT reads **4.20 V ± 0.03 V**. Never above 4.23 V:
   the FireBeetle's battery input is rated to 4.25 V (NETLIST.md). Not below
   4.17 V: PS2 has to stay well above PS1 for the ideal diode to prefer it.
4. Unplug the charger.

## 5. Wiring

Solder and heat-shrink as you go. Wire colours: red = supply, black = GND,
blue = SDA, yellow = SCL, anything else for control lines.

### 5.1 Power path

| from | to | note |
|---|---|---|
| holder red lead | **F1** PTC fuse | 2 cm from the holder, in line, heat shrink over it |
| F1 | PS1 VIN | through the partition notch |
| holder black lead | PS1 GND | |
| PS1 VOUT | LM66200 **VIN1** | the cells' branch, 3.90 V |
| PS1 EN - **R11 13 k** - PS1 GND | | on the module (step 4) |
| J2 VBUS | PS2 VIN | 5 V from the charger (step 4) |
| J2 GND, PS2 GND | GND | |
| PS2 VOUT | LM66200 **VIN2** | the USB-C branch, 4.20 V. **VIN2 no longer goes to GND** (it did up to v1.3) |
| LM66200 **ON**, GND | GND | ON low = always enabled; the higher of VIN1 and VIN2 feeds VOUT |
| LM66200 VOUT | JST PH pigtail **+** | |
| pigtail **−** | GND | |
| pigtail **+** | Sunrise pin 2 (VBB) and the LED's common anode | spliced onto the + lead |
| pack divider: **R1 1 M** | from PS1 VIN to FireBeetle **IO3** | |
| **R2 220 k** and **C1 100 nF** | IO3 to GND | at the FireBeetle |

**Check the pigtail polarity against the "+" on the FireBeetle** before you
plug it in. JST PH leads are not standardised. Then plug it into the
FireBeetle's battery socket.

USB 5 V never touches the pack: J2 only reaches PS2, and the LM66200 keeps
both branches apart. The cells are never charged, also not on a charger.

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
I (xxx) aircheck: 3D Printing AIR CHECK 1.3.1
I (xxx) aircheck: serial AC-XXXX-XXXX
I (xxx) sgp40: VOC index algorithm at a 10 s sampling interval
I (xxx) aircheck: SEN62 <serial>
I (xxx) sunrise: configuration OK: single mode, 32 samples, ABC on (180 h, 425 ppm)
I (xxx) aircheck: power: 6 x AA alkaline, 0 mWh used; site 0 m = 1013 hPa
I (xxx) ac_matter: endpoints: ...
I (xxx) aircheck: not commissioned; manual code ..., QR MT:...
```

then a CO2 reading (`sunrise: CO2 ... ppm`), `power: external, cells as
backup (VSYS 4.2x V, pack ... V)` (the computer on USB counts as external
power) and the SEN62's fan running (continuous mode). If an error line appears instead,
stop here: [`TROUBLESHOOTING.md`](TROUBLESHOOTING.md). It is far easier to
debug on the bench.

Tell the device what it runs on and where it stands:

```bash
python3 tools/configuration/aircheck_config.py --port /dev/tty.usbmodem* set cell_type lithium
python3 tools/configuration/aircheck_config.py --port /dev/tty.usbmodem* set altitude_m 520
```

Press the button once: the air-quality colour for 3 s. Hold it 3 s: blue,
pairing mode. Check red, green and blue each light.

Then the power sources (TESTING T-P7..T-P9): a charger in J2 as well - the
log stays at `external, cells as backup`; unplug the computer, then the
charger - `power: cells`, without a reset.

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
   PS1 (trimpot facing the lid), the LM66200 on two M2 self-tappers, PS2
   next to the LM66200, near the top wall.
5. **J2**, the USB-C power socket, onto its two posts at the top wall with two
   M2 self-tappers, socket in the opening in the top wall, towards the
   right-hand side seen from the front (above the FireBeetle's own USB-C).
   The screws take the plugging force, not the solder joints.
6. **LED holder and button** through the front, nuts from the inside.
7. **Battery holder** into the compartment on its four bosses (M2.5
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
| `default_mode` | NORMAL next to a busy printer, if you accept changing cells every 5 weeks. On a USB-C charger it does not matter: the device runs CONTINUOUS |
| `name` | published as Matter NodeLabel so the dashboard can tell units apart |

## Running from a USB-C charger

Plug any USB-C charger into J2, the socket in the top wall (an 18 W phone
charger is plenty; the device draws at most ~0.6 A at 5 V). J2 asks for plain
5 V, no negotiation. The device then runs CONTINUOUS, like on a computer, and
the cells are the backup: pull the charger and it carries on from them
without a gap. Nothing charges them.

Left in as the backup, the cells last about 15 months (L91), 10 (eneloop
pro) or 9 (alkaline) (`docs/BATTERY_LIFE.md`). For permanent mains operation
the holder may stay empty; the device does not raise a battery alarm then.

The FireBeetle's own USB-C stays for configuration and updates.

## Placement

Beside the printer, not inside its enclosure and not in its fan draught
(the SEN62 must not sit in airflow above 1 m/s). Keep the left side (particle
ports) and the right side (gas bay vents) free, out of direct sun and away
from radiators. The CO2 self-calibration needs fresh air about once a week -
a room that is aired now and then is enough (`CALIBRATION.md`).

Two keyholes in the lid, 70 mm apart horizontally, for two pan-head screws
standing about 3 mm proud of the wall.

## Changing cells

The yellow low-battery blink and Matter's `BatReplacementNeeded` mean
**change the cells now**. Two screws, door off, six new cells of one type and
age, door on. The device notices the higher voltage and resets its energy
counter by itself. Never mix old and new cells, never mix chemistries. If you
switch chemistry, set `cell_type`. Rechargeable cells go into their own
charger, outside the device - nothing charges them in here.

**Take empty cells out promptly - especially NiMH; for L91 it is
uncritical.** The undervoltage lockout switches the device off at ~1.0 V per
cell, but ~55 µA still flow through PS1's EN pull-up and R11, plus ~5 µA
through the pack divider. Over weeks that can take a NiMH pack below 1.0 V
per cell and drive the weakest cell into reversal.

With a charger in J2 the cells can be changed without switching anything
off.
