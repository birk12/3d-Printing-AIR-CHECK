# ACC-1 carrier board

## Status: schematic complete, layout not done

The electrical design is finished and machine-checked:
`electronics/schematic/design.py` declares every part, every net and every
connection, runs 344 rule checks over them, and emits a KiCad-compatible
netlist (`../schematic/aircheck.net`) plus a readable net list
(`../schematic/NETLIST.md`).

**What is not here is a routed PCB.** There are no Gerbers in
`../gerbers/` because no layout has been done, and shipping an
untested, unrouted board file would be worse than shipping nothing.

## What that means for building one

Nothing, for the RECOMMENDED build. It uses modules on a small piece of
prototyping board or a hand-wired carrier, and `docs/ASSEMBLY.md` lists every
connection explicitly. The design is deliberately small enough to wire by hand:
one boost converter, two load switches, a button, an RGB LED, two connectors
and about a dozen passives.

## If you want to lay it out

1. Import `../schematic/aircheck.net` into KiCad (File > Import > Netlist), or
   redraw the schematic from `../schematic/NETLIST.md` - it is about 25
   components.
2. Board outline: **70 x 35 mm**, 1.6 mm FR4, two layers. Mounting holes on a
   3.5 mm inset to match the standoffs in `cad/openscad/aircheck_params.scad`
   (`PCB_HOLE_INSET`).
3. The Feather sits on 0.1 in female headers along the two long edges, with its
   USB-C connector at the edge that faces the case's USB-C opening (model
   X = 0; the right-hand side seen from the front of the finished device).
4. Layout notes that matter:
   * keep the TPS61023's input capacitor, inductor and output capacitor in a
     tight loop - the SPS30 pulls 80 mA for the first 200 ms of every
     measurement and the loop area is what turns that into radiated noise
   * the 100 uF reservoir goes at the **connector** end of the 5 V run, not at
     the boost
   * the SPS30 UART pair and its 330 R series resistors should not run under
     the boost's switching node
   * the sensor bus goes to GPIO6/GPIO7 (the C6's fixed LP_I2C pads), with its
     4.7k pull-ups to +3V3_SENS, never to 3V3 and never via the Feather's own
     SDA/SCL (EDR-11)
   * the LED (Adafruit 159) stands 8.6 mm off the board face; its common anode
     goes to VBAT
   * ground pour on both layers, stitched around the boost
   * the SPS30's metal shield is internally tied to its GND pin - keep the
     shield floating mechanically, per the datasheet warning about unintended
     currents through it
5. Generate Gerbers into `../gerbers/` and add the fabrication notes here.

## Fabrication notes, when there are Gerbers

* two layers, 1.6 mm FR4, 1 oz copper
* HASL is fine, ENIG if you are reflowing the bare sensors
* minimum trace 0.2 mm, minimum via 0.3/0.6 mm - nothing here is fine pitch
* a stencil is only needed for the CUSTOM PCB variant with bare sensors
