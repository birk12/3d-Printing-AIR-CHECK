# ACC-1 carrier board

## Status: schematic complete, layout not done

The electrical design is finished and machine-checked:
`electronics/schematic/design.py` declares every part, every net and every
connection, runs 460 rule checks over them, and emits a KiCad-compatible
netlist (`../schematic/aircheck.net`) plus a readable net list
(`../schematic/NETLIST.md`).

**What is not here is a routed PCB.** There are no Gerbers in
`../gerbers/` because no layout has been done, and shipping an
untested, unrouted board file would be worse than shipping nothing.

## What that means for building one

Nothing, for a first unit. The design is deliberately small enough to wire by
hand on a piece of prototyping board: two load switches with their CT
capacitors, a button, an RGB LED, three connectors and about fifteen passives.
`docs/ASSEMBLY.md` and `../schematic/NETLIST.md` list every connection.

## If you want to lay it out (ACC-1 rev C)

1. Import `../schematic/aircheck.net` into KiCad (File > Import > Netlist), or
   redraw the schematic from `../schematic/NETLIST.md` - about 25 components.
2. Board outline: **66 x 35 mm**, 1.6 mm FR4, two layers, with a **10 x 9 mm
   notch** at the top of the USB end for the screw post. Mounting holes at
   (3.5, 3.5), (62.5, 3.5), (62.5, 31.5) and (15.5, 31.5) mm from the
   bottom-left corner - `PCB_HOLES` in `cad/openscad/aircheck_params.scad`.
3. The FireBeetle 2 ESP32-C6 is soldered straight onto the board with its own
   male headers (2.5 mm spacer), its USB-C end flush with the notched end of
   the board, which faces the case's USB-C opening (model X = 0; the
   right-hand side seen from the front). Its two rows are 22.86 mm apart.
4. Layout notes that matter:
   * each TPS22918 gets its CT capacitor right at the CT pin (C2 4.7 nF for
     the SEN63C, C5 1 nF for the SGP40), and QOD (pin 5) tied to VOUT (pin 6)
   * C1 (22 µF) at J1, the SEN63C connector: the module pulls 200 mA pulses
     of 2 ms
   * the SEN63C bus goes to the FireBeetle's SDA/SCL pins (GPIO19/20) with
     R11/R12 to +3V3_SEN6X; the SGP40 bus to GPIO6/GPIO7 (the C6's fixed
     LP_I2C pads) with R6/R7 to +3V3_SENS - never to 3V3 (EDR-11, EDR-14)
   * the LED (Adafruit 159) and the button go on the **front** side, above
     the FireBeetle (PCB-local y = 30.5); the LED stands 8.6 mm off the board
     face and its common anode goes to VBAT
   * J2 (battery in) and the J3 pigtail pads carry VBAT and the cell's
     negative; the negative is **not** GND on this board - it reaches ground
     through the FireBeetle's reverse-polarity FET
   * R13/R14 (USB sense) from the FireBeetle's VIN pin to GPIO18
   * ground pour on both layers
5. Generate Gerbers into `../gerbers/` and add the fabrication notes here.

## Fabrication notes, when there are Gerbers

* two layers, 1.6 mm FR4, 1 oz copper
* HASL is fine
* minimum trace 0.2 mm, minimum via 0.3/0.6 mm - nothing here is fine pitch
* for JLCPCB assembly all SMD parts are standard LCSC parts; choose DDP
  shipping to Germany (EU customs duty since July 2026)
