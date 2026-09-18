# Assembly

Start to finish this is an evening's work if the parts are on the desk and the
prints are done. Nothing needs a microscope; the finest thing you solder by
hand is an 0603 resistor or a SOT-23, and even those are optional if you order
the carrier board assembled.

```
PARTS -> PRINT -> ASSEMBLE -> FLASH -> CHARGE -> PAIR -> SHARE -> USE
```

![Inside, lid and battery removed](../cad/drawings/open_view.png)

## 1. Parts

Order everything in `docs/BOM.md`. One Mouser parcel covers the SEN63C, the
load switches, the connectors and the RGB LED. Check off before you start:

- [ ] DFRobot FireBeetle 2 ESP32-C6 (DFR1075), with its male headers
- [ ] Sensirion SEN63C-SIN-T **and** a 6-pin JST GH cable, pin 1 to pin 1 (the
      sensor does not come with one)
- [ ] Adafruit SGP40 breakout (4829) and a STEMMA QT cable
- [ ] Adafruit 159 RGB LED (5 mm, diffused, **common anode**)
- [ ] 1S LiPo, 4000 mAh, **with a protection circuit**, 6.0 x 60 x 90 mm, JST PH 2.0
- [ ] ACC-1 rev C carrier board (bare or assembled) and its parts
- [ ] a 2-pin JST PH cable, 100 mm, for carrier to FireBeetle
- [ ] 4 x M2.5 brass heat-set inserts, 4 x M2.5 x 10 mm screws, 6 x M2.5 x 6
      self-tapping screws for the carrier and the breakout
- [ ] 1.5-2 mm EPDM or PU foam strip, self-adhesive

> **Battery safety.** Use a cell with a protection circuit. Not a bare pouch,
> not a salvaged one, not one whose leads you have to strip and re-terminate.
> A 1S LiPo in a sealed plastic box near a machine that runs unattended for
> hours is exactly the situation where the protection circuit earns its keep.

## 2. Print

`manufacturing/print-settings.md` has the profile. Five parts, no supports,
about 8 hours and 160 g of PETG in total. **Print the front shell in white or
natural PETG**: the status LED shines through a 0.6 mm skin of it. With a dark
filament, drill that skin out with a 5 mm bit afterwards.

Before you go further, check the print:

- [ ] the lid drops into the front shell with an even gap all the way round
- [ ] the button cap moves freely in its counterbore and springs back
- [ ] the SEN63C slides into its cradle, port face against the left wall
      (left as seen from the front; in the model it is the X = max wall)
- [ ] the port slots in that wall are open all the way through - the inlet
      window (one column) near the bottom, the outlet window (two columns)
      above it
- [ ] the battery cover slides over the lid's battery bay walls and stays

## 3. Heat-set inserts

Four inserts go into the front shell's corner posts, from the seam side. Iron at
220 C, press straight down until flush, let it cool before you move it. If one
goes in crooked, heat it again and push it the rest of the way with the flat
of the iron.

## 4. Carrier board

Skip steps 1-3 if you ordered it assembled.

1. Solder the SMD parts: both TPS22918 (SW1, SW2), their CT capacitors C2
   (4.7 nF) and C5 (1 nF), C1 22 µF, C4 and C7 1 µF, C6 100 nF, and the
   resistors. **Check the CT capacitors are there**: without them, switching a
   sensor rail on pulls an amp-level spike out of the FireBeetle's 3.3 V and
   can reset the chip.
2. Solder the JST GH header J1 (SEN63C) and the JST PH header J2 (battery in).
3. Solder the 6 x 6 x 5 mm tactile switch to the front side.
4. Solder the RGB LED so its dome stands **8.6 mm proud of the board face**.
   The front shell's LED guide tube is a good jig: push the loose LED into the
   tube, lay the carrier on its standoffs, solder from the back. The long lead
   is the common anode and goes to VBAT.
5. **Measure the FireBeetle's JST PH socket** before soldering the board down:
   it must be no taller than 6.0 mm above the FireBeetle, or it will touch the
   battery cover (bench test B2).
6. Solder the FireBeetle **straight onto the carrier** with its own male
   headers, component side away from the carrier, USB-C flush with the
   carrier's notched end. No female sockets: they would add 5 mm the case does
   not have.

## 5. Wiring

| from | to | notes |
|---|---|---|
| SEN63C connector | J1, 6-pin GH cable | pin 1 to pin 1. Keep it under 15 cm |
| SGP40 breakout | carrier +3V3_SENS, GND, SENS_SDA, SENS_SCL | cut one end of a STEMMA QT cable and solder it to the carrier pads. SDA goes to GPIO6, SCL to GPIO7 - those are the only pads the C6's low-power I2C can use |
| J3, 2-pin JST PH pigtail | carrier J3 pads -> FireBeetle battery socket | **check the polarity** against the "+" mark on the FireBeetle. JST PH leads are not standardised, and the wrong way round the FireBeetle's reverse-polarity FET is the only thing between you and a dead board |

Everything else - VIN for the USB sense, the SEN63C bus on the FireBeetle's
SDA/SCL pins, the LED and button GPIOs - runs through the header pins the
FireBeetle is soldered on with.

Check before you go on:

- [ ] R6/R7 go to **+3V3_SENS** and R11/R12 to **+3V3_SEN6X**, not to 3V3
- [ ] nothing on the carrier connects to the FireBeetle's GPIO15 (its own LED)
      or GPIO9 (BOOT)

> **Do not connect the battery yet.**

## 6. Flash and smoke test

With the boards still outside the case, plug in USB-C and flash:

```bash
cd firmware
. $HOME/esp/esp-idf/export.sh
. $HOME/esp/esp-matter/export.sh
idf.py set-target esp32c6
idf.py -p /dev/tty.usbmodem* flash monitor
```

You should see, within a few seconds:

```
I (xxx) aircheck: 3D Printing AIR CHECK 1.2.0
I (xxx) aircheck: serial AC-XXXX-XXXX
I (xxx) sgp40: VOC index algorithm at a 10 s sampling interval
I (xxx) aircheck: SEN63C <serial>, CO2 self calibration on
I (xxx) ac_matter: endpoints: air=1 temperature=2 humidity=3
I (xxx) aircheck: not commissioned; manual code ..., QR MT:...
```

and, a few seconds later, the SEN63C's fan for about 40 s and then:

```
I (xxx) sen6x: window 40s: PM2.5 3.1 ug/m3 (10 samples), CO2 512 ppm, 22.8 C, 45 %RH
```

The LED flashes white at boot and then blinks blue: not paired yet. If it does
not, stop here and work through `docs/TROUBLESHOOTING.md`. It is far easier
to debug on the bench than through a screwed-together case.

Wait a minute, then press the button once: the LED shows the air-quality
colour for 3 s (white while it is still warming up). Hold it for 3 seconds and
it blinks blue: pairing mode. Also check each colour channel works. A missing
green or blue usually means the LED was fitted the wrong way round.

## 7. Into the case

1. Cut the foam into a frame for each of the two windows on the SEN63C's port
   face - one around both inlets, one around the outlet - and stick them on the
   sensor. The printed ribs on the inside of the wall press into this foam.
   **This seal is not optional**: Sensirion require inlets and outlet to be
   sealed from each other and from the inside of the device, or the sensor
   draws air from inside the case and the readings are meaningless.
2. Slide the SEN63C into its cradle: port face against the left wall, cable
   side towards the middle of the case, the end with the two small inlets at
   the bottom and the **square inlet towards the front** of the case. It
   should need light thumb pressure.
3. Plug in its cable, and lay a strip of foam on its back face; the lid presses
   on it.
4. Screw the carrier board down with four M2.5 self-tappers into the printed
   posts. The LED drops into its guide tube, and the FireBeetle's USB-C lines
   up with the opening in the right-hand wall.
   Snug, not tight - they are cutting their own thread in plastic.
5. Screw the SGP40 breakout into the gas bay: seen from the front it is the
   bottom-right bay, behind the vent slots.
6. Route the SGP40 cable through the pass-through in the gas bay's top rib and
   close the gap round it with a piece of foam.
7. Lay a strip of foam along the top edges of the gas bay's two ribs. The
   battery cover presses on it and seals the bay from behind.
8. Press the button cap into the front face from the outside.

## 8. Battery

1. Put a thin foam pad on the floor of the lid's battery bay, or a strip of
   double-sided foam tape.
2. Lay the cell in **upright**, leads at the top, and bring them out through
   the notch in the bay wall.
3. Slide the printed battery cover over the bay. Its skirt grips the two long
   walls; nothing is screwed. It keeps the cell in place and puts plastic
   between the pouch and the FireBeetle.
4. **Now** plug the cell into J2 on the carrier.
5. Route the lead so it cannot be pinched when the lid closes. A pinched LiPo
   lead is the single most likely way to start a fire in this build.

## 9. Close it

Lower the lid onto the front shell and drive four M2.5 x 10 screws from the
back into the heat-set inserts. Even pressure, work in a cross pattern, do not
over-torque - the inserts will spin out of hot plastic long before the screw
strips.

## 10. Charge

Plug in USB-C: seen from the front, the port is on the right-hand side. The
FireBeetle's charge LED lights inside the case. The device switches to
CONTINUOUS mode while it has USB power (the SEN63C then runs non-stop), which
makes it a good reference instrument on the bench.

At 540 mA a flat 4000 mAh cell takes about **8 hours**. You charge this
device every three months.

## 11. Pair

`docs/APPLE_HOME.md`. Scan the QR code, name it, put it in a room.

Print the label from `tools/commissioning/make_label.py` and stick it on the
back before you forget. The sensor has no screen to show the code on.

## 11b. Share with the dashboard

Home app → long-press the sensor → Accessory Settings → **Turn On Pairing
Mode** → copy the code → hand it to the dashboard. Details and everything the
dashboard reads: `docs/DASHBOARD_INTERFACE.md`.

## 12. Configure

Defaults are sensible. The ones worth changing:

| setting | when |
|---|---|
| `default_mode` | NORMAL if this unit sits next to a printer and you will charge it every few weeks |
| `name` | published as Matter NodeLabel, so the dashboard can tell "3D Printer" from "Room" |
| `ev_sensitivity` | 1 to 5; lower it if your workshop triggers events all day |

## 13. Two units

Build the second one exactly the same way. Commission it separately. Name one
"3D Printer" and one "Room". There is nothing else to do - they are
independent accessories and no pairing step exists.

Put the printer unit **beside** the printer, not inside its enclosure, and
not in the printer's own fan draught: the SEN63C must not sit in a forced
airflow above 1 m/s, and the case is not rated for chamber temperatures. Keep
the left side (the ports) free and out of direct sunlight.

## Wall mounting

Two keyholes one above the other, 40 mm apart, near the right-hand edge as
seen from the front. Two pan-head screws, heads about 3 mm proud of the wall.

## Servicing

* **Battery replacement:** four screws, lift the lid, unplug from J2, slide
  the cover off, replace.
* **Sensor replacement:** both sensors are on connectors.
* **Fan cleaning:** none needed. The SEN6x keeps its optics clean with a
  sheath flow, and Sensirion removed automatic cleaning from the family.
