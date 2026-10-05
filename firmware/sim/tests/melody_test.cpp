#include <cstring>

#include "check.h"
#include "melody.h"

namespace {

const Sound ALL[] = {Sound::FocusDone, Sound::BreakDone, Sound::Attention, Sound::Preview};

// Plays `s` from t0, checking the first and the last millisecond of every note,
// then that it ends exactly when its notes add up to.
void checkWholeTune(Sound s, uint32_t t0) {
  uint8_t n = 0;
  const Note *tune = melodyFor(s, n);
  MelodyPlayer p;
  p.play(s);
  uint32_t t = t0;
  for (uint8_t i = 0; i < n; i++) {
    CHECK(p.update(t) == tune[i].hz);
    CHECK(p.update(t + tune[i].ms - 1) == tune[i].hz);
    CHECK(p.playing());
    t += tune[i].ms;
  }
  CHECK(p.update(t) == 0);
  CHECK(!p.playing());
  CHECK(p.update(t + 1000) == 0);
}

void testWholeTunes() {
  for (Sound s : ALL) {
    checkWholeTune(s, 1000);
    checkWholeTune(s, 0xFFFFFFFFu - 50);  // millis() wraps mid-tune
  }
}

void testIdle() {
  MelodyPlayer p;
  CHECK(!p.playing());
  CHECK(p.update(0) == 0);
  CHECK(p.update(123456) == 0);
}

// A rest is silence, but the tune is still playing.
void testRest() {
  CHECK(MELODY_ATTENTION[1].hz == 0);
  MelodyPlayer p;
  p.play(Sound::Attention);
  CHECK(p.update(0) == MELODY_ATTENTION[0].hz);
  const uint32_t rest = MELODY_ATTENTION[0].ms;
  CHECK(p.update(rest) == 0 && p.playing());
  CHECK(p.update(rest + MELODY_ATTENTION[1].ms) == MELODY_ATTENTION[2].hz);
}

// The editor's preview is requested from pollTouch(), after this pass read
// `now`. The tune must start at the next update, whatever its `now`, and play
// whole.
void testTimingStartsAtFirstUpdate() {
  MelodyPlayer p;
  p.play(Sound::Preview);
  CHECK(p.playing());
  CHECK(p.update(50000) == MELODY_PREVIEW[0].hz);
  CHECK(p.update(50000 + MELODY_PREVIEW[0].ms - 1) == MELODY_PREVIEW[0].hz);
  CHECK(p.update(50000 + MELODY_PREVIEW[0].ms) == 0);
}

// One long pass misses a whole note: the player skips it, and the tune still
// ends on time instead of running late.
void testLatePassSkipsAhead() {
  MelodyPlayer p;
  p.play(Sound::FocusDone);
  CHECK(p.update(0) == MELODY_FOCUS_DONE[0].hz);
  const uint32_t third = MELODY_FOCUS_DONE[0].ms + MELODY_FOCUS_DONE[1].ms;
  CHECK(p.update(third + 5) == MELODY_FOCUS_DONE[2].hz);
  CHECK(p.update(third + MELODY_FOCUS_DONE[2].ms) == 0 && !p.playing());
}

void testPlayReplaces() {
  MelodyPlayer p;
  p.play(Sound::FocusDone);
  p.update(0);
  CHECK(p.update(MELODY_FOCUS_DONE[0].ms) == MELODY_FOCUS_DONE[1].hz);  // mid-tune
  const uint32_t t = MELODY_FOCUS_DONE[0].ms + 10;
  p.play(Sound::Preview);
  CHECK(p.update(t) == MELODY_PREVIEW[0].hz);
  CHECK(p.update(t + MELODY_PREVIEW[0].ms) == 0 && !p.playing());
}

void testStop() {
  MelodyPlayer p;
  p.play(Sound::BreakDone);
  CHECK(p.update(0) != 0);
  p.stop();
  CHECK(!p.playing() && p.update(1) == 0);
  p.play(Sound::BreakDone);  // stopped before it ever sounded
  p.stop();
  CHECK(!p.playing() && p.update(2) == 0);
  p.stop();  // stopping nothing is fine
  CHECK(!p.playing());
}

bool sameTune(Sound a, Sound b) {
  uint8_t na = 0, nb = 0;
  const Note *ta = melodyFor(a, na);
  const Note *tb = melodyFor(b, nb);
  if (na != nb) return false;
  for (uint8_t i = 0; i < na; i++)
    if (ta[i].hz != tb[i].hz || ta[i].ms != tb[i].ms) return false;
  return true;
}

void testTables() {
  for (Sound s : ALL) {
    uint8_t n = 0;
    const Note *tune = melodyFor(s, n);
    CHECK(tune != nullptr && n > 0);
    if (!tune || !n) continue;
    uint32_t total = 0;
    for (uint8_t i = 0; i < n; i++) {
      CHECK(tune[i].ms >= 60);  // the loop times notes to about one 16 ms pass
      total += tune[i].ms;
    }
    CHECK(total < 500);
    CHECK(tune[0].hz != 0 && tune[n - 1].hz != 0);  // no dead air at either end
  }
  CHECK(!sameTune(Sound::FocusDone, Sound::BreakDone));  // you can tell break from work by ear
  CHECK(!strcmp(melodyName(Sound::FocusDone), "focus-done"));
  CHECK(!strcmp(melodyName(Sound::BreakDone), "break-done"));
  CHECK(!strcmp(melodyName(Sound::Attention), "attention"));
  CHECK(!strcmp(melodyName(Sound::Preview), "preview"));
}

}  // namespace

int main() {
  testWholeTunes();
  testIdle();
  testRest();
  testTimingStartsAtFirstUpdate();
  testLatePassSkipsAhead();
  testPlayReplaces();
  testStop();
  testTables();
  return checksDone("melody_test");
}
