"""Static checks over an exported binary STL.

  * manifold-ish sanity: every edge shared by exactly two facets
  * bounding box against the printer's build volume
  * volume (and therefore mass) via the signed-tetrahedron sum
  * overhang area: facets whose normal points down steeper than the
    self-support angle, which is the thing you cannot see in a render

Usage: python3 tools/diagnostics/stl_check.py part.stl [--build 256x256x256]
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from collections import defaultdict

DENSITY = {"PLA": 1.24, "PETG": 1.27, "ASA": 1.07}   # g/cm3


def read_binary_stl(path):
    with open(path, "rb") as f:
        f.read(80)
        (n,) = struct.unpack("<I", f.read(4))
        tris = []
        for _ in range(n):
            d = struct.unpack("<12fH", f.read(50))
            tris.append((d[0:3], d[3:6], d[6:9], d[9:12]))
    return tris


def volume(tris):
    v = 0.0
    for _, a, b, c in tris:
        v += (a[0] * (b[1] * c[2] - b[2] * c[1])
              - a[1] * (b[0] * c[2] - b[2] * c[0])
              + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
    return abs(v)


def area(a, b, c):
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    return 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("stl")
    ap.add_argument("--build", default="256x256x256")
    ap.add_argument("--angle", type=float, default=45.0,
                    help="self-supporting angle from vertical")
    ap.add_argument("--material", default="PETG")
    ap.add_argument("--infill", type=float, default=0.25)
    a = ap.parse_args()

    tris = read_binary_stl(a.stl)
    if not tris:
        print("no facets"); return 1

    xs = [p[0] for _, *pts in tris for p in pts]
    ys = [p[1] for _, *pts in tris for p in pts]
    zs = [p[2] for _, *pts in tris for p in pts]
    bb = (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    bw, bh, bd = (float(v) for v in a.build.lower().split("x"))

    # edge manifold check
    edges = defaultdict(int)
    q = lambda p: (round(p[0], 4), round(p[1], 4), round(p[2], 4))
    for _, p0, p1, p2 in tris:
        for u, v in ((p0, p1), (p1, p2), (p2, p0)):
            edges[tuple(sorted((q(u), q(v))))] += 1
    bad = sum(1 for c in edges.values() if c != 2)

    vol = volume(tris)
    # A 2.4 mm wall printed with a 0.4 mm nozzle is six perimeters, i.e. solid,
    # so for shell parts the solid mass is the honest number.  The infill figure
    # only applies to bulk regions.
    mass_solid = vol / 1000.0 * DENSITY.get(a.material, 1.27)
    mass = mass_solid * (0.35 + 0.65 * a.infill)

    thresh = -math.cos(math.radians(90 - a.angle))
    over = 0.0
    total = 0.0
    bedish = 0.0
    zmin = min(zs)
    for n, p0, p1, p2 in tris:
        ar = area(p0, p1, p2)
        total += ar
        nz = n[2]
        ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
        nz /= ln
        if nz < thresh:
            if max(p0[2], p1[2], p2[2]) - zmin < 1e-3:
                bedish += ar
            else:
                over += ar

    print(f"{a.stl}")
    print(f"  facets            {len(tris)}")
    print(f"  bounding box      {bb[0]:.1f} x {bb[1]:.1f} x {bb[2]:.1f} mm")
    print(f"  non-manifold edges{bad:>6}")
    print(f"  volume            {vol/1000:.1f} cm3")
    print(f"  mass if solid     {mass_solid:.0f} g  ({a.material})")
    print(f"  mass at {a.infill*100:.0f} % infill {mass:.0f} g")
    print(f"  bed contact       {bedish/100:.1f} cm2")
    print(f"  overhang > {a.angle:.0f} deg  {over/100:.1f} cm2  "
          f"({100*over/total:.1f} % of the surface)")

    ok = True
    if bad:
        print("  FAIL: mesh is not closed"); ok = False
    if bb[0] > bw or bb[1] > bh or bb[2] > bd:
        print(f"  FAIL: does not fit a {a.build} build volume"); ok = False
    if over / total > 0.08:
        print("  WARN: more than 8 % overhang, check the orientation")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
