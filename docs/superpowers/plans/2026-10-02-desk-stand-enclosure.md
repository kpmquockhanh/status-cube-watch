# Desk Stand Enclosure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A parametric OpenSCAD desk stand (front shell + base plate + fit-check coupon) for the Waveshare ESP32-S3-Touch-LCD-1.69 and a 12 × 40 × 65 mm LiPo, with print-ready STLs.

**Architecture:** One file, `docs/enclosure/stand.scad`, holds every dimension as a named variable and a `part` selector at the bottom. The same file renders the printable parts (`front`, `base`, `fit`) and the *test parts* (`check_*`), which are boolean intersections that must come out empty (no collision) or non-empty (a cut really reaches the wall). `docs/enclosure/test.sh` renders each test part with OpenSCAD and asserts the result, so geometry gets a red/green cycle even without a CAD test framework.

**Tech Stack:** OpenSCAD (snapshot build with the Manifold backend recommended; stable 2021.01 works but is slow), Bash, Python 3 stdlib (watertightness check).

**Spec:** `docs/superpowers/specs/2026-10-02-desk-stand-enclosure-design.md`

## Deviations from the spec (found while working out the geometry)

1. **Board retention.** The spec says the PCB is screwed to 4 standoffs through its mounting holes. The display glass covers the front of those holes, so a screw cannot enter from the only side the standoffs could reach. Instead the board sits in a recess in the sloped face (glass pocket) and **two pusher posts on the base plate** press the PCB's rear edge up and forward so the glass stays seated; the base-plate screws are the only screws. The fit-check print validates this. Foam tape on the pusher tips is the fallback if the board rattles.
2. **`mirror_sides` becomes `board_flip`.** A mirror is physically wrong (the board cannot be mirrored). If the board turns out to be 180° from the assumed orientation, `board_flip = true` rotates it in the pocket and the firmware needs `lcd.setRotation(2)` in `firmware/src/main.cpp:133`.
3. **No separate cable channel.** The interior is one open volume, so the MX1.25 lead routes freely from the battery to the board connector.
4. **Envelope** is about 48 × 86 × 64 mm (spec said about 70 × 46 × 60): the 65 mm battery plus screw bosses at both ends sets the depth, and a 5 mm top margin above the glass keeps the shell skin at the apex thick enough at any tilt from 55° to 75°.

## Global Constraints

- Units are millimetres; global frame is X = left→right seen from the front, Y = front(0)→back, Z = up from the shell's bottom edge (the base plate occupies Z ∈ [−plate_t, 0]).
- Tilt default `65` degrees from the desk; wall `2.4`; plate `2.4`; lip in front of the glass `1.0`; glass clearance `0.3`; battery clearance `0.4` per side.
- Battery `12 × 40 × 65` (thickness × width × length). Board numbers from the Waveshare drawing: glass 41.13 × 33.13, active 32.634 × 27.972, PCB 37.12 × 29.83, mounting hole centre 4.16 / 1.75 from the PCB corner.
- Values that are guesses are marked `VERIFY` in `stand.scad` and listed in the README: board thickness (7), PCB offsets inside the glass, glass corner radius (4), hole diameter (2.2), connector and button depths.
- Fasteners: 4 × M2 self-tapping into printed bosses (pilot 1.8), or heat-set inserts (`insert_mode = true`, hole 3.2).
- Printable on a plain FDM printer at 0.2 mm layers; plastic only (the chip antenna must not be shielded).
- Orientation inferred: Type-C and battery connector face the right wall, RST/BOOT/PWR the left wall, in the firmware's portrait view (`setRotation(0)`).
- This directory is not a git repository, so there are no commit steps. A task is done when `docs/enclosure/test.sh` exits 0.

## Review Focus

Failure modes the spec implies that a user is most likely to hit; each has a test in the task that owns the code.

1. Different tilt (a user wants 55° or 75°): the glass pocket must not break through the shell's skin. Test in Task 3.
2. Different battery (a thicker or swollen cell, e.g. 15 mm): nothing may collide and the roof must still clear it. Test in Task 4.
3. Printer tolerance differs (clearance 0.5, or a thicker board): the pocket must still sit inside the shell. Test in Task 3.
4. Board is 180° from the inferred orientation (`board_flip = true`): the Type-C slot must move to the left wall, buttons to the right. Test in Task 6.
5. Window must never overlap the touch/active area at any of the above variants. Test in Task 3.

---

### Task 1: Tooling and test harness

**Files:**
- Create: `docs/enclosure/tools/stlcheck.py`
- Create: `docs/enclosure/test.sh`
- Create: `docs/enclosure/stand.scad` (skeleton only)

**Interfaces:**
- Produces: `test.sh` helper functions `expect_empty <label> <part> [-D ...]`, `expect_solid <label> <part> [-D ...]`, `expect_manifold <label> <part> [--max X Y Z]`, `expect_error <label> <part>`; later tasks append test lines before the final `exit $fail`. `stand.scad` dispatches on the variable `part`; unknown names are an error (so an empty result can never pass by accident).
- `stlcheck.py <file.stl> [--max X Y Z]`: prints triangle count and bounding box, exits 1 if any edge is not shared by exactly 2 triangles or the bounding box exceeds `--max`.

- [ ] **Step 1: Install OpenSCAD**

Run: `brew install --cask openscad@snapshot` (Manifold backend; much faster than the stable CGAL build). Then find the binary and export it:

```sh
ls /Applications | grep -i openscad
export OPENSCAD=/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD   # adjust to the name printed above
"$OPENSCAD" --version
```

Expected: a version string. If the snapshot cask is unavailable use `brew install openscad`; the tests still run, just slower.

- [ ] **Step 2: Write the STL checker**

Create `docs/enclosure/tools/stlcheck.py`:

```python
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


main()
```

- [ ] **Step 3: Write the harness with its first two tests (they must fail: no `stand.scad` yet)**

Create `docs/enclosure/test.sh` and `chmod +x` it:

```bash
#!/usr/bin/env bash
# Geometry tests for stand.scad. Usage: docs/enclosure/test.sh   (set OPENSCAD=... if it is not on PATH)
set -u
cd "$(dirname "$0")"
OPENSCAD="${OPENSCAD:-$(command -v openscad || echo /Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD)}"
BACKEND=""
"$OPENSCAD" --help 2>&1 | grep -q -- "--backend" && BACKEND="--backend=manifold"
mkdir -p build
fail=0

render() {  # render <part> [extra openscad args...] -> build/<part>.stl and build/<part>.log
  local part="$1"; shift
  "$OPENSCAD" $BACKEND -D "part=\"$part\"" "$@" -o "build/$part.stl" stand.scad >"build/$part.log" 2>&1
}
is_empty()  { grep -q "object is empty" "build/$1.log"; }
has_error() { grep -q -E "ERROR|Assertion|assertion" "build/$1.log"; }

expect_empty() {  # label part [-D ...]   passes when the part has NO geometry (no collision)
  local label="$1" part="$2"; shift 2
  render "$part" "$@"
  if has_error "$part"; then echo "FAIL $label: openscad error"; sed -n 1,4p "build/$part.log"; fail=1
  elif is_empty "$part"; then echo "PASS $label"
  else echo "FAIL $label: expected empty, got geometry (overlap)"; fail=1; fi
}
expect_solid() {  # label part [-D ...]   passes when the part HAS geometry
  local label="$1" part="$2"; shift 2
  render "$part" "$@"
  if has_error "$part"; then echo "FAIL $label: openscad error"; sed -n 1,4p "build/$part.log"; fail=1
  elif is_empty "$part" || [ ! -s "build/$part.stl" ]; then echo "FAIL $label: expected geometry, got empty"; fail=1
  else echo "PASS $label"; fi
}
expect_manifold() {  # label part [--max X Y Z]   watertight and within a bounding box
  local label="$1" part="$2"; shift 2
  expect_solid "$label renders" "$part" || true
  python3 tools/stlcheck.py "build/$part.stl" "$@" && echo "PASS $label watertight" || { echo "FAIL $label watertight"; fail=1; }
}
expect_error() {  # label part   passes when openscad rejects the part
  render "$2"
  if has_error "$2"; then echo "PASS $1"; else echo "FAIL $1: expected an error"; fail=1; fi
}

expect_empty "part=none renders nothing" none
expect_error "unknown part is rejected" bogus

exit $fail
```

- [ ] **Step 4: Run it and see it fail**

Run: `docs/enclosure/test.sh`
Expected: `FAIL part=none renders nothing: openscad error` (no `stand.scad` yet), `PASS unknown part is rejected` (the missing file is also an error), exit 1.

- [ ] **Step 5: Create the skeleton**

Create `docs/enclosure/stand.scad`:

```scad
// stand.scad: desk stand for the Claude status cube
// (Waveshare ESP32-S3-Touch-LCD-1.69 + 12x40x65 mm LiPo). Units: mm.
// Rendered by test.sh via the `part` variable; see docs/superpowers/plans/2026-10-02-desk-stand-enclosure.md.

part = "assembly";

if (part == "none") {}
else assert(false, str("unknown part: ", part));
```

- [ ] **Step 6: Run to verify both pass**

Run: `docs/enclosure/test.sh`
Expected: both PASS, exit 0.

---

### Task 2: Parameters and the shell body

**Files:**
- Modify: `docs/enclosure/stand.scad` (replace the skeleton below the `part` line)
- Modify: `docs/enclosure/test.sh` (append tests before `exit $fail`)

**Interfaces:**
- Consumes: Task 1 harness.
- Produces (used by every later task): parameters and derived values `W, D, z0, F1, z_back, O, lean, bx0, by0, bay_w, bay_l, bay_t, boss_xy, boss_r, boss_h, pcb_rear`; modules `xz_extrude(h)`, `rrect(x0,z0,w,h,r)`, `board_frame()`, `flip()`, `bf()`, `outer_solid()`, `inner_void()`, `bosses()`, `front_shell()`; functions `b2g(p)`, `flipp(p)`.

- [ ] **Step 1: Write the failing test**

Append to `test.sh` before `exit $fail`:

```bash
expect_manifold "front shell body" front_shell --max 48 86 63
```

- [ ] **Step 2: Run to verify it fails**

Run: `docs/enclosure/test.sh`
Expected: FAIL with `unknown part: front_shell` (openscad error).

- [ ] **Step 3: Write the parameters, helpers and shell body**

Replace everything below `part = "assembly";` in `stand.scad` with:

```scad
$fn = 48;

// ---- tunables ---------------------------------------------------------------
tilt = 65;                  // screen angle from the desk
board_flip = false;         // rotate the board 180 deg in its pocket (also needs lcd.setRotation(2))
insert_mode = false;        // true: heat-set insert holes instead of self-tapping pilots
pilot_d = 1.8;              // M2 self-tapping pilot
insert_d = 3.2;             // M2 heat-set insert hole

wall = 2.4;  plate_t = 2.4;
lip_t = 1.0;                // material in front of the glass
rim = 3.0;                  // face margin below the glass
rim_top = 5.0;              // face margin above the glass (keeps the apex skin thick at any tilt)
clr = 0.3;                  // glass pocket clearance, per side
bcl = 0.4;                  // battery clearance, per side
side_gap = 1.0;             // extra room beside the battery pocket
end_zone = 7.5;             // room at each end of the battery for bosses / pushers
boss_r = 2.6;  boss_h = 8;

// Battery: [length, width, thickness]
bat = [65, 40, 12];

// Board, from the Waveshare drawing. Drawing coords: a = from its left edge, b = from its top edge.
// In the firmware's portrait view: portrait x = b, portrait z = a.
glass_a = 41.13;  glass_b = 33.13;  glass_r = 4;               // VERIFY glass_r
act_a0 = 2.9;  act_b0 = 2.4;  act_a = 32.634;  act_b = 27.972;  // VERIFY the two offsets
pcb_a = 37.12;  pcb_b = 29.83;  pcb_a0 = 0.7;  pcb_b0 = 1.65;   // VERIFY the two offsets
pcb_y0 = 2.0;  pcb_t = 1.2;                                     // VERIFY: PCB front face depth, PCB thickness
board_t = 7;                                                    // VERIFY: glass front to tallest rear part
hole_d = 2.2;  hole_ia = 4.16;  hole_ib = 1.75;                 // VERIFY hole_d; offsets are from the PCB corner
btn_a = [10.7, 19.8, 29.1];                                     // RST, BOOT, PWR along portrait z
usb_a = 20.3;  usb_w = 9.0;  usb_h = 3.3;                       // VERIFY
win_margin = 1.0;                                               // window = active area + this per side

// ---- derived ----------------------------------------------------------------
lean = 90 - tilt;                              // screen lean from vertical
bay_l = bat[0] + 2*bcl;  bay_w = bat[1] + 2*bcl;  bay_t = bat[2] + 2*bcl;
inner_w = bay_w + 2*side_gap;
W = inner_w + 2*wall;
inner_d = bay_l + 2*end_zone;
D = inner_d + 2*wall;
z0 = max(16, bay_t + 3);                       // height of the face's bottom edge
face_len = glass_a + 2*clr + rim + rim_top;
F1 = [face_len*sin(lean), z0 + face_len*cos(lean)];   // top edge of the face, in (y, z)
z_back = bay_t + wall + 1.5;                   // outer roof height at the back
bx0 = (W - bay_w)/2;  by0 = wall + end_zone;   // battery pocket origin (z = 0 is the plate top)
pcb_rear = pcb_y0 + pcb_t;
// Board-local origin (glass front face, portrait bottom-left corner) in global coordinates.
O = [(W - glass_b)/2,
     (rim + clr)*sin(lean) + lip_t*cos(lean),
     z0 + (rim + clr)*cos(lean) - lip_t*sin(lean)];
boss_xy = [for (x = [wall + boss_r, W - wall - boss_r], y = [wall + boss_r, D - wall - boss_r]) [x, y]];

// ---- helpers ----------------------------------------------------------------
// 2D shape given in (x, z) extruded along +y.
module xz_extrude(h) { rotate([-90, 0, 0]) linear_extrude(height = h) mirror([0, 1, 0]) children(); }
module rrect(x0, z0, w, h, r) { translate([x0 + r, z0 + r]) offset(r = r) square([w - 2*r, h - 2*r]); }

// Board-local frame: x = portrait right, y = INTO the stand from the glass front, z = portrait up.
module board_frame() { translate(O) rotate([-lean, 0, 0]) children(); }
module flip() {
  if (board_flip) translate([glass_b/2, 0, glass_a/2]) rotate([0, 180, 0]) translate([-glass_b/2, 0, -glass_a/2]) children();
  else children();
}
module bf() { board_frame() flip() children(); }
function b2g(p) = [O[0] + p[0],
                   O[1] + p[1]*cos(lean) + p[2]*sin(lean),
                   O[2] - p[1]*sin(lean) + p[2]*cos(lean)];
function flipp(p) = board_flip ? [glass_b - p[0], p[1], glass_a - p[2]] : p;

// ---- shell body -------------------------------------------------------------
function profile_pts(zb) = [[0, zb], [0, z0], F1, [D, z_back], [D, zb]];
module profile_outer() polygon(profile_pts(0));
// 2D (y, z) profile extruded along +x
module across(w) { rotate([90, 0, 90]) linear_extrude(w) children(); }
module outer_solid() across(W) profile_outer();
module outer_inset(t) translate([t, 0, 0]) across(W - 2*t) offset(delta = -t) profile_outer();
// open at the bottom: the profile is extended below z=0 before offsetting inwards
module inner_void() translate([wall, 0, 0]) across(W - 2*wall) offset(delta = -wall) polygon(profile_pts(-5));

module boss(p) {
  sx = p[0] < W/2 ? -1 : 1;  sy = p[1] < D/2 ? -1 : 1;
  translate([p[0], p[1], 0]) hull() {
    cylinder(r = boss_r, h = boss_h);
    translate([sx*(boss_r + wall/2) - 0.5, sy*(boss_r + wall/2) - 0.5, 0]) cube([1, 1, boss_h]);
  }
}
module bosses() for (p = boss_xy) boss(p);
module boss_pilots() for (p = boss_xy) translate([p[0], p[1], -0.01]) cylinder(d = insert_mode ? insert_d : pilot_d, h = boss_h - 1);

module front_shell() difference() {
  union() { difference() { outer_solid(); inner_void(); } bosses(); }
  boss_pilots();
}

// ---- parts (rendered by test.sh) ----------------------------------------------
if (part == "none") {}
else if (part == "front_shell") front_shell();
else assert(false, str("unknown part: ", part));
```

- [ ] **Step 4: Run to verify it passes**

Run: `docs/enclosure/test.sh`
Expected: all PASS, including `front shell body renders` and `front shell body watertight` (bbox about 47.6 × 85.6 × 61).

If the bbox limit fails, read the printed bbox and fix the limit only if the numbers match the derived values above (W 47.6, D 85.6, face top about 61); otherwise there is a derivation bug.

---

### Task 3: Glass pocket and window

**Files:**
- Modify: `docs/enclosure/stand.scad`
- Modify: `docs/enclosure/test.sh`

**Interfaces:**
- Consumes: `bf()`, `xz_extrude`, `rrect`, `outer_solid`, `outer_inset(t)` from Task 2.
- Produces: `pocket(c = clr)`, `window()`, `pcb_slab()`, `board_ghost()`; `front_shell()` now cuts the pocket and window; test parts `check_window`, `check_pocket_inside`, `check_skin`.

- [ ] **Step 1: Write the failing tests**

Append to `test.sh` before `exit $fail`:

```bash
# window never covers the active area
expect_empty "active area is clear (nominal)"   check_window
expect_empty "active area is clear (tilt 55)"   check_window -D tilt=55
expect_empty "active area is clear (tilt 75)"   check_window -D tilt=75
# the pocket stays inside the outer body and keeps >= 0.8 mm of skin
expect_empty "pocket inside outer (nominal)"    check_pocket_inside
expect_empty "pocket skin >= 0.8 (nominal)"     check_skin
expect_empty "pocket skin >= 0.8 (tilt 55)"     check_skin -D tilt=55
expect_empty "pocket skin >= 0.8 (tilt 75)"     check_skin -D tilt=75
expect_empty "pocket skin >= 0.8 (board 9 mm)"  check_skin -D board_t=9
expect_empty "pocket skin >= 0.8 (clr 0.5)"     check_skin -D clr=0.5
expect_manifold "front shell with pocket" front_shell --max 48 86 63
```

- [ ] **Step 2: Run to verify they fail**

Run: `docs/enclosure/test.sh`
Expected: FAIL (`unknown part: check_window`, etc.).

- [ ] **Step 3: Implement**

In `stand.scad`, add before the `// ---- parts` section:

```scad
// ---- board pocket and window ----------------------------------------------------
module pocket(c = clr) xz_extrude(board_t + c) rrect(-c, -c, glass_b + 2*c, glass_a + 2*c, glass_r + c);
// through the lip, framing the active area
module window() translate([0, -lip_t - 0.5, 0]) xz_extrude(lip_t + 1.0)
  rrect(act_b0 - win_margin, act_a0 - win_margin, act_b + 2*win_margin, act_a + 2*win_margin, 2);
module pcb_slab() translate([pcb_b0, pcb_y0, pcb_a0]) cube([pcb_b, pcb_t, pcb_a]);
module board_ghost() {   // stand-in for the real board, for assembly renders and tests
  xz_extrude(1.6) rrect(0, 0, glass_b, glass_a, glass_r);
  pcb_slab();
}
// exactly the touch/display area, in front of the glass
module active_prism() translate([act_b0, -lip_t - 0.5, act_a0]) cube([act_b, lip_t + 0.7, act_a]);
```

Replace `front_shell()`:

```scad
module front_shell() difference() {
  union() { difference() { outer_solid(); inner_void(); } bosses(); }
  bf() { pocket(); window(); }
  boss_pilots();
}
```

Replace the parts dispatcher with:

```scad
if (part == "none") {}
else if (part == "front_shell") front_shell();
else if (part == "check_window") intersection() { front_shell(); bf() active_prism(); }
else if (part == "check_pocket_inside") difference() { bf() pocket(); outer_solid(); }
else if (part == "check_skin") difference() { bf() pocket(); outer_inset(0.8); }
else assert(false, str("unknown part: ", part));
```

- [ ] **Step 4: Run to verify they pass**

Run: `docs/enclosure/test.sh`
Expected: all PASS.

If `check_skin` fails for a tilt or board thickness variant, the pocket is too close to the roof apex or the lower edge. First try raising `rim_top` (apex) or `rim` (lower edge) by 1 mm; if the nominal case fails, recheck `O` against the derivation. Do not relax the 0.8 mm skin limit. Record the working values.

---

### Task 4: Battery bay

**Files:**
- Modify: `docs/enclosure/stand.scad`
- Modify: `docs/enclosure/test.sh`

**Interfaces:**
- Consumes: `bay_*`, `bx0`, `by0`, `inner_void()`, `bosses()`, `bf()`, `pocket()`.
- Produces: `bay_ghost()` (the pocket volume, battery plus clearance) and `battery_ghost()` (the cell resting on the plate); test parts `check_battery_void`, `check_battery_bosses`, `check_battery_board`.

- [ ] **Step 1: Write the failing tests**

Append to `test.sh` before `exit $fail`:

```bash
expect_empty "battery pocket inside the void"        check_battery_void
expect_empty "battery pocket clear of bosses"        check_battery_bosses
expect_empty "battery pocket clear of the board"     check_battery_board
expect_empty "15 mm cell: inside the void"           check_battery_void  -D 'bat=[65,40,15]'
expect_empty "15 mm cell: clear of the board"        check_battery_board -D 'bat=[65,40,15]'
expect_empty "15 mm cell: clear of the roof"         check_battery_roof  -D 'bat=[65,40,15]'
expect_empty "battery pocket clear of the roof"      check_battery_roof
```

- [ ] **Step 2: Run to verify they fail**

Run: `docs/enclosure/test.sh`
Expected: FAIL (`unknown part: check_battery_void`, etc.).

- [ ] **Step 3: Implement**

Add before the `// ---- parts` section:

```scad
// ---- battery bay -------------------------------------------------------------------
// The bay is the open interior above the plate; the plate's ribs (Task 5) locate the cell.
module bay_ghost() translate([bx0, by0, 0]) cube([bay_w, bay_l, bay_t]);
module battery_ghost() translate([bx0 + bcl, by0 + bcl, 0]) cube([bat[1], bat[0], bat[2]]);
```

Extend the dispatcher (before the final `else`):

```scad
else if (part == "check_battery_void") difference() { bay_ghost(); inner_void(); }
else if (part == "check_battery_bosses") intersection() { bay_ghost(); bosses(); }
else if (part == "check_battery_board") intersection() { bay_ghost(); bf() pocket(); }
else if (part == "check_battery_roof") difference() { bay_ghost(); outer_inset(wall); }
```

(`outer_inset(wall)` approximates the inner roof; the bay must lie inside it.)

- [ ] **Step 4: Run to verify they pass**

Run: `docs/enclosure/test.sh`
Expected: all PASS. The 15 mm cell cases pass because `z0` and `z_back` are derived from `bay_t`. If `check_battery_board` fails nominally, raise the `16` in `z0 = max(16, bay_t + 3)` until it passes, then re-run the Task 3 tests.

---

### Task 5: Base plate, ribs, pushers, feet

**Files:**
- Modify: `docs/enclosure/stand.scad`
- Modify: `docs/enclosure/test.sh`

**Interfaces:**
- Consumes: `boss_xy`, `bay_ghost()`, `bosses()`, `bf()`, `pcb_slab()`, `pcb_rear`, `b2g()`, `flipp()`, `O`, `W`, `D`.
- Produces: `base_plate()` (shell frame: plate occupies Z ∈ [−plate_t, 0]), `pushers()`, `bay_ribs()`, `push_pts`; test parts `check_pusher_touch`, `check_pusher_battery`, `check_pusher_bosses`, `check_pusher_shell`, `check_plate_bosses`.

- [ ] **Step 1: Write the failing tests**

Append to `test.sh` before `exit $fail`:

```bash
expect_solid "pushers press into the PCB rear"    check_pusher_touch
expect_empty "pushers clear of the battery pocket" check_pusher_battery
expect_empty "pushers clear of the bosses"        check_pusher_bosses
expect_empty "pushers clear of the shell"         check_pusher_shell
expect_empty "plate ribs clear of the shell"      check_ribs_shell
expect_empty "plate ribs clear of the battery"    check_ribs_battery
expect_manifold "base plate" base_plate --max 48 86 30
expect_solid "pushers still touch (tilt 55)"      check_pusher_touch -D tilt=55
expect_solid "pushers still touch (tilt 75)"      check_pusher_touch -D tilt=75
expect_solid "pushers still touch (board_flip)"   check_pusher_touch -D board_flip=true
```

- [ ] **Step 2: Run to verify they fail**

Run: `docs/enclosure/test.sh`
Expected: FAIL (`unknown part: ...`).

- [ ] **Step 3: Implement**

Add before the `// ---- parts` section:

```scad
// ---- base plate --------------------------------------------------------------------
screw_clear_d = 2.4;  cb_d = 4.2;  cb_depth = 1.6;       // M2 clearance and counterbore (underside)
foot_d = 8;  foot_depth = 0.8;                           // rubber-foot recesses (underside)
foot_xy = [for (x = [8, W - 8], y = [D/2 - 20, D/2 + 20]) [x, y]];
rib_t = 0.8;  rib_h = 6;  rib_seg = 4;  rib_side = 12;   // short ribs leave a finger gap beside the cell
push_d = 3.0;  preload = 0.2;                            // pusher post diameter; interference into the PCB rear

// The two lowest PCB mounting-hole positions (world-lowest even with board_flip), at the PCB rear face.
push_local = [for (bb = [hole_ib, pcb_b - hole_ib])
              [pcb_b0 + bb, pcb_rear, pcb_a0 + (board_flip ? pcb_a - hole_ia : hole_ia)]];
push_pts = [for (p = push_local) b2g(flipp(p))];

// A vertical post from the plate, cut flat on the PCB rear plane (minus `preload`), so it presses the
// board up the slope and into the glass pocket.
module pusher(gp) difference() {
  translate([gp[0], gp[1], 0]) cylinder(d = push_d, h = gp[2] + 6);
  bf() translate([-100, -100, -100]) cube([300, 100 + pcb_rear - preload, 300]);
}
module pushers() for (gp = push_pts) pusher(gp);

module bay_ribs() {
  for (x = [bx0, bx0 + bay_w - rib_seg], y = [by0 - rib_t, by0 + bay_l])
    translate([x, y, 0]) cube([rib_seg, rib_t, rib_h]);                       // end ribs
  for (x = [bx0 - rib_t, bx0 + bay_w], y = [by0, by0 + bay_l - rib_side])
    translate([x, y, 0]) cube([rib_t, rib_side, rib_h]);                      // side ribs
}

module base_plate() difference() {
  union() {
    translate([0, 0, -plate_t]) cube([W, D, plate_t]);
    bay_ribs();
    pushers();
  }
  for (p = boss_xy) translate([p[0], p[1], -plate_t - 0.01]) {
    cylinder(d = screw_clear_d, h = plate_t + 0.02);
    cylinder(d = cb_d, h = cb_depth + 0.01);
  }
  for (f = foot_xy) translate([f[0], f[1], -plate_t - 0.01]) cylinder(d = foot_d, h = foot_depth + 0.01);
}
```

Extend the dispatcher:

```scad
else if (part == "base_plate") base_plate();
else if (part == "check_pusher_touch") intersection() { pushers(); bf() pcb_slab(); }
else if (part == "check_pusher_battery") intersection() { pushers(); bay_ghost(); }
else if (part == "check_pusher_bosses") intersection() { pushers(); bosses(); }
else if (part == "check_pusher_shell") intersection() { pushers(); front_shell(); }
else if (part == "check_ribs_shell") intersection() { bay_ribs(); front_shell(); }
else if (part == "check_ribs_battery") intersection() { bay_ribs(); battery_ghost(); }
```

- [ ] **Step 4: Run to verify they pass**

Run: `docs/enclosure/test.sh`
Expected: all PASS.

Likely tuning if one fails: `check_pusher_battery` fails → raise `end_zone` by 0.5 (re-run Tasks 2–4 tests); `check_pusher_shell` fails → reduce `push_d` to 2.6. Re-run the whole suite after any change.

---

### Task 6: Type-C slot and button pinholes

**Files:**
- Modify: `docs/enclosure/stand.scad`
- Modify: `docs/enclosure/test.sh`

**Interfaces:**
- Consumes: `bf()`, `pcb_b0`, `pcb_b`, `pcb_rear`, `usb_*`, `btn_a`, `W`, `wall`.
- Produces: `usb_cut()`, `button_cuts()` (both include their own `bf()`); `front_shell()` subtracts both; helper `wall_left()`, `wall_right()` slabs for tests.

- [ ] **Step 1: Write the failing tests**

Append to `test.sh` before `exit $fail`:

```bash
expect_solid "Type-C slot reaches the right wall"          check_usb_right
expect_empty "Type-C slot misses the left wall"            check_usb_left
expect_solid "buttons reach the left wall"                 check_btn_left
expect_empty "buttons miss the right wall"                 check_btn_right
# board_flip: the board is turned 180 deg, so the openings swap walls
expect_solid "flip: Type-C slot reaches the left wall"     check_usb_left  -D board_flip=true
expect_empty "flip: Type-C slot misses the right wall"     check_usb_right -D board_flip=true
expect_solid "flip: buttons reach the right wall"          check_btn_right -D board_flip=true
expect_manifold "front shell with openings" front_shell --max 48 86 63
```

- [ ] **Step 2: Run to verify they fail**

Run: `docs/enclosure/test.sh`
Expected: FAIL (`unknown part: check_usb_right`, etc.).

- [ ] **Step 3: Implement**

Add before the `// ---- parts` section:

```scad
// ---- openings --------------------------------------------------------------------------
usb_y = pcb_rear + usb_h/2;     // Type-C centre depth (VERIFY)
btn_y = pcb_rear + 0.8;         // side-button centre depth (VERIFY)
pin_d = 2.4;                    // pinhole for RST / BOOT / PWR

module usb_cut() bf()
  translate([pcb_b0 + pcb_b - 3, usb_y - usb_h/2 - 0.4, usb_a - usb_w/2 - 0.4])
    cube([W, usb_h + 0.8, usb_w + 0.8]);
module button_cuts() bf() for (a = btn_a)
  translate([pcb_b0 + 1.0 - W, btn_y, a]) rotate([0, 90, 0]) cylinder(d = pin_d, h = W);

module wall_left()  cube([wall, D, 100]);
module wall_right() translate([W - wall, 0, 0]) cube([wall, D, 100]);
```

Replace `front_shell()`:

```scad
module front_shell() difference() {
  union() { difference() { outer_solid(); inner_void(); } bosses(); }
  bf() { pocket(); window(); }
  usb_cut();
  button_cuts();
  boss_pilots();
}
```

Extend the dispatcher:

```scad
else if (part == "check_usb_right") intersection() { usb_cut(); wall_right(); }
else if (part == "check_usb_left")  intersection() { usb_cut(); wall_left(); }
else if (part == "check_btn_left")  intersection() { button_cuts(); wall_left(); }
else if (part == "check_btn_right") intersection() { button_cuts(); wall_right(); }
```

- [ ] **Step 4: Run to verify they pass**

Run: `docs/enclosure/test.sh`
Expected: all PASS. If a wall check fails, print the global slot position with `echo(b2g(flipp([pcb_b0 + pcb_b, usb_y, usb_a])));` inside a temporary `if (part == "debug")` branch, compare it to the wall faces (x = 0 and x = W), then fix the offending number, never the test.

---

### Task 7: Fit-check coupon, assembly view, exports, previews, README

**Files:**
- Modify: `docs/enclosure/stand.scad`
- Modify: `docs/enclosure/test.sh`
- Create: `docs/enclosure/stand-front.stl`, `stand-base.stl`, `fit-check.stl` (generated)
- Create: `docs/enclosure/preview-front.png`, `preview-back.png`, `preview-section.png` (generated)
- Create: `docs/enclosure/README.md`
- Modify: `docs/superpowers/specs/2026-10-02-desk-stand-enclosure-design.md` (record the deviations)

**Interfaces:**
- Consumes: everything above.
- Produces: `fit_check()` (a slice of the front shell around the pocket, including both side walls, so the glass fit, window, Type-C slot and pinholes can all be tried before the full print), `assembly()`, `section_view()`, `base_print()`, parts `front`, `base`, `fit`, `assembly`, `section`.

- [ ] **Step 1: Write the failing tests**

Append to `test.sh` before `exit $fail`:

```bash
expect_manifold "printable front shell" front --max 48 86 63
expect_manifold "printable base plate"  base  --max 48 86 30
expect_manifold "fit-check coupon"      fit   --max 48 60 70
expect_solid    "assembly renders"      assembly
```

- [ ] **Step 2: Run to verify they fail**

Run: `docs/enclosure/test.sh`
Expected: FAIL (`unknown part: front`, etc.).

- [ ] **Step 3: Implement**

Add before the `// ---- parts` section:

```scad
// ---- deliverables -------------------------------------------------------------------------
// Print the base plate right side up (flat underside on the bed); the shell prints as modelled.
module base_print() translate([0, 0, plate_t]) base_plate();

// A slice of the shell around the glass pocket, spanning both side walls: tests the glass fit,
// the window, the Type-C slot and the pinholes in about 20 minutes of printing.
module fit_check() intersection() {
  front_shell();
  bf() translate([-O[0], -lip_t - 1, -6]) cube([W, board_t + lip_t + 9, glass_a + 12]);
}

module assembly() {
  color("lightgray") front_shell();
  color("dimgray") base_plate();
  color("royalblue", 0.6) battery_ghost();
  color("black") bf() board_ghost();
}
module section_view() difference() {
  assembly();
  translate([-1, -1, -plate_t - 1]) cube([W/2 + 1, D + 2, 100]);   // remove the left half
}
```

Extend the dispatcher:

```scad
else if (part == "front") front_shell();
else if (part == "base") base_print();
else if (part == "fit") fit_check();
else if (part == "assembly") assembly();
else if (part == "section") section_view();
```

- [ ] **Step 4: Run to verify they pass**

Run: `docs/enclosure/test.sh`
Expected: all PASS, exit 0.

- [ ] **Step 5: Export the STLs**

```sh
cd docs/enclosure
B=$([ -n "$(\"$OPENSCAD\" --help 2>&1 | grep -- --backend)" ] && echo --backend=manifold)
"$OPENSCAD" $B -D 'part="front"' -o stand-front.stl stand.scad
"$OPENSCAD" $B -D 'part="base"'  -o stand-base.stl  stand.scad
"$OPENSCAD" $B -D 'part="fit"'   -o fit-check.stl   stand.scad
python3 tools/stlcheck.py stand-front.stl && python3 tools/stlcheck.py stand-base.stl && python3 tools/stlcheck.py fit-check.stl
```

Expected: three files, each reported watertight with 0 non-manifold edges.

- [ ] **Step 6: Render the previews and look at them**

```sh
"$OPENSCAD" $B -D 'part="assembly"' --render --imgsize=1400,1000 --camera=24,43,30,65,0,325,330 -o preview-front.png stand.scad
"$OPENSCAD" $B -D 'part="assembly"' --render --imgsize=1400,1000 --camera=24,43,30,65,0,145,330 -o preview-back.png stand.scad
"$OPENSCAD" $B -D 'part="section"'  --render --imgsize=1400,1000 --camera=24,43,30,90,0,90,330  -o preview-section.png stand.scad
```

Open each PNG (Read tool). Adjust the `--camera` rotation/distance until the part is framed. Confirm by eye: the window frames the display area; the battery lies flat below and behind the board with visible air above it; the pushers touch the lower rear of the board; the Type-C slot is on the right wall and the three pinholes on the left wall; the section shows the battery clear of the board. Fix any problem in `stand.scad` (re-run `test.sh`) and re-export.

- [ ] **Step 7: Write the README**

Create `docs/enclosure/README.md` with these sections (fill every number from `test.sh`/`stlcheck.py` output, nothing guessed):

1. **What it is**: one paragraph, the three previews embedded.
2. **Print list**: `fit-check.stl` first (about 20 min); then `stand-front.stl` and `stand-base.stl`. 0.2 mm layers, 3 perimeters, 20% infill, PLA or PETG. Front shell prints as modelled (open bottom on the bed) with supports for the roof and glass-pocket ceiling; supports sit inside the open cavity and come out through the bottom. Base plate prints flat, pushers upright, no supports.
3. **Hardware**: 4 × M2 × 8 self-tapping screws (or heat-set inserts with `insert_mode = true`), 4 rubber feet (8 mm), optional 1 mm foam tape for the pusher tips.
4. **Assembly order**: connect the battery to the board before closing; set the board in the glass pocket from behind, glass first; screw on the base plate (the pushers press the board's lower edge into the pocket); stick on the feet.
5. **Fit-check procedure**: what to check (glass drops in without force, window clear of the touch area, Type-C plug seats, a paper-clip reaches RST/BOOT/PWR) and which `VERIFY` parameters to edit for each failure; re-run `docs/enclosure/test.sh` after every edit.
6. **Battery polarity warning**: MX1.25 pinouts vary by vendor; compare the lead against the board's `+`/`-` marks before connecting. Battery pocket is 65.8 × 40.8 × 12.8 and must not be compressed.
7. **Antenna**: locate the chip antenna on the board photo; keep the cell's metal pouch away from it (gap shown in `preview-section.png`); the body is plastic only.
8. **If the screen is upside down**: set `board_flip = true` in `stand.scad`, re-export, and set `lcd.setRotation(2)` in `firmware/src/main.cpp`.

- [ ] **Step 8: Record the deviations in the spec**

Edit `docs/superpowers/specs/2026-10-02-desk-stand-enclosure-design.md`: replace "Board mount" with the pusher-post description, `mirror_sides` with `board_flip` (and the `setRotation(2)` note), drop the "cable channel" sentence, and update the envelope to about 48 × 86 × 64. Point to the "Deviations from the spec" section of this plan for the reasons.

- [ ] **Step 9: Final verification**

Run: `docs/enclosure/test.sh; echo exit=$?`
Expected: every line PASS, `exit=0`. Then report to the user: the three STL paths, the three preview PNGs, the fit-check-first instruction, and the list of `VERIFY` values that only a physical print can confirm.

---

## Self-review

**Spec coverage.** Goal and form (Tasks 2, 3); two parts + 4 × M2 (Tasks 2, 5); board mount (Task 3 pocket, Task 5 pushers, deviation 1); window over the active area (Task 3); battery bay (Tasks 4, 5); openings (Task 6); parameters including `insert_mode` (Task 2) and `board_flip` (Tasks 2, 6, deviation 2); deliverables: SCAD, front/base STL, `fit-check.stl`, README, previews (Task 7); verification items 1 to 3 (test.sh, previews, fit-check); risks: polarity, LiPo compression, antenna, orientation (README, Task 7 Step 7). Out of scope items are not planned. Gaps: none.

**Placeholders.** None. Every code step has full code; the only open numbers are the spec's own `VERIFY` defaults.

**Type and name consistency.** `bf()`, `pocket()`, `window()`, `pcb_slab()`, `board_ghost()`, `bay_ghost()`, `battery_ghost()`, `usb_cut()`, `button_cuts()`, `pushers()`, `bay_ribs()`, `base_plate()`, `base_print()`, `fit_check()`, `assembly()`, `section_view()` are each defined once and used with the same signatures later. `W`, `D`, `z0`, `O`, `bx0`, `by0`, `bay_*` are defined in Task 2 before use. Part names in `test.sh` match the dispatcher branches added in the same task.

**Unverified.** The OpenSCAD in this plan has not been run (OpenSCAD is not installed yet), so the first run of each task may need small numeric tuning; the tests are written to point at which number to change.
