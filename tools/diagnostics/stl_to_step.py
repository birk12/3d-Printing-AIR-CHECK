"""Tessellated STL -> STEP conversion.

OpenSCAD has no B-rep, so it cannot emit a real parametric STEP file.  What
this produces is a faceted solid wrapped in a STEP container: good enough to
drop the enclosure into a mechanical assembly for clearance checking, useless
for feature editing.  The editable source stays cad/openscad/*.scad.

Needs build123d (OCCT).  Run with the CAD virtualenv:
    ~/.venvs/cad/bin/python3 tools/diagnostics/stl_to_step.py
"""
import glob
import os
import sys

from build123d import Mesher, export_step

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
out = os.path.join(root, "cad/step")
os.makedirs(out, exist_ok=True)
rc = 0
for stl in sorted(glob.glob(os.path.join(root, "cad/stl/*.stl"))):
    name = os.path.splitext(os.path.basename(stl))[0]
    try:
        shapes = Mesher().read(stl)
        shape = shapes[0]
        dst = os.path.join(out, name + ".step")
        export_step(shape, dst)
        print(f"{name}: {os.path.getsize(dst)/1e6:.1f} MB")
    except Exception as exc:                       # noqa: BLE001
        print(f"{name}: FAILED - {exc}")
        rc = 1
sys.exit(rc)
