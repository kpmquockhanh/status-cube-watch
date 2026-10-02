# IMU auto-rotate: hardware acceptance checklist

The classifier and the touch remap are host-tested, and the flow is exercised in
the simulator. Nothing here has run on a real cube yet. Work through this after
flashing (`pio run -t upload && pio device monitor`).

## Boot log

- [ ] `[imu] QMI8658 who-am-i 0x05` appears right after `[touch] CST816 chip id ...`.
      If it says "not responding", check `QMI8658_ADDR` in `board_pins.h` (`0x6A`
      is the other possible address) and the I2C pins. Touch must still work.

## Direction

- [ ] Stand the cube on its edge so the picture is the right way up. It must stay
      as is. (If it flips to upside down after about a second and back, `IMU_UP_SIGN`
      in `board_pins.h` is wrong: change `1` to `-1` and reflash.)
- [ ] Turn it over 180° on its edge. After about 1 s the picture rotates and
      `[imu] rotation -> 2` is logged. Turn it back: `rotation -> 0`.
- [ ] Lay it flat face up, then face down, and tilt it about 30° either way: the
      picture never flips.

## Display

- [ ] After the flip there is no band or shift at either screen edge. The 20 px
      `LCD_OFFSET_Y` is symmetric in the 40 spare rows of the 320-row frame, so it
      should be unchanged; if a band appears, adjust the offset in `display.h`.
- [ ] Colours are unchanged (no inversion after `setRotation`).

## Touch

- [ ] Upside down: a tap on a visible control (e.g. the DONE bar of the display
      panel, swipe down to open it) lands where it is drawn.
- [ ] Upside down: swipe left advances the card, swipe right goes back, swipe down
      opens the display panel from the top **of the picture**, swipe up closes it.
- [ ] Hold a finger on the screen and turn the cube: the picture does not flip until
      the finger lifts, and no swipe fires.
- [ ] No touch drops or I2C errors in the log while the IMU is polled (watch for a
      minute).

## Sleep and boot

- [ ] Let the screen sleep (`SCREEN_SLEEP_MS`), flip the cube, then touch: it wakes
      already the right way up, and that wake touch does not change the card.
- [ ] Moving the cube alone never wakes a sleeping screen.
- [ ] Power the cube up upside down: after the boot screens the picture rotates
      within about a second.

## Simulator (a human at the window)

- [ ] `cd firmware/sim && CUBE_ORIENT=2 make run`: the window flips about a second
      after start; dragging the mouse right-to-left still advances the card. If it
      goes back a card instead, delete the `touchToScreen(...)` call in
      `firmware/sim/touch_sim.cpp`.
