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
  Gesture raw(uint32_t now, bool down, int x, int y, bool multi = false) {
    last = now;
    wasDown = down;
    return t.update(down, (int16_t)x, (int16_t)y, now, multi);
  }
  Gesture at(uint32_t now, bool down, int x, int y, bool multi = false) {
    if (down && wasDown) {
      while (now - last > 50u) raw(last + 50u, true, x, y, multi);
    }
    return raw(now, down, x, y, multi);
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

  // A touch held too long to be a tap: ignored on release.
  p.at(3000, true, 100, 100);
  CHECK(p.at(3600, true, 100, 100) == Gesture::None);
  CHECK(p.at(3700, false, 0, 0) == Gesture::None);
}

// Two quick taps are one DoubleTap, known once the gap passes with no third.
void testDoubleTap() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(50, false, 0, 0, true) == Gesture::None);   // first tap: wait
  p.at(150, true, 102, 100, true);
  CHECK(p.at(200, false, 0, 0, true) == Gesture::None);  // second tap: still waiting on a third
  CHECK(p.at(200 + MULTI_TAP_GAP_MS, false, 0, 0, true) == Gesture::None);  // gap not yet exceeded
  CHECK(p.at(200 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::DoubleTap);
  CHECK(p.at(1000, false, 0, 0, true) == Gesture::None);  // once
}

// The gap runs from one lift to the next touch, not to the next lift: a second
// press still down when the gap would have ended is part of the double tap.
void testSlowSecondPressStillDoubles() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(50, false, 0, 0, true) == Gesture::None);
  CHECK(p.at(250, true, 100, 100, true) == Gesture::None);   // touches 200 ms after the lift
  CHECK(p.raw(410, true, 100, 100, true) == Gesture::None);  // still down, 360 ms after the lift
  CHECK(p.at(420, false, 0, 0, true) == Gesture::None);      // a 170 ms press: still a tap
  CHECK(p.at(420 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::DoubleTap);
}

// A third tap inside the gap fires TripleTap at once and not a DoubleTap too.
void testTripleTap() {
  Pad p;
  p.at(0, true, 100, 100, true);
  p.at(40, false, 0, 0, true);
  p.at(120, true, 100, 100, true);
  p.at(160, false, 0, 0, true);
  p.at(240, true, 100, 100, true);
  CHECK(p.at(280, false, 0, 0, true) == Gesture::TripleTap);
  CHECK(p.at(1000, false, 0, 0, true) == Gesture::None);
  // A fourth tap starts a fresh sequence.
  p.at(2000, true, 100, 100, true);
  CHECK(p.at(2040, false, 0, 0, true) == Gesture::None);
  CHECK(p.at(2040 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::Tap);
}

// One tap alone settles as a plain Tap, after the gap.
void testSingleTapOnMultiCard() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(50, false, 0, 0, true) == Gesture::None);
  CHECK(p.at(50 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::Tap);
}

// Taps too far apart are two separate taps, not a DoubleTap.
void testSlowTapsDoNotGroup() {
  Pad p;
  p.at(0, true, 100, 100, true);
  p.at(50, false, 0, 0, true);
  // The loop polls while no finger is down, so the first tap settles on its own.
  CHECK(p.at(50 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::Tap);
  p.at(600, true, 100, 100, true);
  CHECK(p.at(650, false, 0, 0, true) == Gesture::None);
  CHECK(p.at(650 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::Tap);
}

// If the next touch lands after a stalled loop, the waiting taps still settle.
void testSettleOnNextTouchDown() {
  Pad p;
  p.at(0, true, 100, 100, true);
  p.at(40, false, 0, 0, true);
  p.at(120, true, 100, 100, true);
  p.at(160, false, 0, 0, true);
  CHECK(p.raw(2000, true, 100, 100, true) == Gesture::DoubleTap);
}

// With multi-tap off (every other card, the editors) a tap is immediate.
void testTapImmediateWhenMultiOff() {
  Pad p;
  p.at(0, true, 100, 100, false);
  CHECK(p.at(50, false, 0, 0, false) == Gesture::Tap);
  p.at(100, true, 100, 100, false);
  CHECK(p.at(150, false, 0, 0, false) == Gesture::Tap);  // never grouped
}

// A swipe or a long press between taps breaks the sequence.
void testBreaksTheSequence() {
  Pad p;
  p.at(0, true, 100, 100, true);
  p.at(40, false, 0, 0, true);  // one tap waiting
  p.at(100, true, 200, 100, true);
  p.at(150, true, 150, 100, true);
  p.at(200, true, 100, 100, true);
  CHECK(p.at(250, false, 0, 0, true) == Gesture::SwipeNext);
  CHECK(p.at(250 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::None);  // the first tap is gone

  p.at(1000, true, 100, 100, true);
  p.at(1040, false, 0, 0, true);
  p.at(1100, true, 100, 100, true);
  p.at(1900, true, 100, 100, true);  // 800 ms press: not a tap
  CHECK(p.at(1950, false, 0, 0, true) == Gesture::None);
  CHECK(p.at(1950 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::None);
}

// Leaving the Pomodoro card with taps pending drops them: they must not
// fire later on some other card.
void testCardChangeDropsPending() {
  Pad p;
  p.at(0, true, 100, 100, true);
  p.at(40, false, 0, 0, true);
  p.at(120, true, 100, 100, true);
  p.at(160, false, 0, 0, true);
  CHECK(p.at(300, false, 0, 0, false) == Gesture::None);  // multi-tap went off
  CHECK(p.at(300 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::None);
}

// A slow press or slight drift is no tap, so it never counts toward a multi-tap.
void testDriftIsNotATap() {
  Pad p;
  p.at(0, true, 100, 100, true);
  p.at(50, true, 130, 100, true);  // 30 px
  CHECK(p.at(100, false, 0, 0, true) == Gesture::None);
  CHECK(p.at(100 + MULTI_TAP_GAP_MS + 1, false, 0, 0, true) == Gesture::None);
}

// millis() wrap.
void testClockWrap() {
  const uint32_t base = 0xFFFFFF00u;
  Pad p;
  p.at(base, true, 100, 100, true);
  p.at(base + 40u, false, 0, 0, true);
  p.at(base + 120u, true, 100, 100, true);
  p.at(base + 160u, false, 0, 0, true);
  CHECK(p.at(base + 160u + MULTI_TAP_GAP_MS + 1u, false, 0, 0, true) == Gesture::DoubleTap);

  Pad q;  // a tap across the wrap
  q.at(base + 200u, true, 100, 100);
  CHECK(q.at(base + 300u, false, 0, 0) == Gesture::Tap);
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

// A vertical drag on the multi-tap Pomodoro card is still a swipe.
void testVerticalSwipeOnMultiCard() {
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
  testTap();
  testSwipes();
  testNonGestures();
  testDoubleTap();
  testSlowSecondPressStillDoubles();
  testTripleTap();
  testSingleTapOnMultiCard();
  testSlowTapsDoNotGroup();
  testSettleOnNextTouchDown();
  testTapImmediateWhenMultiOff();
  testBreaksTheSequence();
  testCardChangeDropsPending();
  testDriftIsNotATap();
  testClockWrap();
  testVerticalSwipes();
  testVerticalClassification();
  testVerticalSwipeOnMultiCard();
  testStartPoint();
  return checksDone("gesture_test");
}
