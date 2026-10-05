// The passive buzzer: a square wave on LEDC channel 0 sets the pitch, and its
// duty sets the loudness. melody.h decides what should sound when; this file
// only turns that into LEDC writes. Hardware-only: see buzzer.h for the
// stand-ins.
#include "buzzer.h"

#include <Arduino.h>

#include "board_pins.h"
#include "device_settings.h"

namespace {

// Channel 0 runs on LEDC timer 0. The backlight is channel 7 on timer 3
// (display.h), so retuning the buzzer never retimes it. Not tone(): that
// claims channel 0 as well, and starts a task of its own.
constexpr uint8_t BUZZ_CH = 0;
constexpr uint8_t BUZZ_BITS = 10;
// Duty per level, out of 1023. A passive transducer is loudest at 50%, and its
// loudness is far from linear in duty, so these are tuned by ear on the board.
constexpr uint32_t DUTY[DEV_MAX_SOUND + 1] = {0, 20, 102, 512};

bool ready = false;
uint8_t level = 0;
bool rewrite = false;  // the level changed: send the duty again even if the pitch did not
uint16_t outHz = 0;    // what the pin plays now; 0 = silent
MelodyPlayer player;

}  // namespace

bool buzzerBegin() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  if (ledcSetup(BUZZ_CH, 2000, BUZZ_BITS) == 0) return false;  // the pin stays LOW
  ledcAttachPin(PIN_BUZZER, BUZZ_CH);
  ledcWrite(BUZZ_CH, 0);
  ready = true;
  return true;
}

void buzzerSetLevel(uint8_t l) {
  l = devClampU8(l, 0, DEV_MAX_SOUND);
  if (l == level) return;
  level = l;
  rewrite = true;
  if (level == 0) player.stop();
}

void buzzerPlay(Sound s) {
  if (!ready || level == 0) return;
  player.play(s);
  Serial.printf("[buzz] %s @%s\n", melodyName(s), devSoundName(level));
}

void buzzerUpdate(uint32_t now) {
  if (!ready) return;
  const uint16_t hz = player.update(now);
  if (hz == outHz && !rewrite) return;  // LEDC is only touched on a change
  if (hz) {
    if (hz != outHz) ledcChangeFrequency(BUZZ_CH, hz, BUZZ_BITS);
    ledcWrite(BUZZ_CH, DUTY[level]);
  } else {
    ledcWrite(BUZZ_CH, 0);  // a rest, the end, or level 0: the output sits LOW
  }
  outHz = hz;
  rewrite = false;
}
