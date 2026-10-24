# Pod: a compact, rain-proof cube for the desk and the bike

Status: draft for approval (2026-10-24).

## Intent

A third enclosure in `3d-printing/`, beside the wedge and the easel. Both of those lay a 12 x 40 x 65 mm cell flat in a base. The new cell is **6.2 x 34 x 41 mm**: nearly the board's own footprint (glass 33.13 x 41.13). That allows a much smaller enclosure, a closed **pod** with the board in front and the cell stacked behind it, shifted along the long axis so it does not cover the board's chip antenna.

The same pod lives in two places:

- **On the desk**, standing up next to the monitor. Mostly read at a glance; touched rarely.
- **On the bike**, carried along when needed (likely the phone-driven navigation screen), **in the rain too**.

The pod never changes between the two. Only the base it locks into does.

## Goals

- One pod: board + 6.2 x 34 x 41 mm cell, closed, as small as the parts allow once the cell clears the antenna. Estimate about 39 x 56–60 x 22 mm before the mount boss (my guess); the real figures come from `board.scad` and the STEP model.
- **Cell clear of the antenna**: the cell is shifted along the board's long axis so that no part of it (pouch, protection board, lead) sits over the chip antenna, at the expense of a longer pod. The probable antenna is a 6 x 2 x 1 mm part on the PCB rear at the short edge on the buttons' side (unflipped board x 7.6..13.6, z 2.0..4.0; see the README's Antenna section), still to be confirmed on a real board.
- **Cell buffer**: the bay is 3–5 mm longer than 41 mm (parametric, default 5) for a protection board folded along one edge, which may not be included in the quoted size.
- **Quarter-turn mount** (Garmin style) on the pod's back: press in, turn 90 deg to lock. Its shape follows the Garmin quarter-turn interface, so a store-bought Garmin mount also takes the pod.
- **Desk foot**: a small printed base with the quarter-turn receiver that holds the pod standing, leaned back for reading next to a monitor. **It lets a USB-C cable be plugged in while it holds the pod** (a clear path to the Type-C opening and somewhere for the cable to leave), so the pod can live on cable power at the desk.
- **Handlebar clamp**: a printed clamp with the same receiver, for a 31.8 mm bar, with snap-in shims for 25.4 and 22.2 mm bars.
- **Rain while riding**: a TPU gasket around the glass, compressed between the shells, and a tethered TPU plug in the Type-C opening. No button holes.
- Charging: by cable, with the plug pulled out, on the desk foot or off it. On the bike: never while riding.
- Opens with **four small screws** (two shells, gasket between), for flashing firmware or swapping the cell.

## Non-goals

- No cable while riding; the bike clamp needs no cable path.
- No button access from outside. The board stays on; RST / BOOT / PWR are reached by opening the pod.
- Not submersible: rain and road spray while riding, not dunking or pressure washing.
- No firmware or bridge change. The IMU's 180 deg auto-rotate already handles either way up.
- The wedge and the easel are untouched.

## Anti-goals (a failure even if it technically works)

- The pod falls off the bike on a bump, or the twist-lock can turn loose by vibration.
- Water reaches the board after a ride in the rain.
- The cell can move, rattle or press on board components on the bike.
- The pod is noticeably bigger than the board + cell stack needs.
- Metal (screws, the cell) over the board's chip antenna (see the README's Antenna section).

## Constraints

- **Manufacture**: sent to a printing service. Rigid parts in **MJF nylon (PA12)**; the gasket and the plug in **TPU**. Thin silicone sheet or a ready-made rubber plug can stand in for them. Clearances must suit MJF accuracy, not FDM.
- **Board facts** come from `3d-printing/board.scad` (shared with the other stands), which was corrected from Waveshare's STEP model. Glass 33.13 x 41.13, glass front to PCB rear 6.3 mm, rear parts up to 10.7 mm, Type-C at `usb_a`.
- **Cell**: 6.2 x 34 x 41 mm quoted, plus 3–5 mm for a possible protection board on one edge. Not confirmed: which edge the lead leaves from, its length, and whether it ends in an MX1.25 plug. Keep these parametric. It is slightly wider than the glass (34 vs 33.13), so it may set the pod's width. Its lead plugs into the board's BAT header (2-pin MX1.25, beside the Type-C).
- **Repo conventions**: a parametric OpenSCAD file `3d-printing/pod.scad` that `include`s `board.scad`. It has its own geometry checks in `test.sh`, including the STEP checks (`tools/stepcheck.py`) against the real board, positive controls, and exported STLs with a README section, like the easel.

## Decided

| What | Decision | By |
|---|---|---|
| Use | desk next to the monitor, mostly reading; bike when needed | user |
| Rain | must survive riding in the rain | user |
| Mount | quarter-turn, Garmin style (picked from three mockups) | user |
| Charging | after the ride; nothing plugged in while riding | user |
| Desk power | the desk foot lets a cable be plugged in while it holds the pod | user |
| Antenna | shift the cell clear of the antenna; a longer pod is accepted | user |
| Cell buffer | 3–5 mm extra length for a protection board | user |
| Buttons | not used day to day (the board stays on), so no button holes | user |
| Making it | printing service | user |
| Material | PA12 MJF for rigid parts, TPU for the seals | Claude's call |
| Closing | two shells, four screws, gasket between | Claude's call |
| Bar sizes | 31.8 mm, shims for 25.4 / 22.2 | Claude's call |
| Garmin compatibility | the boss follows Garmin's shape, so commercial mounts fit | Claude's call |
| Checking the boss | the first order includes a boss-only test print, plus a cheap store-bought Garmin mount if none is at hand, before the full pod | Claude's call |
| Buzzer | no sound hole; the sealed shell muffles the buzzer (only the settings beep uses it today) | Claude's call |
| Power with no button | the board stays on; plugging USB in after a flat cell should start it without PWR (`VERIFY` on hardware: the PWR / SYS_EN power-hold circuit is not documented for this case). Fallback if not: open the pod | Claude's call |

Out of scope, noted for later: rain on the glass can register as touches, and on the navigation screen a drag pans the map. That is firmware, not this enclosure.

## Left to the builder

- Exact wall thicknesses, gasket section and groove, screw size and positions (keep the screws off the antenna).
- How the cell is held: a separator between the rear parts and the cell (it must not rest on components such as the 10.7 mm header), plus padding. Also how the battery lead routes to the BAT header.
- The quarter-turn boss and tab dimensions. Take them from a reliable reference and check them against a real Garmin mount before a full print. Mark them `VERIFY`.
- The desk foot's lean angle and footprint (the stand is rarely touched, so modest stability is enough).
- The plug's tether and seal shape (it must not foul the desk foot or the cable when open), and the screen-edge seal detail.
- Which end the cell's buffer and lead go, within the antenna constraint, as long as both stay parametric.
- Which way the pod sits on the bar (portrait, matching the navigation screen, is the expected default).
- A small fit-test print to order first (as with the easel carrier).
