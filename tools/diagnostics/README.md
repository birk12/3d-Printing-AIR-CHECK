# Diagnostics and verification tools

| tool | what it does | needs |
|---|---|---|
| `stl_check.py` | mesh closure, bounding box against the build volume, volume and mass, **overhang area** - the thing a render cannot show you | python3 |
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
  bounding box      98.0 x 102.0 x 20.0 mm
  non-manifold edges     0
  mass if solid     61 g  (PETG)
  overhang > 45 deg  7.3 cm2  (1.7 % of the surface)
```

1.7 % and all of it self-supporting chamfers, which is why neither shell needs
support material.
