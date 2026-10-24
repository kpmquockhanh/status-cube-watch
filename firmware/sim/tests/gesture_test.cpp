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
  Gesture raw(uint32_t now, bool down, int x, int y, bool drag = false) {
    return raw(now, down, x, y, drag ? DragMode::Vertical : DragMode::Off);
  }
  Gesture raw(uint32_t now, bool down, int x, int y, DragMode drag) {
    last = now;
    wasDown = down;
    return t.update(down, (int16_t)x, (int16_t)y, now, drag);
  }
  Gesture at(uint32_t now, bool down, int x, int y, bool drag = false) {
    return at(now, down, x, y, drag ? DragMode::Vertical : DragMode::Off);
  }
  Gesture at(uint32_t now, bool down, int x, int y, DragMode drag) {
    if (down && wasDown) {
      while (now - last > 50u) raw(last + 50u, true, x, y, drag);
    }
    return raw(now, down, x, y, drag);
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

// Taps are never grouped: each one is delivered as it lifts.
void testTapsNeverGroup() {
  Pad p;
  p.at(0, true, 100, 100);
  CHECK(p.at(50, false, 0, 0) == Gesture::Tap);
  p.at(100, true, 100, 100);
  CHECK(p.at(150, false, 0, 0) == Gesture::Tap);
  p.at(200, true, 100, 100);
  CHECK(p.at(250, false, 0, 0) == Gesture::Tap);
}

// A slight drift past TAP_MAX_PX is no tap.
void testDriftIsNotATap() {
  Pad p;
  p.at(0, true, 100, 100);
  p.at(50, true, 130, 100);  // 30 px
  CHECK(p.at(100, false, 0, 0) == Gesture::None);
}

// millis() wrap.
void testClockWrap() {
  const uint32_t base = 0xFFFFFF00u;
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

// Drag mode (the Volume card): a vertical move locks to a drag once it is
// DRAG_LOCK_PX from where it landed, and reports every move until the lift.
void testDragLock() {
  Pad p;
  CHECK(p.raw(0, true, 120, 200, true) == Gesture::None);
  CHECK(p.raw(20, true, 120, 195, true) == Gesture::None);  // 5 px: undecided
  CHECK(p.raw(40, true, 121, 188, true) == Gesture::DragStart);
  CHECK(p.t.lastY() == 188);
  CHECK(p.raw(60, true, 121, 150, true) == Gesture::Drag);
  CHECK(p.t.lastY() == 150);
  CHECK(p.raw(80, true, 121, 150, true) == Gesture::None);  // finger still
  CHECK(p.raw(100, true, 121, 170, true) == Gesture::Drag);  // back down
  CHECK(p.raw(120, false, 0, 0, true) == Gesture::DragEnd);
  CHECK(p.raw(140, false, 0, 0, true) == Gesture::None);
}

// A quick vertical flick in drag mode is a drag, never a swipe (it would open
// a panel on the Volume card).
void testDragFlickIsNotSwipe() {
  Pad p;
  p.raw(0, true, 120, 200, true);
  CHECK(p.raw(30, true, 120, 170, true) == Gesture::DragStart);
  CHECK(p.raw(60, true, 120, 100, true) == Gesture::Drag);
  CHECK(p.raw(90, false, 0, 0, true) == Gesture::DragEnd);
}

// Horizontal still changes card in drag mode.
void testDragModeHorizontalSwipe() {
  Pad p;
  CHECK(p.raw(0, true, 200, 100, true) == Gesture::None);
  CHECK(p.raw(50, true, 150, 102, true) == Gesture::None);  // locked horizontal
  CHECK(p.raw(100, true, 100, 100, true) == Gesture::None);
  CHECK(p.raw(200, false, 0, 0, true) == Gesture::SwipeNext);
}

// A move that locked horizontal and then wandered vertical is nothing: it can
// neither become a drag nor a vertical swipe.
void testDragModeHorizontalThenVertical() {
  Pad p;
  p.raw(0, true, 100, 150, true);
  CHECK(p.raw(30, true, 112, 150, true) == Gesture::None);  // locked horizontal
  CHECK(p.raw(60, true, 112, 100, true) == Gesture::None);  // no DragStart now
  CHECK(p.raw(90, false, 0, 0, true) == Gesture::None);     // and no SwipeUp
}

void testDragModeTap() {
  Pad p;
  p.raw(0, true, 100, 100, true);
  p.raw(40, true, 103, 102, true);
  CHECK(p.raw(80, false, 0, 0, true) == Gesture::Tap);
}

// Review Focus 3: the deck moves off the Volume card mid-drag, so
// main.cpp turns drag mode off. The drag runs to its end; the lift is DragEnd,
// not a swipe on whatever card is now showing.
void testDragEndAfterModeOff() {
  Pad p;
  p.raw(0, true, 120, 200, true);
  CHECK(p.raw(20, true, 120, 185, true) == Gesture::DragStart);
  CHECK(p.raw(40, true, 120, 150, false) == Gesture::Drag);
  CHECK(p.raw(60, true, 120, 110, false) == Gesture::Drag);
  CHECK(p.raw(80, false, 0, 0, false) == Gesture::DragEnd);
}

// Drag mode off: nothing new. A slow vertical move is still nothing.
void testDragModeOffUnchanged() {
  Pad p;
  CHECK(p.raw(0, true, 120, 200) == Gesture::None);
  CHECK(p.raw(400, true, 120, 190) == Gesture::None);
  CHECK(p.raw(800, true, 120, 180) == Gesture::None);
  CHECK(p.raw(801, false, 0, 0) == Gesture::None);
}

}  // namespace

// Free drag (the navigation map): a move either way is a drag, never a swipe;
// every move reports Drag with both coordinates; a still touch is still a tap.
void testFreeDrag() {
  Pad p;
  const DragMode F = DragMode::Free;
  CHECK(p.raw(0, true, 100, 100, F) == Gesture::None);
  CHECK(p.raw(20, true, 105, 100, F) == Gesture::None);       // under the lock distance
  CHECK(p.raw(40, true, 112, 101, F) == Gesture::DragStart);  // horizontal locks a drag too
  CHECK(p.t.lastX() == 112 && p.t.lastY() == 101);
  CHECK(p.raw(60, true, 112, 101, F) == Gesture::None);       // no move, no Drag
  CHECK(p.raw(80, true, 112, 140, F) == Gesture::Drag);       // then any direction
  CHECK(p.raw(100, true, 60, 140, F) == Gesture::Drag);
  CHECK(p.t.lastX() == 60 && p.t.lastY() == 140);
  CHECK(p.raw(120, false, 0, 0, F) == Gesture::DragEnd);      // a fast fling is not a swipe

  CHECK(p.raw(1000, true, 100, 100, F) == Gesture::None);
  CHECK(p.raw(1050, false, 0, 0, F) == Gesture::Tap);
}

int main() {
  testTap();
  testSwipes();
  testNonGestures();
  testTapsNeverGroup();
  testDriftIsNotATap();
  testClockWrap();
  testVerticalSwipes();
  testVerticalClassification();
  testStartPoint();
  testDragLock();
  testDragFlickIsNotSwipe();
  testDragModeHorizontalSwipe();
  testDragModeHorizontalThenVertical();
  testDragModeTap();
  testDragEndAfterModeOff();
  testDragModeOffUnchanged();
  testFreeDrag();
  return checksDone("gesture_test");
}
