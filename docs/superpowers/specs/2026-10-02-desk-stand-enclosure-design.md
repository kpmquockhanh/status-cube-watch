# Desk stand enclosure — design

Date: 2026-10-02 · Status: implemented; see the plan's "Deviations from the spec" (docs/superpowers/plans/2026-10-02-desk-stand-enclosure.md) for what changed and why

## Goal

A 3D-printable desk stand for the Claude status cube: it holds the Waveshare
ESP32-S3-Touch-LCD-1.69 and a 12 × 40 × 65 mm LiPo, with the screen tilted back
for reading on a desk. Output is parametric OpenSCAD plus print-ready STLs.

## Constraints (from the project and the user)

- Touch must stay fully usable: tap, swipe, and the Pomodoro 600 ms / 2 s
  long-press all use the CST816T panel, so the active area is an open window with no overlay.
- Type-C stays reachable with the battery installed (flashing, charging, serial).
- RST, BOOT and PWR stay reachable. BOOT is held through boot for the 5 s
  setup-portal entry and for download mode.
- The chip antenna (2.4 GHz) is not covered by the battery or any metal; the body is plastic only.
- FDM printable without a multi-material setup; support use minimised.
- The user chose: **angled wedge**, **two-part shell closed with 4 × M2 screws**.

## Source dimensions

From the Waveshare drawing in `docs/ESP32-S3-Touch-LCD-1.69.md` (landscape view):

| Item | Value (mm) |
|---|---|
| Glass outline | 41.13 × 33.13, rounded corners |
| Active area | 32.634 × 27.972 |
| PCB | 37.12 × 29.83 |
| Mounting hole centre | about 4.16 from PCB left edge, about 1.75 from PCB top edge (top-left hole; assumed symmetric) |
| Battery | 12 × 40 × 65 |

**Not in the drawing, set as defaults and marked `VERIFY` in the SCAD file:**
total board thickness (7), mounting hole diameter (2.2), corner radius of the glass (4),
battery corner radius (1), connector and button positions along their edges.

**Orientation (inferred, to confirm on the first print).** Firmware uses
`setRotation(0)`, i.e. portrait 240 × 280. The flex cable sits on the drawing's left
edge, which becomes the bottom in portrait. So Type-C and the MX1.25 battery
connector face the **right** wall, and RST/BOOT/PWR face the **left** wall.
A single `board_flip` parameter rotates the board 180° (a mirror is physically wrong) and also needs `lcd.setRotation(2)` in `firmware/src/main.cpp`.

## Design

### Form
Wedge. Screen on the sloped front face at `tilt = 65°` from the desk. Battery lies
flat in the base under the board's lower part and extends rearward under a low back
section. Envelope: about 86 deep × 48 wide × 64 high (the 65 mm battery plus screw bosses sets the depth, and a 5 mm top margin keeps the apex skin thick at tilts of 55°–75°). The heavy cell sits at the bottom, so the stand does not tip.

### Parts
1. **Front shell:** sloped face with screen window and 1 mm bezel lip, side and back
   walls (2.4 mm), 4 base-plate screw bosses, port and button cutouts.
2. **Base plate:** closes the underside with 4 × M2 screws; 4 rubber-foot recesses;
   battery retaining ribs. Removing it exposes the battery and the board's back.

### Board mount
Board is inserted from inside. The glass seats against a 1.0 mm lip. The glass covers the
front of the PCB mounting holes, so screws cannot be used; instead two vertical pusher posts on
the base plate press the PCB's rear edge up and forward (0.2 mm preload). Foam tape on the pusher tips is the fallback if the board rattles. Glass pocket is
glass outline + 0.3 mm per side, with matching corner radii. Window is the active area + 1 mm margin
per side, so the whole touch surface is exposed.

### Battery bay
Pocket 65 × 40 × 12 + 0.4 mm clearance per side (65.8 × 40.8 × 12.8). Finger gaps
between the base-plate ribs allow removal. The interior is one open volume, so the MX1.25 lead
routes freely. The bay position relative to the chip antenna is unverified and must be checked
physically.

### Openings
- Type-C: about 9.5 × 3.5 mm slot + 0.4 clearance in the right wall.
- Buttons: 3 × Ø2 pinholes in the left wall, aligned to RST / BOOT / PWR.

### Parameters
All dimensions are named variables at the top of `stand.scad`: board, glass, battery,
tilt, wall, clearance, screw size, foot size, `board_flip`, `insert_mode`
(self-tapping bosses by default; heat-set inserts optional).

## Deliverables

- `docs/enclosure/stand.scad`
- `docs/enclosure/stand-front.stl`, `docs/enclosure/stand-base.stl`
- `docs/enclosure/fit-check.stl`: a small, quick print (window + pocket + port slot
  only) for validating the unknown dimensions before printing the full stand
- `docs/enclosure/README.md`: print orientation, layer height (0.2 mm), supports,
  screws needed, assembly order, battery polarity warning
- Preview PNGs from OpenSCAD (front, back, and a section through the battery bay)

## Verification

There is no CAD test suite. Verification is:
1. OpenSCAD renders both parts without CGAL errors; STLs are manifold.
2. Preview PNGs and the battery-bay section confirm: battery fits with clearance, board
   rear components do not touch the battery, the window frames the active area, and the
   cutouts line up with the connector and button positions in the drawing.
3. The user prints `fit-check.stl` and reports fit; defaults are corrected before the full print.

## Risks

- **Battery polarity:** MX1.25 pinouts vary by vendor. Check against the board's
  `+`/`−` marks before connecting. This goes in the README.
- **Unknown thickness and hole diameter:** may force a parameter tweak after the fit check.
- **Orientation inference:** if wrong, set `board_flip`.
- **LiPo safety:** the pocket must not compress the cell. Clearance is intentional, and
  the base plate must not press on it.

## Out of scope

Wall mounting, a hinge or adjustable tilt, wireless charging, and changes to the firmware
or bridge.
