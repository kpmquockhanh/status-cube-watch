#include "anim_pacer.h"
#include "check.h"

namespace {

// Counts the frames due over [from, to) with a pass every millisecond.
int framesIn(AnimPacer &p, uint32_t from, uint32_t to, bool animating, bool external) {
  int n = 0;
  for (uint32_t t = from; t != to; t++) n += p.due(t, animating, external) ? 1 : 0;
  return n;
}

void pacesExternal() {
  AnimPacer p;
  CHECK(ANIM_EXTERNAL_MS == 33);
  CHECK(!p.due(1000, true, true));  // the screen that starts it was just drawn
  CHECK(!p.due(1000 + ANIM_EXTERNAL_MS - 1, true, true));
  CHECK(p.due(1000 + ANIM_EXTERNAL_MS, true, true));
  CHECK(!p.due(1000 + ANIM_EXTERNAL_MS, true, true));  // at most once per period
  CHECK(framesIn(p, 1000 + ANIM_EXTERNAL_MS + 1, 1000 + ANIM_EXTERNAL_MS + 1 + 10000, true, true) ==
        (int)(10000 / ANIM_EXTERNAL_MS));
}

void pacesBattery() {
  AnimPacer p;
  CHECK(ANIM_BATTERY_MS == 42);
  CHECK(!p.due(0, true, false));
  CHECK(!p.due(ANIM_BATTERY_MS - 1, true, false));
  CHECK(p.due(ANIM_BATTERY_MS, true, false));
  CHECK(framesIn(p, ANIM_BATTERY_MS + 1, ANIM_BATTERY_MS + 1 + 10000, true, false) == (int)(10000 / ANIM_BATTERY_MS));
  // A late pass does not owe the frames it missed.
  CHECK(p.due(60000, true, false));
  CHECK(!p.due(60001, true, false));
  // Plugged in mid-run: the faster period applies from the last frame.
  CHECK(p.due(60000 + ANIM_EXTERNAL_MS, true, true));
}

void keepsScheduleOnLatePasses() {
  // A pass every 20 ms: a frame lands on the first pass past each due time, but
  // the rate is still one per period, not one per two passes.
  AnimPacer p;
  CHECK(!p.due(0, true, true));
  int n = 0;
  for (uint32_t t = 20; t <= 10000; t += 20) n += p.due(t, true, true) ? 1 : 0;
  CHECK(n >= (int)(10000 / ANIM_EXTERNAL_MS) - 1 && n <= (int)(10000 / ANIM_EXTERNAL_MS));
}

void stopsWhenNotAnimating() {
  AnimPacer p;
  CHECK(framesIn(p, 0, 5000, false, true) == 0);
  CHECK(framesIn(p, 0, 5000, false, false) == 0);
  CHECK(!p.due(5000, true, true));  // starts over: the first frame is a period away
  CHECK(p.due(5000 + ANIM_EXTERNAL_MS, true, true));
  CHECK(!p.due(5000 + 2 * ANIM_EXTERNAL_MS, false, true));     // stopped
  CHECK(!p.due(5000 + 2 * ANIM_EXTERNAL_MS + 1, true, true));  // restarted: a period again
  CHECK(p.due(5000 + 3 * ANIM_EXTERNAL_MS + 1, true, true));
}

void wrapSafe() {
  AnimPacer p;
  const uint32_t t0 = 0xFFFFFFFFu - 10;
  CHECK(!p.due(t0, true, true));
  CHECK(!p.due(t0 + 20, true, true));  // wrapped past 0, 20 ms in
  CHECK(p.due(t0 + ANIM_EXTERNAL_MS, true, true));
  CHECK(!p.due(t0 + ANIM_EXTERNAL_MS + 1, true, true));
  // A stamp newer than `now` (a clock read before it) is not ~49 days old.
  AnimPacer q;
  CHECK(!q.due(1000, true, true));
  CHECK(!q.due(990, true, true));
}

}  // namespace

int main() {
  pacesExternal();
  pacesBattery();
  keepsScheduleOnLatePasses();
  stopsWhenNotAnimating();
  wrapSafe();
  return checksDone("anim_pacer");
}
