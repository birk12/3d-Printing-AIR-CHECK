# Assembly

Start to finish this is an evening's work if the parts are on the desk and the
prints are done. Nothing needs a microscope; the finest thing you solder by
hand is an 0603 resistor, and even those are optional if you have the carrier
board assembled.

```
PARTS -> PRINT -> ASSEMBLE -> FLASH -> CHARGE -> PAIR -> SHARE -> USE
```

## 1. Parts

Order everything in `docs/BOM.md`. Check off before you start:

- [ ] Adafruit ESP32-C6 Feather
- [ ] Sensirion SPS30 **and** a JST ZHR-5 cable (the sensor does not come with one)
- [ ] SGP40 breakout, SCD41 breakout
- [ ] Adafruit 159 RGB LED (5 mm, diffused, common anode)
- [ ] 1S LiPo, 4000 mAh, **with a protection circuit**, 6.0 x 60 x 90 mm, JST PH 2.0
- [ ] ACC-1 carrier board, or the parts to build the budget version
- [ ] 8 x M2.5 brass heat-set inserts, 12 x M2.5 x 8 mm screws
- [ ] 2 mm EPDM or acoustic foam strip
- [ ] short female Feather headers (12 + 16 way)

> **Battery safety.** Use a cell with a protection circuit. Not a bare pouch,
> not a salvaged one, not one whose leads you have to strip and re-terminate.
> A 1S LiPo in a sealed plastic box near a machine that runs unattended for
> hours is exactly the situation where the protection circuit earns its keep.

## 2. Print

`manufacturing/print-settings.md` has the profile. Five parts, no supports,
about 8 hours and 150 g of PETG in total. **Print the front shell in white or
natural PETG**: the status LED shines through a 0.6 mm skin of it. With a dark
filament, drill that skin out with a 5 mm bit afterwards.

Before you go further, check the print:

- [ ] the two shells close with an even gap all the way round
- [ ] the button cap moves freely in its counterbore and springs back
- [ ] the SPS30 slides into its cradle
- [ ] hold the front shell up to a light with the bottom towards you: you must
      **not** be able to see from the inlet opening through to the outlet
      opening. If you can, the separating rib did not print cleanly. Fix it
      before you build - a leaking duct makes the PM readings read low and
      there is no way to tell from the numbers.

## 3. Heat-set inserts

Four inserts go into the front shell's corner posts, from the seam side. Iron at
220 C, press straight down until flush, let it cool before you move it. If one
goes in crooked, heat it again and push it the rest of the way with the flat
of the iron.

## 4. Electronics

**Order matters. Do the wiring before anything goes in the case.**

1. Solder the short female headers to the carrier board, then seat the
   Feather. Check it sits square and that the USB-C connector points at the
   left edge of the board.
2. Solder the 6 x 6 x 5 mm tactile switch to the carrier.
3. Solder the RGB LED so its dome stands **8.6 mm proud of the board face**,
   about 0.8 mm higher than it would sit flush. The front shell's LED guide
   tube is a good jig: push the loose LED into the tube, lay the carrier on
   its standoffs, solder from the back. The long lead is the common anode and
   goes to VBAT.
4. Wire the SPS30's ZHR-5 cable to the carrier:
   | SPS30 pin | signal | goes to |
   |---|---|---|
   | 1 | VDD | +5V from the boost |
   | 2 | RX | carrier `SPS30_RX_S` (330 R to GPIO16) |
   | 3 | TX | carrier `SPS30_TX_S` (330 R to GPIO17) |
   | 4 | SEL | **leave unconnected** - floating selects UART |
   | 5 | GND | GND |
   Keep the cable under 10 cm and do not run it alongside the battery leads.
5. Wire the two gas breakouts to the **sensor bus** on the carrier: +3V3_SENS,
   GND, SENS_SDA (GPIO6), SENS_SCL (GPIO7). A STEMMA QT cable chained between
   them is fine; cut one end and solder it to the carrier. **Do not** use the
   Feather's own STEMMA QT port. It is on the gauge bus, which is only
   powered for a few milliseconds every 5 minutes (EDR-11).
6. Check the two 4.7 kΩ pull-ups R6/R7 go to **+3V3_SENS**, not to 3V3.

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
I (xxx) aircheck: 3D Printing AIR CHECK 1.1.0
I (xxx) aircheck: serial AC-XXXX-XXXX
I (xxx) sgp40: VOC index algorithm at a 10 s sampling interval
I (xxx) ac_matter: endpoints: air=1 temperature=2 humidity=3
I (xxx) aircheck: not commissioned; manual code ..., QR MT:...
```

The LED flashes white at boot and then blinks blue: not paired yet. If it does
not, stop here and work through `docs/TROUBLESHOOTING.md`. It is far easier
to debug on the bench than through a screwed-together case.

Wait a minute, then press the button once: the LED shows the air-quality
colour for 3 s (white while it is still warming up). Hold it for 3 seconds and
it blinks blue: pairing mode. Also check each colour channel works. A missing
green or blue usually means the LED was fitted the wrong way round.

## 6. Into the case

1. Lay the 1.5 mm foam strip across the SPS30's port face, then slide the
   sensor into its cradle, **ports facing the bottom wall**, connector facing
   up. It should need light thumb pressure. The foam is what seals the duct;
   without it the inlet and outlet short-circuit inside the case.
2. Screw the carrier board down with four M2.5 x 8 into the printed posts.
   The LED drops into its guide tube.
   Snug, not tight - they are cutting their own thread in plastic.
3. Screw the two gas breakouts into the gas bay: seen from the front it is
   the bottom-right bay, next to the vent slots.
4. Route the SPS30 cable through the pass-through in the divider rib. Do not
   let it sit across the duct openings.
5. Press the button cap into the front face from the outside.

## 7. Battery

1. Put the foam pad in the back shell's battery bay.
2. Lay the cell in **upright**, leads at the bottom edge, and bring them out
   through the notch.
3. Fit the printed battery cover and screw its two tabs into the bosses beside
   the bay. The cell should not be able to slide, and the cover should not be
   squeezing it. The cover also keeps the sensor-bay air off the cell.
4. **Now** plug the JST PH 2.0 into the Feather.
5. Route the lead so it cannot be pinched when the shells close. A pinched
   LiPo lead is the single most likely way to start a fire in this build.

## 8. Close it

Bring the two shells together and drive four M2.5 x 8 screws from the back into
the heat-set inserts. Even pressure, work in a cross pattern, do not
over-torque - the inserts will spin out of hot plastic long before the screw
strips.

## 9. Charge

Plug in USB-C: seen from the front, the port is on the right-hand side. The
Feather's own charge LED lights inside the case. The device switches to
CONTINUOUS mode while it has USB power, which makes it a good reference
instrument on the bench.

At 196 mA a flat 4000 mAh cell takes roughly **24 hours** to fill. That is
slow, and it is a deliberate consequence of using the Feather's built-in
charger rather than adding a second one. You charge this device twice a year.

## 10. Pair

`docs/APPLE_HOME.md`. Scan the QR code, name it, put it in a room.

Print the label from `tools/commissioning/make_label.py` and stick it on the
back before you forget. The sensor has no screen to show the code on.

## 10b. Share with the dashboard

Home app → long-press the sensor → Accessory Settings → **Turn On Pairing
Mode** → copy the code → hand it to the dashboard. Details and everything the
dashboard reads: `docs/DASHBOARD_INTERFACE.md`.

## 11. Configure

Defaults are sensible. The ones worth changing:

| setting | when |
|---|---|
| `default_mode` | NORMAL if this unit sits next to a printer and you will charge it monthly |
| `name` | published as Matter NodeLabel, so the dashboard can tell "3D Printer" from "Room" |
| `ev_sensitivity` | 1 to 5; lower it if your workshop triggers events all day |

## 12. Two units

Build the second one exactly the same way. Commission it separately. Name one
"3D Printer" and one "Room". There is nothing else to do - they are
independent accessories and no pairing step exists.

Put the printer unit **beside** the printer, not inside its enclosure: the
SPS30 must not sit in a forced airflow above 1 m/s, and the case is not rated
for chamber temperatures.

## Servicing

* **Battery replacement:** four screws, lift the back shell, unplug, replace.
  The cell is not glued.
* **Sensor replacement:** all three are on connectors.
* **Fan cleaning:** automatic, once a week, and it is not optional - the SPS30
  resets its own cleaning counter every time it is powered off, and this
  device powers it off after every measurement.
