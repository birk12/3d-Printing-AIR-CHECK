# Print settings

Four printed parts. Nothing needs support. Everything fits a 250 x 210 mm bed.

| part | file | orientation | material | filament |
|---|---|---|---|---|
| Front shell (with the battery compartment and the charger chamber) | `cad/stl/aircheck_front.stl` | **front face down on the bed**, as exported | PETG | 87–169 g |
| Lid (over the electronics) | `cad/stl/aircheck_back.stl` | **outer face down**, as exported | PETG | 33–65 g |
| Battery door (retaining ribs, NTC finger, engraved label) | `cad/stl/aircheck_door.stl` | **outer face down**, as exported | PETG | 32–62 g |
| Desk stand (optional) | `cad/stl/aircheck_stand.stl` | on its back face, as exported | PETG | 21–42 g |

Filament figures are from `tools/diagnostics/stl_check.py` for the v1.4
case (136 × 174 × 34 mm): the lower number at 25 % infill, the upper one
solid. The 2.4 mm walls print as six perimeters, so the real figure sits
towards the upper end. For print times, your slicer's number is the real
one.

The front shell's colour no longer matters: the LED sits in a panel holder
in a real hole since v1.3.

## Bake-out - required

Fresh PETG gives off residual monomers and additives for weeks. The SGP40
would learn them as "clean air", and the VOC index would be skewed until they
are gone. So, before assembly:

1. **24 h at 50–60 °C** - kitchen oven with a separate thermometer (their
   dials are ±20 °C), door ajar, or a filament dryer. PETG softens near 80 °C;
   stay below 65 °C.
2. Let the parts air at room temperature for another day.
3. No painting, no lacquer, no labels with solvent glue inside the gas bay.

## Profile

| setting | value | why |
|---|---|---|
| nozzle | 0.4 mm | everything is dimensioned for it |
| layer height | 0.2 mm | 0.15 mm looks better on the bezel chamfer and costs ~40 % more time |
| perimeters | 4 (walls are 2.4 mm, so they fill anyway) | |
| top / bottom layers | 5 / 5 | |
| infill | 25 % gyroid | only the screw posts and the stand have any bulk |
| supports | **none** | nothing overhangs more than 45 deg except the roofs of the slots in the side walls and the USB-C opening, which are bridges of at most 13 mm. `stl_check.py` measures 1.2 % overhang on the front shell, 1.0 % on the lid and 1.5 % on the door |
| bridges | your slicer's bridge settings, fan 100 % | the SEN62 port slots and the gas-bay side vents are 2.4 mm tall slots whose roofs print as bridges |
| brim | 5 mm on the front shell if your bed adhesion is marginal | 222 cm2 of bed contact, it should not need one |
| seam | "aligned" or "rear" | there is a vertical corner at each of the four case corners for it to hide in |

## Materials

* **PETG** - the only material for the final build. Tough, takes heat-set
  inserts well. Print at 235 / 80 C. Bake it out (above).
* **PLA** - fine for a first fit check. Do **not** use it for the final build:
  the front shell and the door enclose the cells and the charger, and the
  Power-Standard allows no PLA there; the case also sits near a 3D printer,
  and PLA softens around 55 C. Heat-set inserts in PLA also relax.
* **ABS / ASA** - not for this device: they keep emitting styrene for a long
  time and the SGP40 sits right next to them.

## Heat-set inserts

Six M2.5 brass inserts: four into the lid posts, two into the door posts in
the battery compartment's side walls, all entered from the seam side. Hole
3.84 mm diameter, 6 mm deep - sized for PETG per the manufacturer chart. Iron
at 220 C, press until flush, keep it square.

The modules (FireBeetle, SGP40, LM66200, the #6091 charger, the J2 USB-C
socket) and the two battery holders (M3) use **direct self-tapping screws**
into printed cores: fitted once, never touched again. J2 is the one that sees force in use, every time
a charger is plugged in: the screws take it, not the solder joints.

## What to check on the first print

1. Do the lid and the door each drop in with an even shadow gap?
2. Does the SEN62 slide into its cradle, port face against the left wall
   (seen from the front)? With its EPDM frames on it should need light thumb
   pressure.
3. Are all port slots in the left wall open, with clean bridge roofs? A
   sagging roof narrows the slot and cuts the open area Sensirion require.
4. Are the gas-bay slots (right wall and front) open?
5. Does the SHT40 board sit snug in its fence, the Sunrise between its guides
   without touching the bay walls?
6. Do the LED holder (8.2 mm) and the button (7.2 mm) go through their holes,
   and do their nuts sit flat on the spot faces inside?
7. Does a USB-C plug seat fully in J2 through the opening in the top wall,
   with J2 screwed to its two posts, and is that opening's roof clean?
8. Battery compartment: do the two holders sit flat on their bosses and pads,
   the #6091 on its four posts, and are the charger chamber's vents open (in
   the bottom wall and high in the side wall)?
9. Does the door close over the cells with its ribs and the NTC finger clear
   of them, and is the engraved label legible?
