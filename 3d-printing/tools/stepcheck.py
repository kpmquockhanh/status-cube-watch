#!/usr/bin/env python3
"""Check printed parts against Waveshare's STEP model of the board, not the box ghost in board.scad.

usage: stepcheck.py mesh BOARD.stp OUT.npz
       stepcheck.py check OUT.npz PART.stl FRAME.echo [--touch-pcb] [--min-gap G]

`mesh` (needs cadquery-ocp, trimesh, scipy) turns every solid of the STEP into a closed mesh in board.scad's
board-local frame (x = portrait right from the glass edge, y = into the stand from the glass front, z = portrait
up): the STEP's glass faces +Z, so the frame is the STEP turned 180 deg about the glass normal, centred on the
glass. A solid whose triangulation is not closed (tiny pads, a few chips) is replaced by its convex hull.

`check` (needs numpy, manifold3d) places the board where FRAME.echo says (`part="board_frame"`: O, lean, flip,
sag) and intersects every solid with PART.stl, a printed part rendered in place. It exits 1 when the part
overlaps a solid, or (--min-gap) comes closer than G to one, and 2 when it cannot check. The glass module and the PCB are exempt from
--min-gap: the glass meets the lip and the posts press the PCB by design. --touch-pcb: the part must overlap
the PCB (the preload) and nothing else.
"""
import re
import sys

import numpy as np


def mesh(stp, out):
    import trimesh
    from OCP.BRep import BRep_Tool
    from OCP.BRepBndLib import BRepBndLib
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.Bnd import Bnd_Box
    from OCP.STEPControl import STEPControl_Reader
    from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED, TopAbs_SOLID
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopLoc import TopLoc_Location
    from OCP.TopoDS import TopoDS

    r = STEPControl_Reader()
    if r.ReadFile(stp) != 1:
        sys.exit(f"cannot read {stp}")
    r.TransferRoots()
    shape = r.OneShape()
    box = Bnd_Box()
    BRepBndLib.Add_s(shape, box, True)
    lo, hi = box.CornerMin(), box.CornerMax()
    BRepMesh_IncrementalMesh(shape, 0.01, False, 0.2, True)
    solids = {}
    e = TopExp_Explorer(shape, TopAbs_SOLID)
    while e.More():
        V, F = [], []
        f = TopExp_Explorer(e.Current(), TopAbs_FACE)
        while f.More():
            face = TopoDS.Face(f.Current())
            loc = TopLoc_Location()
            tri = BRep_Tool.Triangulation_s(face, loc)
            if tri is not None:
                t, base = loc.Transformation(), len(V)
                for k in range(1, tri.NbNodes() + 1):
                    p = tri.Node(k).Transformed(t)
                    V.append((p.X(), p.Y(), p.Z()))
                rev = face.Orientation() == TopAbs_REVERSED
                for k in range(1, tri.NbTriangles() + 1):
                    a, b, c = (i + base - 1 for i in tri.Triangle(k).Get())
                    F.append((a, c, b) if rev else (a, b, c))
            f.Next()
        e.Next()
        V = np.array(V)
        # STEP (X, Y, Z) -> board-local: 180 deg about Z, glass front to y = 0, glass centred on board.scad's
        L = np.column_stack([(lo.X() + hi.X()) / 2 - V[:, 0] + 33.13 / 2,
                             hi.Z() - V[:, 2],
                             (lo.Y() + hi.Y()) / 2 - V[:, 1] + 41.13 / 2])
        m = trimesh.Trimesh(L, F, process=True)
        if not m.is_watertight:
            m = m.convex_hull
        i = len(solids) // 2
        solids[f"v{i}"], solids[f"f{i}"] = np.asarray(m.vertices), np.asarray(m.faces)
    np.savez_compressed(out, n=len(solids) // 2, **solids)
    print(f"{out}: {len(solids) // 2} solids")


def kinds(sol):
    """Name the glass module (it starts at the glass front) and the PCB (the largest 1 mm slab)."""
    lo = [v.min(0) for v, _ in sol]
    hi = [v.max(0) for v, _ in sol]
    glass = min(range(len(sol)), key=lambda i: lo[i][1])
    slabs = [i for i in range(len(sol)) if hi[i][1] - lo[i][1] < 1.1]
    pcb = max(slabs, key=lambda i: (hi[i][0] - lo[i][0]) * (hi[i][2] - lo[i][2]))

    def label(i):
        if i == glass:
            return "glass + LCD + flex"
        if i == pcb:
            return "PCB"
        return f"part x {lo[i][0]:.1f}..{hi[i][0]:.1f} z {lo[i][2]:.1f}..{hi[i][2]:.1f}, {hi[i][1]:.2f} deep"
    return glass, pcb, label


def check(npz, stl, echo, touch_pcb, min_gap):
    import manifold3d as M
    sys.path.insert(0, __file__.rsplit("/", 1)[0])
    from stlcheck import triangles

    d = np.load(npz)
    sol = [(d[f"v{i}"], d[f"f{i}"]) for i in range(int(d["n"]))]
    glass, pcb, label = kinds(sol)
    m = re.search(r"BOARDFRAME\[([^\]]*)\]", open(echo).read())
    if not m:
        print(f"{echo}: no BOARDFRAME echo")
        sys.exit(2)
    ox, oy, oz, lean, flip, gb, ga, sag = (float(x) for x in m.group(1).split(","))
    c, s = np.cos(np.radians(lean)), np.sin(np.radians(lean))

    def place(p):  # flip(), then the sag, then b2g() in board.scad
        p = p.copy()
        if flip > 0.5:
            p[:, 0], p[:, 2] = gb - p[:, 0], ga - p[:, 2]
        p[:, 2] -= sag
        return np.column_stack([ox + p[:, 0], oy + p[:, 1] * c + p[:, 2] * s, oz - p[:, 1] * s + p[:, 2] * c])

    T = np.array(list(triangles(stl)), np.float32).reshape(-1, 3)
    pm = M.Mesh(T, np.arange(len(T), dtype=np.uint32).reshape(-1, 3))
    pm.merge()
    part = M.Manifold(pm)
    if part.is_empty():
        print(f"{stl}: not a closed solid")
        sys.exit(2)
    reach = max(min_gap, 1.0) + 1.0   # min_gap() searches this far and reports `reach` when nothing is closer
    hits, gaps = [], []
    for i, (v, f) in enumerate(sol):
        b = M.Manifold(M.Mesh(place(v).astype(np.float32), f.astype(np.uint32)))
        vol = (part ^ b).volume()
        if vol > 1e-4:
            hits.append((i, vol))
        if i not in (glass, pcb):
            gaps.append((part.min_gap(b, reach), i))
    bad = [h for h in hits if not (touch_pcb and h[0] == pcb)]
    ok = not bad and (not touch_pcb or any(h[0] == pcb for h in hits))
    near = min(gaps) if gaps else None
    if min_gap and near and near[0] < min_gap:
        ok = False
    for i, vol in hits:
        print(f"  overlap {vol:.3f} mm3 with {label(i)}")
    if touch_pcb and not any(h[0] == pcb for h in hits):
        print("  does not reach the PCB")
    if near and near[0] < reach:
        print(f"  closest {near[0]:.2f} mm to {label(near[1])}")
    return ok


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["mesh"] and len(a) == 3:
        mesh(a[1], a[2])
    elif a[:1] == ["check"] and len(a) >= 4:
        g = float(a[a.index("--min-gap") + 1]) if "--min-gap" in a else 0.0
        sys.exit(0 if check(a[1], a[2], a[3], "--touch-pcb" in a, g) else 1)
    else:
        sys.exit(__doc__)
