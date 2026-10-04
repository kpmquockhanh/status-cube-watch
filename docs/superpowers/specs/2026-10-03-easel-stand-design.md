# Easel desk stand — design

Date: 2026-10-03 · Status: approved; plan: docs/superpowers/plans/2026-10-03-easel-stand.md

## Goal

A second printed stand for the status cube, chosen from the concepts page as **A · Easel**.
The board stands at 65° in a slim bezel, carried on a low tray that holds the 12 × 40 × 65 mm
LiPo flat. Compared with the wedge (`3d-printing/stand.scad`) it looks lighter, it keeps the cell in its
own tray, and every part prints flat without supports. It lives alongside the wedge, and the two
share one copy of the board facts.

## Constraints

Carried over from the wedge spec (`docs/superpowers/specs/2026-10-02-desk-stand-enclosure-design.md`):
- the whole active area stays open, with no overlay;
- Type-C stays reachable, with a plug relief and an optional blanking cap;
- RST/BOOT/PWR are reachable through pinholes;
- plastic only near the antenna;
- FDM without supports.

New:
- **One source of truth for the board.** Move the board facts, fit tunables and board helpers out of
  `stand.scad` into `board.scad`. After the move, the wedge must render the same parts.
- **Stable under touch.** A press on the centre of the active area of at least 3.0 N must not tip the
  stand backwards, and neither must 1.3 N on its top edge. These are estimates from the meshes, not
  measurements.
- **Screws only, no glue:** 5 × M2 self-tapping.
- **`board_flip` needs no firmware change**, because auto-rotate is always on.

## Source dimensions

All board numbers come from `board.scad`, moved out of `stand.scad` unchanged, including its VERIFY
defaults. The easel adds three VERIFY items:
- **PCB mounting-hole positions.** The carrier's pegs go into these holes. They are assumed symmetric,
  as in the wedge spec. `peg_l = 0` turns the pegs off.
- **Where the MX1.25 connector sits** on the board's rear, and the route of the battery lead to it.
- **Which end of the cell the tabs leave from.** The design assumes the front.

## Frames and placement

- **World frame:** X = width, Y = depth (front edge y = 0), Z = up, with the desk at z = 0.
- **Board-local frame**, as in `stand.scad`: x = portrait right, y = into the stand from the glass
  front, z = portrait up. The origin is the glass's front-bottom-left corner.
- **Lean:** the board leans back by `lean = 90 − tilt` = 25°.
- **Origin:** `O = [(W − glass_b)/2, 6.2, deck + 9.1]`. `deck` = 17.6 is the top of the frame lid.
- **Deck plane** in board-local coordinates: z = −10.04 + 0.466·y.
- **Hinge line P:** the bezel's front-bottom edge, at board (y −1, z −10.51), which is world (y 0.85,
  z 17.6). The bezel swings about P.

## Design

### Form
Four printed parts and an optional cap:
- **Tray** (46 × 81 × 15.2): holds the cell flat.
- **Frame:** a 2.4 mm lid with a central fin; it closes the tray.
- **Carrier:** stands on the lid at 65° and holds the board from behind.
- **Bezel:** frames the glass from the front and hooks onto the carrier at the bottom.

The envelope is about 46 × 81 × 68 mm (W × D × H). The 55 g cell sits low and mostly behind the screen. That keeps the
centre of gravity far in front of the rear feet, which stops a press on the screen from tipping the stand backwards.

### Parts

#### 1. Tray
Prints open side up.
- Walls and floor 2.4 mm.
- **Battery bay** 40.8 × 65.8 × 12.8 (the cell plus 0.4 per side) at x 2.6–43.4, y 9.9–75.7, tab end
  towards the front.
- **Front space** (y 2.4–9.9, 7.5 mm deep) holds:
  - the bend of the battery lead;
  - two Ø4.2 screw pilasters at (14.5, 7.6) and (31.5, 7.6), webbed to the front wall.
- **Rear pilaster** Ø4.2 at (23, 77.9), merged into the rear wall.
- **Pilots:** Ø1.8 in every pilaster.
- **Feet:** four Ø8 × 0.8 recesses for rubber feet, 7 mm in from the corners.
- **Bottom edge:** 0.8 mm chamfer.

#### 2. Frame
Prints lid down.
- **Lid:** 2.4 thick, W × D, corner radius 4.
- **Holes:**
  - Ø2.4 clearance for the two leg screws;
  - Ø2.4 with a Ø4.2 × 1.6 counterbore for the rear screw;
  - two lead slots, 4.5 × 5.5, one beside each carrier leg, over the tray's front space.
- **Fin:** 5 mm thick, on the centre line, lightened with a window.
  - It sits 0.2 mm behind the carrier's rear face, up to board z 32, and runs back to y 65 on the lid.
  - Its job is to stop a press on the screen from flexing the carrier backwards.

#### 3. Carrier
Prints rear face down. The legs stand at 65°, so they need no supports.
- **Back plate:** the panel outline (x −3.2 to 36.33, top z 45.13, r 4) from y 7.3 to 9.7, cut by the deck.
- **Posts:** four, at the PCB holes.
  - Ø3 shoulders reach y 3.0, which preloads the PCB rear (y 3.2) by 0.2.
  - Ø1.8 × 1.0 pegs enter the holes. They hold the board in place while the bezel goes on.
- **Legs:** two, 6 mm wide, under the board.
  - Position: x 5.065–11.065 and 22.065–28.065; y 1.5–7.3; from the deck up to z −1.3.
  - Each takes a vertical M2 × 10 from above, at world (14.5, 7.6) and (31.5, 7.6). The Ø4.2 counterbore floor is 2.4 above the deck.
  - The screw passes through the leg and the lid into the tray pilaster, so one screw clamps all three parts.
- **Ridges:** one on each leg's front face, 1 mm proud (y 0.5–1.5). Each is an arc band from radius 3.2 to 4.4 about P.
- **Top-screw holes:** two Ø2.4 holes, with Ø4.2 × 1.6 counterbores from the rear face.
- **Plug relief:** the Type-C relief continues 1.3 mm into the back plate (y 7.3–8.6).

#### 4. Bezel
Prints face down.
- **Body:** the panel outline from y −1 (front face) to 7.3, cut by the deck. A 0.6 chamfer runs along the top and side edges of the front face.
- **Glass pocket and window:** the same as the wedge, with the chamfered window.
- **Open chin cavity:** x −0.3 to 33.43, from y 1.2 to the rear, and everything below the pocket. It
  leaves room for the legs, the screw heads and the battery lead.
- **Chin wall** (y −1 to 1.2): its top is lowered to z −0.6, so it clears a board that sags on its
  pegs while the bezel swings.
- **Grooves** for the ridges, cut into the chin wall's rear face:
  - an arc band from radius 3.0 to 4.6 about P;
  - from y 0.3 to the rear face;
  - across each leg's width plus 0.3 per side.
- **Top-screw pilots:** two, Ø1.8, from y 7.3 to −0.2, at board (−0.561, 41.691) and (33.691, 41.691).
  These are the corners between the glass and the outline, and both keep walls of at least 1.2.
- **Openings:** the Type-C slot, plug relief and RST/BOOT/PWR pinholes, as in the wedge. They follow `board_flip`.

#### 5. USB cap
Optional; prints flange down.
- A friction plug for the blind plug relief.
- Crush ribs on its ±z faces, only on the part of the plug that sits in the bezel.
- A 1.0 flange plus a pull ear, sitting on the side face at board x 36.33.

### Hinge
- Ridges and grooves are concentric about P, so swinging the bezel about P slides each groove along
  its ridge. The ridge leaves through the groove's open rear face, and nothing else meets.
- When closed, the ridges stop the chin lifting or sliding forward. The grooves also locate it sideways.
- The two top screws then fix the bezel to the carrier.

### Assembly
1. Put the cell in the tray, tab end forwards. Thread the lead up through the slot on the board's connector side.
2. Put the frame on and fit the rear M2 × 8.
3. Stand the carrier on the lid. Fit two M2 × 10 through the legs; they clamp carrier, lid and tray together.
4. Plug the lead into the board and press the board onto the pegs, glass outwards.
5. Hold the bezel tilted forward with its front-bottom edge on the lid. Swing it back over the glass, so the grooves pick up the ridges.
6. Fit two M2 × 6 from the carrier's rear.

To reach the board or the lead later, remove the two top screws and swing the bezel off.

### Hardware
- **Screws** (self-tapping into Ø1.8 pilots):
  - 2 × M2 × 6 for the top;
  - 2 × M2 × 10 for the legs;
  - 1 × M2 × 8 for the rear.
- **Feet:** 4 × Ø8 rubber feet.

### Parameters
- **`board.scad`:** the board facts, fit tunables and board helpers. It has no top-level geometry.
  - Facts: glass, active area, PCB, holes, openings.
  - Tunables: `clr`, `lip_t`, `win_*`, `board_flip`.
- **`easel.scad`:**
  - `tilt`, W, D and `wall`;
  - cell size and clearance;
  - leg, ridge, peg and screw positions, and the swing margin;
  - `part`, which selects the output.

## Deliverables

- **Shared file:** `3d-printing/board.scad`, with `stand.scad` changed to include it. The wedge output is unchanged.
- **`3d-printing/easel.scad`**, with these parts:
  - `tray`, `frame`, `carrier`, `bezel` and `cap`, in print orientation;
  - `assembly` and `section`, for viewing;
  - `at_*`, the parts in their assembled positions, for analysis.
- **STLs:** `3d-printing/easel-tray.stl`, `easel-frame.stl`, `easel-carrier.stl`, `easel-bezel.stl` and `easel-usb-cap.stl`.
- **`3d-printing/tools/stability.py`:** works out mass, centre of gravity and tip force from the `at_*` STLs
  and the reference points that `easel.scad` echoes.
- **`3d-printing/test.sh`:** covers both stands. `./test.sh stand` or `./test.sh easel` runs one.
- **README:** an Easel section. The stale `docs/enclosure/` paths and the old `setRotation` note are fixed.

## Verification

`./test.sh` runs OpenSCAD with the manifold backend and checks the following.

**Parts**
- Every part renders, is manifold and stays inside its size envelope.
- Each part also renders at tilt 55, 65 and 75, and with `board_flip`.

**Fit and clearance**
- No part collides with another part, the board ghost or a cell ghost.
- This also holds with a 15 mm cell, with `clr` 0.5 and with a 9 mm board.
- The window clears the active area.
- Pilot walls are at least 1.2.
- The leg screw heads sit inside the leg and clear the bezel.
- The lead slots open into the chin cavity above and the tray's front space below.

**Hinge**
- The ridges sit inside the grooves.
- With the bezel swung 5, 10, 20 and 30° about P, it clears the carrier and the board, and the board is
  checked both at nominal and sagged 0.2 mm on its pegs.

**Board mount**
- The pegs sit inside the PCB holes.
- The shoulders press on the PCB.
- The cap seats in the relief.

**Stability**
- `stability.py` reports at least 3.0 N at the centre of the active area and at least 1.3 N at the top edge.

Every check also has a positive control that must fail, such as a part moved or a margin removed. That
catches a check that could never fail.

**After the tests:** print the carrier first and check the pegs against the real board before printing the rest.

## Risks

- **PCB hole positions are assumed.** If the pegs are wrong, the board will not seat. Shorten the pegs
  (`peg_l`) or turn them off; the shoulders still press the PCB.
- **Tip force is an estimate.** It assumes 0.9 solidity at 1.24 g/cm³, a 55 g cell and a 12 g board, and
  it takes a pivot 1 mm inside the rear edge of the rear feet.
- **The hinge has 0.2 mm clearances in printed plastic.** If it binds, sand the ridges or reduce `ridge_t`.
- **Lead routing.** The lead must pass beside a leg and behind the PCB to the MX1.25. Check this on the first build.
- **Battery polarity and LiPo safety:** as in the wedge README.

## Out of scope

- Replacing the wedge. It stays, and choosing between the two is the user's call.
- Adjustable tilt.
- Firmware or bridge changes.
