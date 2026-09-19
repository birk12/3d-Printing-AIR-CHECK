# Diagnostics and verification tools

| tool | what it does | needs |
|---|---|---|
| `stl_check.py` | mesh closure, bounding box against the build volume, volume and mass, bed contact, **overhang area** - the thing a render cannot show you | python3 |
| `stl_to_step.py` | tessellated STL to STEP, for dropping the case into a mechanical assembly | build123d |

## Overhang measurement

The one worth explaining. A render shows you what a part looks like; it does
not show you that four bosses are holding the whole shell 2 mm off the bed, or
that a chamfer runs tangential into a 90 degree overhang. `stl_check.py`
measures the area of every downward-facing facet steeper than the
self-supporting angle and reports it as a fraction of the surface:

```
$ python3 tools/diagnostics/stl_check.py cad/stl/aircheck_front.stl --build 250x210x220
cad/stl/aircheck_front.stl
  facets            17726
  bounding box      130.0 x 146.0 x 30.0 mm
  non-manifold edges     0
  volume            111.6 cm3
  mass if solid     142 g  (PETG)
  mass at 25 % infill 73 g
  bed contact       176.0 cm2
  overhang > 45 deg  10.4 cm2  (1.1 % of the surface)
```

1.1 % on the front shell, 1.0 % on the lid, 0.8 % on the door and the stand.
On the front shell that is the roofs of the side-wall slots and the USB-C
opening, printed as bridges (`manufacturing/print-settings.md`); none of the
four parts needs support material. The checker fails on an open mesh or a part that does not
fit the build volume, and warns above 8 % overhang.
