# STEP files

There are none checked in, on purpose.

OpenSCAD is a CSG modeller with no B-rep kernel, so it cannot export a real
parametric STEP file. What *can* be produced is a tessellated solid wrapped in
a STEP container, tens of megabytes per part, which is a lot of repository
for files you cannot edit anyway.

If you need STEP to drop the enclosure into a mechanical assembly for a
clearance check, generate it yourself:

```bash
~/.venvs/cad/bin/python3 tools/diagnostics/stl_to_step.py
```

(any Python with `build123d` installed works; the script only needs OCCT). It
converts every STL in `cad/stl/` and writes the STEP files here.

The v1.4 enclosure is 136 × 174 × 34 mm in four parts: the front shell (with
the battery compartment and the charger chamber), the lid, the battery door
and the desk stand. The editable source is
`cad/openscad/aircheck_params.scad` plus `cad/openscad/aircheck_case.scad`.
Every dimension is a named variable, and the asserts in `aircheck_case.scad`
re-check every module's position on each render - including the charger's
clearance to the walls and to the gas bay's sensors. Two
of them - the Grove SHT40 board and the Sunrise's pin rows - come from no
published drawing and are measured before printing (TESTING T-M1, T-M2).
