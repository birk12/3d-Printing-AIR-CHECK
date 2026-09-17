# STEP files

There are none checked in, on purpose.

OpenSCAD is a CSG modeller with no B-rep kernel, so it cannot export a real
parametric STEP file. What *can* be produced is a tessellated solid wrapped in
a STEP container - the enclosure came out at 33 MB and 16 MB that way, which is
a lot of repository for a file you cannot edit anyway.

If you need STEP to drop the enclosure into a mechanical assembly for a
clearance check, generate it yourself:

```bash
~/.venvs/cad/bin/python3 tools/diagnostics/stl_to_step.py
```

(any Python with `build123d` installed works; the script only needs OCCT).

The editable source is `cad/openscad/aircheck_params.scad` plus
`cad/openscad/aircheck_case.scad`. Every dimension is a named variable.
