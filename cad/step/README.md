# STEP files

There are none checked in, on purpose.

OpenSCAD is a CSG modeller with no B-rep kernel, so it cannot export a real
parametric STEP file. What *can* be produced is a tessellated solid wrapped in
a STEP container - the v1.3 enclosure comes out at 43 MB (front shell), 13 MB
(lid), 34 MB (battery door) and 0.1 MB (stand) that way, which is a lot of
repository for files you cannot edit anyway.

If you need STEP to drop the enclosure into a mechanical assembly for a
clearance check, generate it yourself:

```bash
~/.venvs/cad/bin/python3 tools/diagnostics/stl_to_step.py
```

(any Python with `build123d` installed works; the script only needs OCCT). It
converts every STL in `cad/stl/` and writes the STEP files here.

The editable source is `cad/openscad/aircheck_params.scad` plus
`cad/openscad/aircheck_case.scad`. Every dimension is a named variable. Two
of them - the Grove SHT40 board and the Sunrise's pin rows - come from no
published drawing and are measured before printing (TESTING T-M1, T-M2).
