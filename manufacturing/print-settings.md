# Print settings

Five printed parts. Nothing needs support. Everything fits a 250 x 210 mm bed.

| part | file | orientation | material | est. time | filament |
|---|---|---|---|---|---|
| Front shell | `cad/stl/aircheck_front.stl` | **display face down on the bed**, as exported | PETG | ~5 h 30 | ~77 g |
| Back shell | `cad/stl/aircheck_back.stl` | **outer back face down**, as exported | PETG | ~4 h 30 | ~66 g |
| Button cap | `cad/stl/aircheck_button.stl` | disc face down | PETG | ~3 min | <1 g |
| Battery clip | `cad/stl/aircheck_batclip.stl` | flat, as exported | PETG | ~20 min | ~3 g |
| Desk stand | `cad/stl/aircheck_stand.stl` | on its back face, as exported | PETG | ~1 h 15 | ~16 g |

Filament figures are the solid volume from `tools/diagnostics/stl_check.py`
multiplied by the PETG density of 1.27 g/cm3. Both shells are 2.4 mm walls,
which is six perimeters at a 0.4 mm nozzle - they print essentially solid, so
infill barely changes the number. Times are estimates from the volume and a
typical 0.4 mm / 0.2 mm profile; your slicer's number is the real one.

## Profile

| setting | value | why |
|---|---|---|
| nozzle | 0.4 mm | everything is dimensioned for it |
| layer height | 0.2 mm | 0.15 mm looks better on the bezel chamfer and costs ~40 % more time |
| perimeters | 4 (walls are 2.4 mm, so they fill anyway) | |
| top / bottom layers | 5 / 5 | |
| infill | 25 % gyroid | only the screw posts and the stand have any bulk |
| supports | **none** | the model is designed so that nothing overhangs more than 45 deg. `stl_check.py` measures 1.6 % overhang on the front shell and 0.4 % on the back shell, all of it self-supporting chamfers |
| brim | 5 mm on the front shell if your bed adhesion is marginal | 126 cm2 of bed contact, it should not need one |
| seam | "aligned" or "rear" | there is a vertical corner at each of the four case corners for it to hide in |

## Materials

* **PETG** - the recommended choice. Tough, takes heat-set inserts well, does
  not creep under the battery clip's preload. Print at 235 / 80 C.
* **PLA** - fine for a first fit check. Do **not** use it for the final build:
  the case sits near a 3D printer, and PLA softens around 55 C. Heat-set
  inserts in PLA also relax.
* **ABS / ASA** - works, and is the right pick if the device will sit in an
  enclosed printer chamber. Expect ~0.4 % shrinkage: set `FIT_SLIDE` and
  `FIT_LOOSE` in `aircheck_params.scad` up by 0.1 mm before slicing.

## Heat-set inserts

Six M2.5 brass inserts go into the front shell's screw posts, entered from the
seam side. Hole is 3.84 mm diameter, 6 mm deep - sized for PETG per the
manufacturer chart, not guessed. Set the iron to 220 C, press until the insert
is flush, keep it square. There are twelve in the BOM because you will ruin a
couple learning the feel.

The carrier board and the two gas breakouts use **direct screws** into 2.1 mm
cores instead, because they are fitted once and never touched again. Inserts
are only worth it where a joint is opened repeatedly.

## What to check on the first print

1. Does the display module drop into its four retaining tabs without force?
   If not, raise `FIT_LOOSE`.
2. Does the button cap move freely in its counterbore and return? If it binds,
   raise `FIT_SLIDE`; if it rattles, lower it.
3. Does the SPS30 slide into its cradle and sit flat against the duct ribs?
   With the 1.5 mm foam strip in place it should need light thumb pressure.
4. Do the two shells close with an even shadow gap all the way round?
5. Hold the assembled case up to a light with the ducts facing you: you should
   **not** be able to see from the inlet opening to the outlet opening. If you
   can, the separating rib is not sealing and the PM readings will read low.
