# Diagnostics and verification tools

| tool | what it does | needs |
|---|---|---|
| `stl_check.py` | mesh closure, bounding box against the build volume, volume and mass, **overhang area** - the thing a render cannot show you | python3 |
| `screen_preview.c` | renders every UI screen to a PBM exactly as the panel would show it, without any hardware | a C compiler |
| `stl_to_step.py` | tessellated STL to STEP, for dropping the case into a mechanical assembly | build123d |

## Overhang measurement

The one worth explaining. A render shows you what a part looks like; it does
not show you that four bosses are holding the whole shell 2 mm off the bed, or
that a chamfer runs tangential into a 90 degree overhang. `stl_check.py`
measures the area of every downward-facing facet steeper than the
self-supporting angle and reports it as a fraction of the surface:

```
$ python3 tools/diagnostics/stl_check.py cad/stl/aircheck_front.stl
  facets            13356
  bounding box      122.0 x 114.0 x 20.0 mm
  non-manifold edges     0
  volume            60.7 cm3
  mass if solid     77 g  (PETG)
  bed contact       126.3 cm2
  overhang > 45 deg  8.8 cm2  (1.6 % of the surface)
```

1.6 % and all of it self-supporting chamfers, which is why neither shell needs
support material.

## Screen previews

```bash
cc -std=c99 -Ifirmware/components/ac_core/include \
   firmware/components/ac_core/src/*.c tools/diagnostics/screen_preview.c \
   -lm -o /tmp/preview
/tmp/preview /tmp/screens
```

It runs a simulated day through the engine first - a quiet room, then a print,
then recovery - so the trend screen has real data on it and the event state is
something other than IDLE. Three separate layout collisions were found by
looking at these images; none of them were visible in the code.
