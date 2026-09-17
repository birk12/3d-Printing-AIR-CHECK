# Assembly

Start to finish this is an evening's work if the parts are on the desk and the
prints are done. Nothing needs a microscope; the finest thing you solder by
hand is an 0603 resistor, and even those are optional if you have the carrier
board assembled.

```
PARTS -> PRINT -> ASSEMBLE -> FLASH -> CHARGE -> PAIR -> CONFIGURE -> USE
```

## 1. Parts

Order everything in `docs/BOM.md`. Check off before you start:

- [ ] Adafruit ESP32-C6 Feather
- [ ] Sensirion SPS30 **and** a JST ZHR-5 cable (the sensor does not come with one)
- [ ] SGP40 breakout, SCD41 breakout
- [ ] 1.54 in e-paper module, 200 x 200, SSD1681
- [ ] 1S LiPo, 4000 mAh, **with a protection circuit**, 6.0 x 60 x 90 mm, JST PH 2.0
- [ ] ACC-1 carrier board, or the parts to build the budget version
- [ ] 12 x M2.5 brass heat-set inserts, 12 x M2.5 x 8 mm screws
- [ ] 2 mm EPDM or acoustic foam strip
- [ ] short female Feather headers (12 + 16 way)

> **Battery safety.** Use a cell with a protection circuit. Not a bare pouch,
> not a salvaged one, not one whose leads you have to strip and re-terminate.
> A 1S LiPo in a sealed plastic box near a machine that runs unattended for
> hours is exactly the situation where the protection circuit earns its keep.

## 2. Print

`manufacturing/print-settings.md` has the profile. Five parts, no supports,
about 11 hours and 160 g of PETG in total.

Before you go further, check the print:

- [ ] the two shells close with an even gap all the way round
- [ ] the display module drops into its four tabs without force
- [ ] the button cap moves freely in its counterbore and springs back
- [ ] the SPS30 slides into its cradle
- [ ] hold the front shell up to a light with the bottom towards you: you must
      **not** be able to see from the inlet opening through to the outlet
      opening. If you can, the separating rib did not print cleanly. Fix it
      before you build - a leaking duct makes the PM readings read low and
      there is no way to tell from the numbers.

## 3. Heat-set inserts

Six inserts go into the front shell's screw posts, from the seam side. Iron at
220 C, press straight down until flush, let it cool before you move it. If one
goes in crooked, heat it again and push it the rest of the way with the flat
of the iron.

## 4. Electronics

**Order matters. Do the wiring before anything goes in the case.**

1. Solder the short female headers to the carrier board, then seat the
   Feather. Check it sits square and that the USB-C connector points at the
   left edge of the board.
2. Solder the 6 x 6 mm tactile switch to the carrier.
3. Wire the SPS30's ZHR-5 cable to the carrier:
   | SPS30 pin | signal | goes to |
   |---|---|---|
   | 1 | VDD | +5V from the boost |
   | 2 | RX | carrier `SPS30_RX_S` (330 R to GPIO16) |
   | 3 | TX | carrier `SPS30_TX_S` (330 R to GPIO17) |
   | 4 | SEL | **leave unconnected** - floating selects UART |
   | 5 | GND | GND |
   Keep the cable under 10 cm and do not run it alongside the battery leads.
4. Wire the two gas breakouts to the switched sensor rail and the I2C bus. A
   STEMMA QT cable chained between them is fine; cut one end and solder it to
   the carrier.
5. Connect the e-paper module's 8-pin header.

> **Do not connect the battery yet.**

## 5. Flash and smoke test

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
I (xxx) aircheck: 3D Printing AIR CHECK 1.0.0
I (xxx) sgp40: VOC index algorithm at a 10 s sampling interval
I (xxx) ac_matter: endpoints: air=1 temperature=2 humidity=3
I (xxx) aircheck: not commissioned; pairing code 3497-011-2332
```

and the e-paper should show the WARMING UP screen. If it does not, stop here
and work through `docs/TROUBLESHOOTING.md` - it is far easier to debug on the
bench than through a screw-together case.

Press the button once: the screen should advance. Hold it 3 seconds: the
pairing screen should appear.

## 6. Into the case

1. Drop the e-paper module into the front shell's four tabs, ribbon towards
   the bottom.
2. Lay the 1.5 mm foam strip across the SPS30's port face, then slide the
   sensor into its cradle, **ports facing the bottom wall**, connector facing
   up. It should need light thumb pressure. The foam is what seals the duct;
   without it the inlet and outlet short-circuit inside the case.
3. Screw the carrier board down with four M2.5 x 8 into the printed posts.
   Snug, not tight - they are cutting their own thread in plastic.
4. Screw the two gas breakouts into the bottom-left bay.
5. Route the SPS30 cable through the pass-through in the divider rib. Do not
   let it sit across the duct openings.
6. Press the button cap into the front face from the outside.

## 7. Battery

1. Put the foam pad in the back shell's battery bay.
2. Lay the cell in with its leads at the bottom edge, and bring them out
   through the notch.
3. Fit the printed battery clip and screw it down. The cell should not be able
   to slide, and the clip should not be squeezing it.
4. **Now** plug the JST PH 2.0 into the Feather.
5. Route the lead so it cannot be pinched when the shells close. A pinched
   LiPo lead is the single most likely way to start a fire in this build.

## 8. Close it

Bring the two shells together and drive six M2.5 x 8 screws from the back into
the heat-set inserts. Even pressure, work in a cross pattern, do not
over-torque - the inserts will spin out of hot plastic long before the screw
strips.

## 9. Charge

Plug in USB-C. The Feather's charge LED comes on; the screen shows CHARGING
and the device switches to CONTINUOUS mode while it has mains power.

At 196 mA a flat 4000 mAh cell takes roughly **24 hours** to fill. That is
slow, and it is a deliberate consequence of using the Feather's built-in
charger rather than adding a second one. You charge this device twice a year.

## 10. Pair

`docs/APPLE_HOME.md`. Scan the QR code, name it, put it in a room.

Print the label from `tools/commissioning/make_label.py` and stick it on the
back before you forget - the QR code is much easier to scan off the case than
off a screen.

## 11. Configure

Defaults are sensible. The ones worth changing:

| setting | when |
|---|---|
| `default_mode` | NORMAL if this unit sits next to a printer and you will charge it monthly |
| `name` | so the two units differ on screen as well as in Apple Home |
| `ev_sensitivity` | 1 to 5; lower it if your workshop triggers events all day |
| `display_timeout_s` | how long after a button press the device keeps refreshing |

## 12. Two units

Build the second one exactly the same way. Commission it separately. Name one
"3D Printer" and one "Room". There is nothing else to do - they are
independent accessories and no pairing step exists.

Put the printer unit **beside** the printer, not inside its enclosure: the
SPS30 must not sit in a forced airflow above 1 m/s, and the case is not rated
for chamber temperatures.

## Servicing

* **Battery replacement:** six screws, lift the back shell, unplug, replace.
  The cell is not glued.
* **Sensor replacement:** all three are on connectors.
* **Fan cleaning:** automatic, once a week, and it is not optional - the SPS30
  resets its own cleaning counter every time it is powered off, and this
  device powers it off after every measurement.
