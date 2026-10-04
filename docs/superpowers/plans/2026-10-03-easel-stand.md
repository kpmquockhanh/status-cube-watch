# Easel Stand Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the easel desk stand (concept A) next to the wedge: a tray for the cell, a frame lid with a fin, a carrier that holds the board at 65° and a bezel that hooks on at the bottom, with both stands sharing one copy of the board facts.

**Architecture:** `board.scad` takes the board facts, fit tunables and board-local helpers out of `stand.scad`. It is `include`d (not `use`d), so its frame helpers read the includer's `O` and `lean` when called. `easel.scad` models every part in its assembled (world) position and turns each one into print orientation only for output. `test.sh` drives both files through OpenSCAD's manifold backend with empty/solid intersection checks, each with a positive control. `tools/stability.py` weighs the assembled-position meshes and estimates the tip force.

**Tech Stack:** OpenSCAD (docker `openscad/openscad:trixie` via `3d-printing/tools/openscad`, `--backend=manifold`), bash, Python 3 standard library.

**Spec:** `docs/superpowers/specs/2026-10-03-easel-stand-design.md`

## Deviations from the spec (found while working out the geometry)

1. **Tip pivot.** A backward push rocks the stand on the rear edge of its rear feet, so the estimate pivots 1 mm inside that edge (y 77). The inner edge (y 70) gave about 2.2 / 1.1 N, which says more about the model than the stand. The spec's risk line is updated.
2. **Top pilots** sit at board (−0.561, 41.691) and (33.691, 41.691): on the 45° diagonal out of the glass corner arc, `(glass_r + clr + pilot_in + pilot_d/2)/√2` = 4.561 from its centre. That makes the pocket-side wall exactly `pilot_in` = 1.25 at any `clr`; the outer wall is 1.63 (1.44 at `clr` 0.5). The spec's (−0.525, 41.655) left 1.199. Blind, 0.8 behind the face.
3. **Tilt turns the board about O; the base stays put.** At 75° the leg counterbore and screw head break 0.46 mm through the leg front, so the head-in-leg check runs at 55° and 65° and uses 75° as its control. At 55° P sits 1.4 mm ahead of the tray front and the carrier plate covers the rear 0.8 mm of the lead slots, so the lead-path check runs at 65° only.
4. **Cell names.** The cell keeps the wedge's names, `bat = [65, 40, 12]` and `bcl = 0.4`. A 15 mm cell raises the deck, and everything above it moves up 3 mm.
5. **Outputs** go to `build/<scad>-<part>.*`, so the two stands never overwrite each other's renders.
6. **Touching faces.** The manifold backend counts a shared face as an overlap. Faces that touch by design are pulled apart by `nudge` = 0.02 in the collision checks: carrier and bezel bottoms on the deck, bezel rear on the carrier plate, lid on the tray walls, cell on the tray floor, glass on the lip. The carrier/board check runs at `preload = -0.05` against a PCB ghost with its Ø2.2 holes. "Shoulders press" is `check_shoulders` (posts ∩ PCB must be solid); "pegs in holes" is `check_pegs` (pegs ∩ holed PCB must be empty). With `board_flip` the posts follow the holes to board z 7.47 / 36.27.
7. **Stability inputs.** `at_tray`, `at_frame`, `at_carrier`, `at_bezel` (PLA), `at_cell` (55 g) and `at_board` (12 g) are the parts in place. `part="meta"` echoes `EASEL{lean, press, top, pivot_y, front_y}` into `build/easel-meta.echo`. CLI: `python3 tools/stability.py build/easel --min-center 3.0 --min-top 1.3 [--cell-g G] [--board-g G]`, exit 1 on a miss; its control is `--cell-g 0`.
8. **Print transforms.** Carrier `translate([0,0,back_y]) rotate([lean-90,0,0]) translate(-O)`, i.e. (x_b, z_b, 9.7 − y_b), rear face down. Bezel `translate([0,0,lip_t]) rotate([90+lean,0,0]) translate(-O)`, i.e. (x_b, −z_b, y_b + 1), face down. Frame `translate([0,0,-tray_h])`, lid down. Tray as modelled. The cap is modelled flange down.
9. **Envelopes** (print orientation): tray 46 × 81 × 15.2, frame 46 × 81 × 36.3, carrier 39.5 × 54.5 × 9.2, bezel 39.5 × 55.6 × 8.3, cap 17.2 × 9.5 × 2.6. The tests allow about 0.5 mm over.
10. **Assembly order.** The vertical leg-screw axis passes through the glass (board z −0.1 to 3.3), so the leg screws go in before the board, as in spec steps 3–4. The README says why.
11. **No preview PNGs.** OpenSCAD's PNG export fails in the docker image (GLAD), so the Easel section has no images.
12. **D = 81, not 80.** At 80 the rear counterbore (Ø4.2 at y 77.9) is tangent to the lid's rear edge; at 81 it keeps a 1.0 wall. `check_rear_wall` guards it, control `D = 80`. The fin ends at y 65.
13. **Lead path.** The lead-path check is the slot footprint, inset 0.05, from z `tray_h − 3` to `deck + 1`. Above that the chin wall leans over the slot's front, so the lead bends back into the chin cavity.
14. **Bezel/frame control.** The fin never reaches the bezel (the bezel is open behind the glass), so `check_bezel_frame` only guards the bezel's bottom on the deck; its control is `nudge = -0.02`.

## Global Constraints

- Units mm. World: X = width, Y = depth (front y = 0), Z = up, desk z = 0. Board-local: x = portrait right, y = into the stand from the glass front, z = portrait up, origin at the glass's front-bottom-left corner.
- OpenSCAD runs through `3d-printing/tools/openscad` (docker, run from `3d-printing/`) with `--backend=manifold`. Renders land in `3d-printing/build/` (gitignored, outputs root-owned: `cp` deliverables out).
- `board.scad` has no top-level geometry and no `$fn`; includers put `include <board.scad>` right after `$fn = 48;`. No variable is assigned in two files.
- The wedge is unchanged: `stand.scad` renders the same STLs before and after the move.
- FDM without supports: overhangs at most about 45° from vertical, apart from small bridges (foot recesses, counterbore floors, the fin window apex).
- Screws only, no glue: 5 × M2 self-tapping into Ø1.8 pilots (2 × M2 × 6 top, 2 × M2 × 10 legs, 1 × M2 × 8 rear), plus 4 × Ø8 rubber feet.
- The whole active area stays open; Type-C slot, plug relief and optional cap; RST/BOOT/PWR pinholes; plastic only near the antenna.
- Stability: at least 3.0 N at the centre of the active area and 1.3 N at its top edge.
- `board_flip` needs no firmware change (the IMU auto-rotates).
- Every check has a positive control that must fail.
- No firmware, bridge or mac-helper changes. Commit only the files a task names, by path.

## Review Focus

Failure modes the spec implies that a user is most likely to hit; each has a test in the task that owns the code.

1. **Another tilt (55° / 75°).** A user changing `tilt` expects watertight parts and a screw head that stays inside the leg where it can. Tests in Tasks 3–5 (manifold at 55/75, head check at 55/65, 75 as control).
2. **A thicker cell (`bat = [65, 40, 15]`).** Everything above the deck must move up without new collisions. Test in Tasks 3–5 (`fit_check` variants).
3. **A looser fit or a thicker board (`clr = 0.5`, `board_t = 9`).** Pocket, cavity, pilots and plate must follow. Tests in Tasks 4–5 (`fit_check` variants, pilot wall at `clr` 0.5).
4. **`board_flip`.** Window, posts, openings and cap must follow the board. Tests in Tasks 4–6.
5. **A board sagging 0.2 on its pegs while the bezel swings on.** The lowered chin wall must clear it. Test in Task 5 (`check_swing` at sag 0.2, control `chin_top = -0.3`).

---

### Task 1: Extract the board facts into `board.scad`

**Files:** Create `3d-printing/board.scad`. Modify `3d-printing/stand.scad`.

**Interfaces.** `board.scad` produces the board facts (glass, active area, PCB, holes, buttons, Type-C, `win_margin`), the fit tunables `board_flip`, `lip_t`, `clr`, `win_cham`, the derived `pcb_rear`, `usb_y`, `btn_y`, `pin_d`, the functions `b2g`, `flipp`, and the modules `xz_extrude`, `rrect`, `round2d`, `across`, `board_frame`, `flip`, `bf`, `pocket(c = clr, extra = 0)`, `win_rect`, `window`, `pcb_slab`, `board_ghost`, `active_prism`, `usb_slot(reach)`, `usb_plug_relief(reach)`, `button_holes(reach)`. It consumes the includer's `O` and `lean`.

- [ ] **Step 1: Baseline renders.** From `3d-printing/`:
  `for p in front base fit cap assembly section cap_fitted; do tools/openscad --backend=manifold -D "part=\"$p\"" -o build/pre-$p.stl stand.scad; done`
  Expected: seven STLs, no ERROR.
- [ ] **Step 2: Write `board.scad`** with the blocks listed above, moved verbatim. The `board_flip` comment becomes "(no firmware change: the IMU auto-rotates)". The openings become board-local modules taking a `reach` (the old `W`).
- [ ] **Step 3: Edit `stand.scad`.** Delete the moved lines, add `include <board.scad>` after `$fn = 48;`, and keep `usb_cut()`, `usb_plug_cut()` and `button_cuts()` as `bf()` wrappers with `reach = W`.
- [ ] **Step 4: Compare.** Render the same seven parts to `build/post-*.stl` and run `cmp build/pre-$p.stl build/post-$p.stl` for each.
  Expected: identical files (OpenSCAD output is deterministic for the same CSG).
- [ ] **Step 5: Wedge tests.** `./test.sh` → every line PASS.
- [ ] **Step 6: Commit** `3d-printing/board.scad 3d-printing/stand.scad`: "refactor(3d): move the board facts into board.scad".

### Task 2: Two-file `test.sh` and the `easel.scad` skeleton

**Files:** Modify `3d-printing/test.sh`. Create `3d-printing/easel.scad`.

**Interfaces.** `test.sh [stand|easel]`. Helpers `render`, `expect_empty`, `expect_solid`, `expect_manifold`, `expect_error` work on `$SCAD` and `$VARIANT` and write `build/<scad>-<part>.{stl,log}`. New: `expect_pass label cmd…`, `expect_fail label cmd…`. `easel.scad` produces the tunables, derived values (`lean`, `deck`, `O`, `P`, bay and tray numbers), `cell_ghost()`, `at_cell`, `at_board`, `meta`.

- [ ] **Step 1: Generalise `test.sh`.** `set -f` (the `-D` values contain brackets), the argument filter, `out()`, `$SCAD`/`$VARIANT` in `render`, `expect_pass`/`expect_fail`, and `run()`. Wrap the existing wedge body in `if run stand; then SCAD=stand.scad … fi` and add an `if run easel; then SCAD=easel.scad … fi` block.
- [ ] **Step 2: Failing tests first.** In the easel block: `expect_empty none`, `expect_error "unknown part is rejected" bogus`, `expect_solid at_cell`, `expect_solid at_board`, and a meta check (`render meta`, then grep the echo for `EASEL{`).
  Run: `./test.sh easel` → FAIL (no `easel.scad`).
- [ ] **Step 3: Write the skeleton.** Header, `part`, `$fn`, tunables (`tilt`, `W` 46, `D` 81, `wall`, `lid_t`, `bat`, `bcl`, `front_space`, the screw sizes), `include <board.scad>`, derived values, `cell_ghost()`, `meta` echo, and dispatch ending in `assert(false, …)`.
- [ ] **Step 4:** `./test.sh easel` → PASS. `./test.sh stand` → PASS.
- [ ] **Step 5: Commit** `3d-printing/test.sh 3d-printing/easel.scad`.

### Task 3: Tray and frame

**Files:** Modify `3d-printing/easel.scad`, `3d-printing/test.sh`.

**Interfaces.** `tray()`, `frame()` in world position; `tray_print()`, `frame_print()`; `fin()`; `lead_slots()`; `leg_screw_xy`, `rear_screw_xy`.

- [ ] **Step 1: Tests.**
  - `check_cell_tray` (cell ghost ∩ tray, cell raised by `nudge`) empty; control `front_space=6` fails.
  - `check_cell_frame` (cell ghost ∩ frame) empty; control `bcl=-0.1`.
  - `check_lead_path` (slot footprint inset 0.05, z `tray_h−3`..`deck+1`, minus air) empty at tilt 65; control `'lead_x=[12,30]'`.
  - `check_rear_wall` (rear counterbore grown by 1.0 minus the lid) empty; control `D=80`.
  - Manifold `tray --max 46.5 81.5 15.7` and `frame --max 46.5 81.5 36.8`, at tilt 55/65/75 and `bat=[65,40,15]` (frame envelope then +3).
- [ ] **Step 2:** `./test.sh easel` → FAIL on the new lines.
- [ ] **Step 3: Model the tray**: rounded box (r 4), 0.8 chamfer under, bay void, three pilasters with Ø1.8 pilots to z 5.2, webs to the front wall, four foot recesses.
- [ ] **Step 4: Model the frame**: lid with the corner radius, leg holes Ø2.4, rear hole with counterbore, two lead slots, and the fin (profile from p0/p1/p2, `fin_gap` behind the carrier plate, windowed).
- [ ] **Step 5:** `./test.sh easel` → PASS. Commit.

### Task 4: Carrier

**Files:** Modify `3d-printing/easel.scad`, `3d-printing/test.sh`.

**Interfaces.** `carrier()`, `carrier_print()`, `post_pts`, `ridge()`, `top_pilot_pts`, `holed_pcb()`.

- [ ] **Step 1: Tests**, each with its control:
  - `check_carrier_frame` empty; control `fin_gap=-0.1`.
  - `check_carrier_board` (carrier ∩ board ghost, `preload=-0.05`, holed PCB) empty; control `nudge=-0.02`.
  - `check_shoulders` solid (posts ∩ PCB at nominal preload).
  - `check_pegs` (pegs ∩ holed PCB) empty; control `hole_ib=3`.
  - `check_leg_head` (head ghost minus leg) empty at 55 and 65; control tilt 75.
  - `check_carrier_tray`, `check_carrier_cell` empty.
  - Manifold `carrier --max 40 55 9.7` at 55/65/75, `board_flip=true`, `clr=0.5`, `board_t=9`.
- [ ] **Step 2:** FAIL. **Step 3:** model it (plate, posts with shoulders and pegs, legs, leg counterbores, ridges, top holes with rear counterbores, plug relief). **Step 4:** PASS. **Step 5:** Commit.

### Task 5: Bezel and hinge

**Files:** Modify `3d-printing/easel.scad`, `3d-printing/test.sh`.

**Interfaces.** `bezel()`, `bezel_print()`, `swing(a)`, `grooves()`, `chin_cavity()`, `fit_check()`.

- [ ] **Step 1: Tests**, each with its control:
  - `check_window` empty at 55/65/75 and with `board_flip`.
  - `check_pilot_wall` empty (pilot grown by 1.2 minus the bezel), also at `clr=0.5`; control `pilot_in=0.5`.
  - `check_ridge_groove` (ridge minus groove) empty; control `nudge=-0.02`.
  - `check_swing` (bezel swung about P ∩ carrier + board) empty at 5/10/20/30°, sag 0 and 0.2; control `chin_top=-0.3` at sag 0.2.
  - `check_bezel_carrier` empty; control `groove_clr=-0.1`.
  - `check_bezel_frame` empty; control `nudge=-0.02`.
  - `check_slot_cavity` (lead path ∩ bezel) empty.
  - `check_head_bezel` (screw heads ∩ bezel) empty.
  - Manifold `bezel --max 40 56.1 8.8` at 55/65/75, `board_flip`, `clr=0.5`, `board_t=9`; `fit --max 47 30 30`.
- [ ] **Step 2:** FAIL. **Step 3:** model it. **Step 4:** PASS. **Step 5:** Commit.

### Task 6: Type-C cap

**Files:** Modify `3d-printing/easel.scad`, `3d-printing/test.sh`.

**Interfaces.** `ecap()`, `ecap_place()`, `ecap_print()`.

- [ ] **Step 1: Tests:**
  - `check_cap_relief` (plug body ∩ assembled bezel and carrier) empty; control `cap_clr=-0.1`.
  - `check_cap_seat` (flange seat face minus the parts) empty; control `cap_flange=3`.
  - `check_cap_ribs` (ribs ∩ bezel) solid; control `cap_rib=0`.
  - `check_cap_board` empty; also with `board_flip`.
  - Manifold `cap --max 17.7 10 3.1`.
- [ ] **Step 2:** FAIL. **Step 3:** model it. **Step 4:** PASS. **Step 5:** Commit.

### Task 7: `tools/stability.py`

**Files:** Create `3d-printing/tools/stability.py`. Modify `3d-printing/tools/stlcheck.py` (add a `__main__` guard so `triangles` can be imported), `3d-printing/test.sh`.

**Interfaces.** `python3 tools/stability.py build/easel [--min-center N] [--min-top N] [--cell-g G] [--board-g G] [--density G_PER_MM3]`. It reads `build/easel-at_{tray,frame,carrier,bezel}.stl` (PLA, 1.116e-3 g/mm³ = 1.24 × 0.9), `build/easel-at_cell.stl` and `build/easel-at_board.stl` (point masses spread over their volume), and `build/easel-meta.echo`. It prints mass, CoG, and F at the centre and top press points, and exits 1 if either falls short or the stand is statically unstable.

- [ ] **Step 1: Test lines** in the easel block: render the six `at_*` parts and `meta`, then `expect_pass "tip force" python3 tools/stability.py build/easel --min-center 3.0 --min-top 1.3` and `expect_fail "tip force control" … --cell-g 0 --min-center 3.0 --min-top 1.3`.
- [ ] **Step 2:** FAIL (no script). **Step 3:** write it: signed-tetrahedron volume and centroid per mesh; F = W·(piv_y − cog_y)/(s_z·cos L − (piv_y − s_y)·sin L), where s is the press point and L the lean; if the denominator is ≤ 0 the push cannot tip the stand. **Step 4:** PASS. If the force falls short, lower `foot_in` or raise `D`, and note it here. **Step 5:** Commit.

### Task 8: Exports and README

**Files:** Create `3d-printing/easel-{tray,frame,carrier,bezel,usb-cap}.stl` and `3d-printing/easel-fit-check.stl`. Modify `3d-printing/README.md`.

- [ ] **Step 1: Export.** Render `tray frame carrier bezel cap fit_check` into `build/`, then copy them to the deliverable names. Check each one with `stlcheck.py`.
- [ ] **Step 2: README.**
  - Intro: two stands and `board.scad`.
  - New "Easel stand" section: print list and orientation, hardware, assembly order (leg screws before the board, and why), the hinge, the stability figures, the VERIFY items, and "print the carrier first".
  - Fixes: the `docs/enclosure/test.sh` paths, the `setRotation` note (the IMU auto-rotates), and the test usage.
- [ ] **Step 3: Full run.** `./test.sh` → all PASS.
- [ ] **Step 4: Commit** the STLs and the README.

## Self-review

- **Spec coverage.**
  - Every Deliverables bullet maps to a task: `board.scad` → 1, test.sh → 2, the parts → 3–6, `stability.py` → 7, STLs and README → 8.
  - Every Verification bullet has a check in the task that owns its code.
  - Pilot walls are checked at 1.2.
- **Controls.** Every `expect_empty` in the easel block has a matching control that must fail. Solid checks (shoulders, ribs) use a margin set to zero.
- **Names.**
  - `bat`/`bcl` match the wedge.
  - `nudge`, `fin_gap`, `groove_clr`, `chin_top`, `pilot_in` and `cap_*` are introduced in the task that first uses them.
  - The easel's cap modules are `ecap*`, so they never clash with `stand.scad`'s, which is not included.
- **Risk.** OpenSCAD has no namespaces. A name assigned in both `board.scad` and `easel.scad` silently takes the last value. Every new tunable is grepped against `board.scad` before it is added.

## Found during implementation

- **Zero-volume slivers.** Manifold reports coplanar touching faces as geometry, so every contact-by-design check separates the parts by `nudge` (0.02) or grows a cutter by it: the carrier/bezel seam in `check_cap_seat` (`fwd(nudge) carrier()`), the cap flange in `check_cap_relief`, the pilot bore in `check_pilot_wall` (subtracted at `pilot_d + 2*nudge`, probe starting 0.05 behind the pilot floor), the cap seat ring (built in cap coordinates, minus the opening grown by `nudge`).
- **Pocket corners squared.** In the swing check, the round lower pocket corners swept into the glass corners (board sagged 0.2) and the PCB corners (5 deg). The bezel now squares them from the glass face back, `glass_r + 1` high (taller cut the flipped BOOT pinhole and left non-manifold edges).
- **Controls that differ from the plan:** `check_carrier_board` uses `peg_d=2.6`; `check_pegs` uses `hole_d=1.6` (`hole_ib` would move the posts too); `check_shoulders` uses `preload=-0.05`, which must come out empty; `check_ridge_groove` is ridges ∩ bezel with `groove_clr=-0.1`; `check_window` uses `win_margin=-2`; `check_cap_ribs` uses `cap_rib=0.1` (0 would be a degenerate solid).
- `check_rear_wall` uses a 0.9 margin; the rear screw is tied to the bay (`bay_y1 + 2.2`) so the `D=80` control works.
- `check_slot_cavity` folded into `check_lead_path`, which intersects the lead path with tray, frame, carrier and bezel together.
- **No fit coupon.** `easel-fit-check.stl` is dropped: `check_fit` (all part pairs, seven variants) covers the clearances, and the carrier itself is the fit print (pegs against the real board).
- Added `check_cap_rib_carrier`: the ribs stay in the bezel, so the bezel still swings off.
- `expect_manifold` passes `-D` arguments to OpenSCAD and `--max` to `stlcheck`; `stlcheck` got its `__main__` guard early, while debugging.
- **Battery lead route (added after review).** The plan had no path for the lead from the board's MX1.25 BAT header to the lid slot. The header sits on the PCB rear beside the Type-C and opens towards the board centre (Waveshare rear view), so the lead leaves flat across the PCB rear, between parts up to `board_t`. The carrier plate went 2.4 → 3.2 mm. Its front face now has a 3 mm wide, 1.6 mm deep groove: across, down at `cord_x`, past the lower post, out under the PCB edge to the chin. With `board_flip` the route mirrors in x and still runs down. `check_cord` runs a plug ghost and a Ø1.6 lead to the slot. It checks against every part, the board, and a rear-parts envelope (the PCB rear up to `board_t`, minus the header and `bat_exit`). Controls: `cord_depth=0.4`, `bat_h=4.5`, `bat_exit=1`.
- Stability at nominal: 111.0 g; 3.27 N centre, 1.48 N top (109.5 g; 3.21 / 1.45 before the thicker plate). No change to `foot_in` or `D` was needed.
- Tests: 234 PASS (102 wedge, 132 easel).
