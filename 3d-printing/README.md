# Claude status cube: desk stand

A two-part printed desk stand for the Waveshare ESP32-S3-Touch-LCD-1.69 and a 12 x 40 x 65 mm LiPo. The board sits in a glass pocket in the sloped front face (65 deg screen angle by default), the cell lies flat in a bay behind and below it, and a base plate with two pusher posts closes the bottom and presses the board into the pocket. Everything is parametric in `stand.scad`; `test.sh` checks the geometry with boolean-intersection tests.

> **Biggest unverified risk: board retention.** The board has only two lower rear contacts (the two pusher posts on the base plate); nothing supports the upper board from behind, and the pusher/retention mechanism is NOT in `fit-check.stl`, so the fit-check cannot validate retention. Touch-time rattle or flex can only be judged on the full print. Fallbacks if it rattles: foam on the pusher tips (then set `preload = -0.8`), or add a rear pad at the roof. Also check on the full print (the fit-check has no posts or rear parts) that rear parts near the lower mounting holes (for example the MX1.25 battery connector) clear the pusher posts.

![front](preview-front.png)
![back](preview-back.png)
![section](preview-section.png)

Outer envelope 47.6 x 85.6 x 61.07 mm (front shell). Every dimension marked `VERIFY` in `stand.scad` is an estimate to confirm on a real print.

## Print list

1. `fit-check.stl` first (1074 triangles, print time not measured): a slice of the shell around the glass pocket with both side walls. It tests glass fit, window, Type-C relief and pinholes only, not retention. The slice has no horizontal face: print it lying on its rear cut face (the large flat face opposite the glass; use the slicer's lay-on-face, which is a rotation of `tilt` = 65 deg from the modelled orientation), so the glass face and pocket point up and no supports should be needed. Printing as modelled needs supports.
2. `stand-front.stl` (2392 triangles) and `stand-base.stl` (2828 triangles).

Settings: 0.2 mm layers, 3 perimeters, 20% infill, PLA or PETG.

- **Front shell** prints as modelled, open bottom on the bed, with supports for the interior roof and the glass-pocket ceiling (both are required). The supports sit inside the open cavity and come out through the bottom.
- **Base plate** (`base_print()`): print with the plate's bottom face (z = -plate_t, the side with the counterbores and foot recesses) on the bed and the pushers pointing up. No supports.

## Hardware

- 4 x M2 x 6 self-tapping screws (an M2 x 8 bottoms out at the pilot) (or heat-set inserts with `insert_mode = true`)
- 4 rubber feet, 8 mm
- optional 1 mm foam tape on the pusher tips (then set `preload = -0.8`)

## Assembly order

1. Connect the battery to the board before closing (see the polarity warning).
2. Set the board into the glass pocket from behind, glass first.
3. Screw on the base plate; the pushers press the board's lower edge into the pocket.
4. Stick on the feet.

## Fit-check procedure

Print `fit-check.stl` and check:

- the glass drops in without force: if tight or loose, adjust `clr` (and `glass_a`, `glass_b`, `glass_r`);
- the window leaves the touch area clear: `act_a0`, `act_b0`, `win_margin`;
- the Type-C plug seats: the slot (`usb_a`, `usb_w`, `usb_h`) is surrounded by a plug relief (`usb_plug_w` x `usb_plug_h` = 13 x 7.5 mm, VERIFY) cut through the wall starting `usb_plug_x0` = 3 mm beyond the PCB edge, so the overmold can reach within a few mm of the board edge (the PCB edge is about 8.9 mm from the outer wall, the wall is 2.4 mm thick). Measure your plug and adjust `usb_plug_w`, `usb_plug_h`, `usb_plug_x0`;
- a paper clip reaches RST / BOOT / PWR through the pinholes: `btn_a`, `btn_y`, `pin_d`;
- the tallest rear part clears: `board_t`.

The opening positions (Type-C slot, buttons) and many board dimensions are `VERIFY` items: measure them on the fit-check print, edit the values, and re-run `docs/enclosure/test.sh` after every edit. Also check the battery position against the board's chip antenna (see Antenna).

## Orientation

The orientation is inferred, not measured: Type-C and the battery connector are on the right wall, RST / BOOT / PWR on the left wall. If the board turns out the other way round, or the screen is upside down, set `board_flip = true` in `stand.scad`. That rotates the board 180 deg, swaps the openings to the opposite walls, and also needs `lcd.setRotation(2)` in `firmware/src/main.cpp` (the `lcd.setRotation(0)` call, currently line 178). With `board_flip = true` the pusher posts sit further back, so the front end zone grows automatically to keep them at least 1 mm (`push_gap`) in front of the battery bay; the stand gets slightly deeper (about 86.56 mm instead of 85.6 at tilt 65); a lower tilt also grows it (about 86.3 mm at tilt 55, no flip). Re-export the STLs after changing it.

## Battery polarity warning

MX1.25 connector pinouts vary by vendor. Compare the battery lead against the `+` / `-` marks on the board before connecting; a swapped pair can destroy the board or the cell. The battery pocket is 65.8 x 40.8 x 12.8 mm and the LiPo must not be compressed: do not force a thicker cell in, and do not let the lid or the base squeeze it.

## Antenna

The battery and its metal pouch must not sit over the board's chip antenna. The model does not include the antenna, so the bay position relative to it is **unverified**: before closing the stand, locate the chip antenna on the board and confirm physically that the battery does not sit over it. The body is plastic only.

## Tests and regenerating

`docs/enclosure/test.sh` (uses Docker via `tools/openscad`; override with `OPENSCAD=...`) must exit 0. Export:

```sh
tools/openscad --backend=manifold -D 'part="front"' -o stand-front.stl stand.scad   # also part="base", part="fit"
python3 tools/stlcheck.py stand-front.stl
```

Previews were rendered headless with `--render` and `xvfb-run` (installed with apt inside the Docker image; not scripted here), using `part="assembly"` / `part="section"` with a section camera rotated 270 deg about z. They are illustrative and not reproducible from this repo alone.
