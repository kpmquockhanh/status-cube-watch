// Desktop stand-in for buzzer.cpp: no speaker, so a play is only logged, with
// the same line the board prints.
#include <Arduino.h>

#include "../src/buzzer.h"
#include "../src/device_settings.h"

namespace {
uint8_t level = 0;
}

bool buzzerBegin() { return true; }
void buzzerSetLevel(uint8_t l) { level = devClampU8(l, 0, DEV_MAX_SOUND); }
void buzzerPlay(Sound s) {
  if (level) Serial.printf("[buzz] %s @%s\n", melodyName(s), devSoundName(level));
}
void buzzerUpdate(uint32_t) {}
