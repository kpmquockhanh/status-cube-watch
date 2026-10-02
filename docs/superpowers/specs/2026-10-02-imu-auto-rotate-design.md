# IMU auto-rotate (180°) — design

## Goal

When the cube is turned upside down, the display redraws rotated 180° so it stays
readable, and touch/swipes still feel correct. Portrait and upside-down only.

Out of scope: landscape, flip/shake as an input gesture, any on/off setting
(always on; decided with the user), gyro use, the IMU interrupt pin.

Assumption: the cube is usually stood upright or hung, and often rests flat, face up.
USB-C sits on a side edge, so its position is irrelevant to the logic; only the
sign of the accelerometer axis that runs along the screen's vertical axis matters.

## Approach

Rotate the panel in hardware (`lcd.setRotation(0|2)`) and leave the renderer alone.
`ui.cpp` composes one 240x280 sprite and pushes it, so card/ring/editor layouts
and `preview.html` are untouched. Touch is remapped before it reaches
`GestureTracker`, so swipes and both editors work unchanged.

## Components

1. **`imu.h/.cpp`** (hardware-only) — minimal QMI8658C driver on the shared `Wire`
   bus, address `0x6B` (`QMI8658_ADDR` in `board_pins.h`). Accelerometer only, low
   ODR, one call `bool imuReadAccel(float &ax, float &ay, float &az)` in g.
   Absent/not answering → returns false and orientation stays at 0; never blocks boot.
2. **`orientation.h`** (pure, host-tested, no Arduino) — `Orientation` class:
   `update(now, up, az, busy)` → true when the rotation (0 or 2) changed. `up` is the
   accelerometer axis along the screen's vertical, signed by `IMU_UP_SIGN` so that
   `+1 g` means "screen top is up".
   - A flip candidate exists only when `|up| > 0.6 g` **and** `|up| > |az|` (so a cube
     lying flat or tilted past 45° towards face-up never flips) and it points away
     from the current orientation. It must hold for 1000 ms.
   - Anything else (flat, in the dead zone, pointing at the current orientation)
     clears the candidate and keeps the orientation. Needing `up < -0.6` to go to 2
     and `up > +0.6` to come back is the hysteresis.
   - `busy` (finger down) defers a pending flip; it lands right after the finger
     lifts if still wanted.
3. **`board_pins.h`** — `IMU_UP_SIGN` (+1/-1): which sign of the IMU axis means
   "screen top is up". Needs confirming on the first hardware run.
4. **`main.cpp`** — poll the IMU ~5 Hz from the main loop; when `Orientation`
   changes, call `lcd.setRotation(r)`, set the touch-remap flag, and force a full
   redraw. Touch remap: for rotation 2, `x → LCD_WIDTH-1-x`, `y → LCD_HEIGHT-1-y`,
   applied where `touch.read()` results are consumed. Remap lives in a small pure
   helper so it is host-tested.
5. **Simulator** — `imu_sim.cpp` returns a gravity vector from `CUBE_ORIENT=0|2`
   (default 0), so `make run` flips after ~1 s; `touch_sim.cpp` undoes the remap so
   `main.cpp` sees raw panel coordinates as on hardware. `make shot` is **not**
   affected: it reads pixels back with `readRect` in LovyanGFX's already-rotated
   logical frame, so a flipped frame looks identical there. The flip is only visible
   in the `make run` window.

## Interactions to preserve

- Screen sleep: IMU motion is **not** activity; it neither wakes the screen nor
  resets the idle timer. Orientation is still tracked while asleep (the CPU runs at
  80 MHz then; 5 Hz reads are negligible) so the first frame on wake is correct.
- Editors and Pomodoro: rotation may change while an editor or timer is open;
  state is untouched, only the output and touch mapping change.
- Boot: orientation starts at 0 and is corrected within ~1 s if the cube boots upside down.

## Error handling

IMU missing or I2C errors → treat as "no reading", keep current orientation, log once
to Serial (`[imu] not responding`), same style as `touch.cpp`. A bad IMU never
affects touch or display.

## Testing

- Host (`make test` in `firmware/sim/`): `Orientation` — flips after hold, not before;
  flat readings never flip; hysteresis band; `busy` defers; flips back; touch remap
  helper for both rotations incl. corners.
- Simulator: `CUBE_ORIENT=2 make run` shows the rotated frame and a left swipe with
  the mouse still advances the card.
- Hardware only (open checklist, like `docs/ble-acceptance.md`): `IMU_UP_SIGN` is
  right; no band at an edge after rotation (the 20 px `LCD_OFFSET_Y` is symmetric
  in the 40 spare rows of the 320-row frame, so it should be unchanged under
  rotation 2; if a band appears, adjust the offset in `display.h`); touch lands
  where drawn when flipped; no I2C errors from touch while polling; QMI8658
  `WHO_AM_I` (reg `0x00`) reads `0x05` in the boot log.

## Docs

Update CLAUDE.md (new "Auto-rotate" paragraph, local-only: not in payload or
`preview.html`, hardware layer `imu.cpp` with sim stand-in) and the Commands list
(`CUBE_ORIENT`).
