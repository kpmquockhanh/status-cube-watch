#include <cstdint>

#include "check.h"
#include "gesture.h"

namespace {

// `at` models the real loop, which samples every ~15 ms: a held touch is fed
// as a sample every 50 ms up to `now`. `raw` feeds exactly one sample.
struct Pad {
  GestureTracker t;
  uint32_t last = 0;
  bool wasDown = false;
  Gesture raw(uint32_t now, bool down, int x, int y, bool hold = false) {
    last = now;
    wasDown = down;
    return t.update(down, (int16_t)x, (int16_t)y, now, hold);
  }
  Gesture at(uint32_t now, bool down, int x, int y, bool hold = false) {
    if (down && wasDown) {
      while (now - last > 50u) raw(last + 50u, true, x, y, hold);
    }
    return raw(now, down, x, y, hold);
  }
};

void testTap() {
  Pad p;
  CHECK(p.at(0, true, 100, 100) == Gesture::None);
  CHECK(p.at(50, true, 101, 101) == Gesture::None);
  CHECK(p.at(100, false, 0, 0) == Gesture::Tap);
  CHECK(p.at(150, false, 0, 0) == Gesture::None);  // nothing more once released
}

void testSwipes() {
  Pad p;
  p.at(0, true, 200, 100);
  p.at(100, true, 150, 102);
  p.at(150, true, 100, 100);
  CHECK(p.at(200, false, 0, 0) == Gesture::SwipeNext);  // moved left: advance

  p.at(1000, true, 60, 100);
  p.at(1100, true, 110, 98);
  p.at(1150, true, 160, 100);
  CHECK(p.at(1200, false, 0, 0) == Gesture::SwipePrev);
}

void testNonGestures() {
  Pad p;
  // A slow drag is neither a swipe nor a tap.
  p.at(0, true, 200, 100);
  p.at(400, true, 150, 100);
  p.at(800, true, 100, 100);
  CHECK(p.at(801, false, 0, 0) == Gesture::None);

  // A mostly-vertical drag is not a horizontal swipe (it is a vertical one).
  p.at(2000, true, 100, 200);
  p.at(2100, true, 102, 120);
  CHECK(p.at(2150, false, 0, 0) == Gesture::SwipeUp);

  // A touch held too long to be a tap, with holds disabled: ignored on release.
  p.at(3000, true, 100, 100);
  CHECK(p.at(3600, true, 100, 100) == Gesture::None);
  CHECK(p.at(3700, false, 0, 0) == Gesture::None);
}

void testLongPressAndReset() {
  Pad p;
  CHECK(p.at(0, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(300, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(599, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(600, true, 100, 100, true) == Gesture::LongPress);
  CHECK(p.at(700, true, 100, 100, true) == Gesture::None);   // once
  CHECK(p.at(1900, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(2000, true, 100, 100, true) == Gesture::ResetPress);
  CHECK(p.at(2100, true, 100, 100, true) == Gesture::None);  // once
  CHECK(p.at(2200, false, 0, 0) == Gesture::None);           // release acts as nothing
}

// Review Focus 1: the finger lifting after a hold must not be read as a tap.
void testNoTrailingTap() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(600, true, 100, 100, true) == Gesture::LongPress);
  CHECK(p.at(650, false, 0, 0) == Gesture::None);  // not Tap
  // ...and the tracker is clean for the next touch.
  p.at(1000, true, 100, 100, true);
  CHECK(p.at(1050, false, 0, 0) == Gesture::Tap);
}

void testReleaseBetweenLongAndReset() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(600, true, 100, 100, true) == Gesture::LongPress);
  CHECK(p.at(1500, false, 0, 0) == Gesture::None);
  // No ResetPress was produced, and a fresh touch starts from zero.
  p.at(2000, true, 100, 100, true);
  CHECK(p.at(2400, true, 100, 100, true) == Gesture::None);
}

// Review Focus 1: holds on a card that does not use them stay ignored.
void testHoldDisabled() {
  Pad p;
  p.at(0, true, 100, 100, false);
  CHECK(p.at(600, true, 100, 100, false) == Gesture::None);
  CHECK(p.at(2000, true, 100, 100, false) == Gesture::None);
  CHECK(p.at(2100, false, 0, 0) == Gesture::None);
}

void testMovingCancelsHold() {
  Pad p;
  p.at(0, true, 100, 100, true);
  p.at(100, true, 130, 100, true);  // 30 px: no longer a stationary hold
  CHECK(p.at(700, true, 130, 100, true) == Gesture::None);
  CHECK(p.at(2100, true, 130, 100, true) == Gesture::None);
  CHECK(p.at(2200, false, 0, 0) == Gesture::None);  // and 30 px is not a swipe either
}

void testQuickTapOnHoldCard() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(100, false, 0, 0) == Gesture::Tap);
}

// Review Focus 3: millis() wrap.
void testClockWrap() {
  const uint32_t base = 0xFFFFFF00u;
  Pad p;
  p.at(base, true, 100, 100, true);
  CHECK(p.at(base + 599u, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(base + 600u, true, 100, 100, true) == Gesture::LongPress);
  CHECK(p.at(base + 650u, false, 0, 0) == Gesture::None);

  Pad q;  // a tap across the wrap
  q.at(base + 200u, true, 100, 100);
  CHECK(q.at(base + 300u, false, 0, 0) == Gesture::Tap);
}

// Final review I1: a touch that began where holds meant nothing must not turn
// into a LongPress the moment the deck lands on the Pomodoro card.
void testHoldEnabledMidTouch() {
  Pad p;
  p.at(0, true, 100, 100, false);
  CHECK(p.at(1000, true, 100, 100, false) == Gesture::None);
  CHECK(p.at(1010, true, 100, 100, true) == Gesture::None);  // card changed under the finger
  CHECK(p.at(1500, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(1600, false, 0, 0) == Gesture::None);
  // A fresh touch on the card is a normal hold again.
  p.at(2000, true, 100, 100, true);
  CHECK(p.at(2600, true, 100, 100, true) == Gesture::LongPress);
}

// Final review I3: a stalled loop (a blocking fetch) must not turn one hold
// into LongPress and ResetPress back to back.
void testSampleGapBlocksHold() {
  Pad p;
  p.raw(0, true, 100, 100, true);
  p.raw(15, true, 100, 100, true);
  CHECK(p.raw(2500, true, 100, 100, true) == Gesture::None);  // 2.5 s with no samples
  CHECK(p.raw(2515, true, 100, 100, true) == Gesture::None);
  CHECK(p.raw(2600, false, 0, 0) == Gesture::None);
}

// ResetPress is a deliberate second hold: it is measured from the LongPress,
// not just from touch-down, so one slow sample cannot fire both together.
void testResetFollowsLongByItsOwnInterval() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(600, true, 100, 100, true) == Gesture::LongPress);
  CHECK(p.at(650, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(1999, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(2000, true, 100, 100, true) == Gesture::ResetPress);
}

void testVerticalSwipes() {
  Pad p;
  p.at(0, true, 100, 220);
  p.at(100, true, 101, 170);
  p.at(150, true, 100, 120);
  CHECK(p.at(200, false, 0, 0) == Gesture::SwipeUp);  // finger moved up the screen

  p.at(1000, true, 100, 60);
  p.at(1100, true, 99, 110);
  p.at(1150, true, 100, 160);
  CHECK(p.at(1200, false, 0, 0) == Gesture::SwipeDown);
}

// The dominant axis wins on a diagonal; a short or slow vertical drag is nothing.
void testVerticalClassification() {
  Pad p;
  p.at(0, true, 100, 200);
  p.at(100, true, 130, 130);  // dx 30, dy -70
  CHECK(p.at(150, false, 0, 0) == Gesture::SwipeUp);

  p.at(1000, true, 100, 100);
  p.at(1100, true, 190, 130);  // dx 90, dy 30
  CHECK(p.at(1150, false, 0, 0) == Gesture::SwipePrev);

  p.at(2000, true, 100, 200);
  p.at(2100, true, 100, 170);  // 30 px: more than a tap, less than a swipe
  CHECK(p.at(2150, false, 0, 0) == Gesture::None);

  p.at(3000, true, 100, 220);
  p.at(3400, true, 100, 170);
  p.at(3800, true, 100, 120);  // 100 px but 800 ms: too slow
  CHECK(p.at(3801, false, 0, 0) == Gesture::None);
}

// A vertical drag on the hold-enabled Pomodoro card must not become a hold.
void testVerticalSwipeOnHoldCard() {
  Pad p;
  p.at(0, true, 100, 220, true);
  p.at(100, true, 100, 170, true);
  p.at(200, true, 100, 120, true);
  CHECK(p.at(250, false, 0, 0) == Gesture::SwipeUp);
}

// Review Focus 5: a tap is positioned where the finger went down.
void testStartPoint() {
  Pad p;
  p.at(0, true, 120, 150);
  p.at(40, true, 125, 154);  // a little drift, still a tap
  CHECK(p.at(80, false, 0, 0) == Gesture::Tap);
  CHECK(p.t.startX() == 120);
  CHECK(p.t.startY() == 150);
  p.at(500, true, 30, 40);
  p.at(540, false, 0, 0);
  CHECK(p.t.startX() == 30);
  CHECK(p.t.startY() == 40);
}

}  // namespace

int main() {
  testHoldEnabledMidTouch();
  testSampleGapBlocksHold();
  testResetFollowsLongByItsOwnInterval();
  testTap();
  testSwipes();
  testNonGestures();
  testLongPressAndReset();
  testNoTrailingTap();
  testReleaseBetweenLongAndReset();
  testHoldDisabled();
  testMovingCancelsHold();
  testQuickTapOnHoldCard();
  testClockWrap();
  testVerticalSwipes();
  testVerticalClassification();
  testVerticalSwipeOnHoldCard();
  testStartPoint();
  return checksDone("gesture_test");
}
