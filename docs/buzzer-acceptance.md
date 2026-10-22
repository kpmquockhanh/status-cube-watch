# Buzzer: hardware acceptance checklist

The sequencer (`melody.h`) and the SOUND row are host-tested, and the simulator logs
every play. Nothing here has run on a real cube yet. Work through this after flashing
(`pio run -t upload && pio device monitor`). With the Pomodoro gone, the only sound
today is the beep the display panel plays when SOUND changes (`[buzz] preview`).

## Boot and idle

- [ ] No `[buzz] ledc setup failed` appears in the boot log.
- [ ] No click or tone at power-up or reset.
- [ ] After 10 minutes idle and silent, the area around the 3.3 V regulator is no
      warmer than with the previous firmware, which left the pin floating.
- [ ] Flashed over firmware without a buzzer (nothing stored under `sd`), the display
      panel shows SOUND MED, and stepping it beeps.

## Levels

- [ ] In the display panel (swipe down), SOUND's `-` / `+` step through LOW, MED and HIGH,
      and each step beeps at the new level. LOW is clearly quieter than HIGH, and LOW
      is still audible across a desk. If not, tune `DUTY` in `buzzer.cpp` (and the
      pitches in `melody.h`), reflash, and repeat.
- [ ] A tap on SOUND's label turns it OFF without a beep. Another tap restores the level.
- [ ] A level set from the Mac (**Cube settings…** > Sound) or in the portal shows in
      the display panel, and the next step there beeps from that level.
- [ ] On a Mac helper connected to a cube that has this firmware, the settings window
      shows Sound. On older firmware it does not.

## Backlight

- [ ] While a beep plays, the backlight neither flickers nor changes brightness, at any
      brightness setting. The buzzer's LEDC timer 0 is separate from the backlight's
      timer 3.

## Board revision

- [ ] If nothing sounds even at HIGH while the log shows `[buzz]` lines, check the board
      revision: an older one wires the buzzer to GPIO33. Confirm that on the board's
      schematic before changing `PIN_BUZZER` in `board_pins.h`.
