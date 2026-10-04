#!/usr/bin/env python3
"""Check an STL is watertight (every edge shared by exactly two triangles) and print its bounding box.

usage: stlcheck.py file.stl [--max X Y Z]
"""
import struct
import sys


def triangles(path):
    data = open(path, "rb").read()
    if len(data) >= 84:
        n = struct.unpack_from("<I", data, 80)[0]
        if 84 + 50 * n == len(data):  # binary STL
            for i in range(n):
                v = struct.unpack_from("<12fH", data, 84 + 50 * i)
                yield (v[3:6], v[6:9], v[9:12])
            return
    verts = []
    for line in data.decode("ascii", "ignore").splitlines():
        parts = line.split()
        if parts[:1] == ["vertex"]:
            verts.append(tuple(float(p) for p in parts[1:4]))
            if len(verts) == 3:
                yield tuple(verts)
                verts = []


def main():
    path = sys.argv[1]
    limit = None
    if "--max" in sys.argv:
        i = sys.argv.index("--max")
        limit = [float(x) for x in sys.argv[i + 1:i + 4]]
    edges = {}
    lo, hi = [1e18] * 3, [-1e18] * 3
    ntri = 0
    for tri in triangles(path):
        ntri += 1
        pts = [tuple(round(c, 3) for c in p) for p in tri]
        for p in pts:
            for k in range(3):
                lo[k] = min(lo[k], p[k])
                hi[k] = max(hi[k], p[k])
        for a, b in ((0, 1), (1, 2), (2, 0)):
            e = (pts[a], pts[b]) if pts[a] < pts[b] else (pts[b], pts[a])
            edges[e] = edges.get(e, 0) + 1
    bad = sum(1 for c in edges.values() if c != 2)
    size = [round(hi[k] - lo[k], 2) for k in range(3)]
    print(f"{path}: {ntri} triangles, bbox {size} mm, {bad} non-manifold edges")
    ok = bad == 0 and ntri > 0
    if limit and any(size[k] > limit[k] for k in range(3)):
        print(f"  bbox exceeds limit {limit}")
        ok = False
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
