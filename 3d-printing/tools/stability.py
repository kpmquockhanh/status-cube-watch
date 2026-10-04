#!/usr/bin/env python3
"""Tip-over estimate for the easel stand, from the assembled-position meshes test.sh renders.

usage: stability.py build/easel [--min-center N] [--min-top N] [--cell-g G] [--board-g G] [--density G_PER_MM3]

Reads <prefix>-at_{tray,frame,carrier,bezel}.stl (printed parts: volume x density), <prefix>-at_cell.stl and
<prefix>-at_board.stl (fixed masses spread over their volume), and <prefix>-meta.echo (lean, press points,
pivots, from `part="meta"`). A press pushes into the screen, along the screen normal. The stand tips
backwards about the rear pivot line (world y = pivot_y, on the desk) when

    F * (s_z * cos L - (pivot_y - s_y) * sin L) > W * (pivot_y - cog_y)

so the force that just tips it is F = W * (pivot_y - cog_y) / (s_z cos L - (pivot_y - s_y) sin L); with the
denominator <= 0 the push presses the stand down instead. Exits 1 when the centre of gravity is outside the
feet, or a force falls short of its minimum. Estimates only: infill, screws and rubber feet are ignored.
"""
import argparse
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stlcheck import triangles  # noqa: E402

G = 9.81
PRINTED = ["tray", "frame", "carrier", "bezel"]


def volume_centroid(path):
    """Signed-tetrahedron volume (mm^3) and centroid of a closed mesh."""
    v = cx = cy = cz = 0.0
    for a, b, c in triangles(path):
        d = (a[0] * (b[1] * c[2] - b[2] * c[1])
             - a[1] * (b[0] * c[2] - b[2] * c[0])
             + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
        v += d
        cx += d * (a[0] + b[0] + c[0]) / 4.0
        cy += d * (a[1] + b[1] + c[1]) / 4.0
        cz += d * (a[2] + b[2] + c[2]) / 4.0
    if v <= 0:
        sys.exit(f"{path}: empty or inside-out mesh (volume {v:.3f})")
    return v, (cx / v, cy / v, cz / v)


def read_meta(path):
    m = re.search(r'ECHO: "EASEL(\{.*\})"', open(path).read())
    if not m:
        sys.exit(f"{path}: no EASEL{{...}} echo (render part=\"meta\" first)")
    return json.loads(m.group(1).replace('\\"', '"'))


def tip_force(w, cog_y, s, pivot_y, lean):
    L = math.radians(lean)
    den = s[2] * math.cos(L) - (pivot_y - s[1]) * math.sin(L)
    return math.inf if den <= 0 else w * (pivot_y - cog_y) / den


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("prefix", help="e.g. build/easel")
    ap.add_argument("--min-center", type=float, default=0.0, help="N, press at the centre of the active area")
    ap.add_argument("--min-top", type=float, default=0.0, help="N, press at the top edge of the active area")
    ap.add_argument("--cell-g", type=float, default=55.0)
    ap.add_argument("--board-g", type=float, default=12.0)
    ap.add_argument("--density", type=float, default=1.116e-3, help="g/mm^3 (PLA 1.24 x 0.9 solidity)")
    a = ap.parse_args()

    meta = read_meta(f"{a.prefix}-meta.echo")
    masses = []  # (name, grams, centroid)
    for name in PRINTED:
        v, c = volume_centroid(f"{a.prefix}-at_{name}.stl")
        masses.append((name, v * a.density, c))
    for name, g in (("cell", a.cell_g), ("board", a.board_g)):
        masses.append((name, g, volume_centroid(f"{a.prefix}-at_{name}.stl")[1]))

    total = sum(g for _, g, _ in masses)
    cog = [sum(g * c[i] for _, g, c in masses) / total for i in range(3)]
    for name, g, c in masses:
        print(f"  {name:8s} {g:6.1f} g  at y {c[1]:5.1f} z {c[2]:5.1f}")
    print(f"total {total:.1f} g, CoG x {cog[0]:.1f} y {cog[1]:.1f} z {cog[2]:.1f}; "
          f"feet y {meta['front_y']}..{meta['pivot_y']}")

    ok = True
    if not meta["front_y"] < cog[1] < meta["pivot_y"]:
        print("FAIL: centre of gravity outside the feet (falls over on its own)")
        ok = False
    w = total / 1000.0 * G
    for label, key, need in (("centre", "press", a.min_center), ("top", "top", a.min_top)):
        f = tip_force(w, cog[1], meta[key], meta["pivot_y"], meta["lean"])
        short = f < need
        ok &= not short
        print(f"{label:6s} press tips it at {f:.2f} N (need >= {need}){'  SHORT' if short else ''}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
