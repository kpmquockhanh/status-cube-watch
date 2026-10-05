#pragma once
#include <stdint.h>

#include "melody.h"

// The on-board passive buzzer. buzzer.cpp drives it on the board;
// sim/buzzer_sim.cpp (logs only) and sim/bench.cpp (silent) stand in for it.

// First thing in setup(): drives the pin LOW, because a floating buzzer pin
// keeps drawing current through the 3.3 V LDO. Then sets up LEDC. Returns
// false when LEDC could not be set up; the cube is then silent, with the pin
// still held LOW.
bool buzzerBegin();

// 0 = off, 1..3 = LOW / MED / HIGH (DeviceSettings.sound). Takes effect on the
// next buzzerUpdate(); 0 also stops a tune that is playing.
void buzzerSetLevel(uint8_t level);

// Starts `s`, replacing whatever is playing. Does nothing at level 0.
void buzzerPlay(Sound s);

// Every loop pass.
void buzzerUpdate(uint32_t now);
