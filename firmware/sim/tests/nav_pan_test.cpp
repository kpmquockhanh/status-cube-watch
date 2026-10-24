#include "check.h"
#include "nav_pan.h"

namespace {

void testDragMovesWithFinger() {
  NavPan p;
  p.grab(100, 100, 0);
  CHECK(p.held());
  CHECK(p.drag(130, 80, 20));
  CHECK(p.x() == 30 && p.y() == -20);
  CHECK(!p.drag(130, 80, 40));  // no move, no change
  p.release(60);
  CHECK(!p.held());
  // A second drag continues from where the map is.
  p.grab(10, 10, 100);
  p.drag(20, 50, 120);
  CHECK(p.x() == 40 && p.y() == 20);
}

void testClamp() {
  NavPan p;
  p.grab(0, 0, 0);
  p.drag(5000, -5000, 10);
  CHECK(p.x() == NAV_PAN_MAX && p.y() == -NAV_PAN_MAX);
}

void testSendRate() {
  NavPan p;
  int16_t x = 0, y = 0;
  CHECK(!p.takeSend(0, x, y));  // centred and the phone knows it
  p.grab(0, 0, 0);
  p.drag(10, 0, 10);
  CHECK(p.takeSend(10, x, y) && x == 10 && y == 0);
  p.drag(20, 0, 50);
  CHECK(!p.takeSend(50, x, y));  // too soon while dragging
  CHECK(p.takeSend(110, x, y) && x == 20);
  p.drag(25, 0, 130);
  p.release(140);
  CHECK(p.takeSend(140, x, y) && x == 25);  // the final value at once
  CHECK(!p.takeSend(150, x, y));
}

void testTapRecentresWithGlide() {
  NavPan p;
  int16_t x = 0, y = 0;
  p.grab(0, 0, 0);
  p.drag(100, -60, 10);
  p.release(20);
  p.takeSend(20, x, y);
  CHECK(p.recenter(1000));
  CHECK(p.gliding());
  CHECK(p.takeSend(1000, x, y) && x == 0 && y == 0);  // the glide's end goes out first
  CHECK(p.tick(1000 + NAV_PAN_GLIDE_MS / 2));
  CHECK(p.x() > 0 && p.x() < 100 && p.y() < 0 && p.y() > -60);
  p.tick(1000 + NAV_PAN_GLIDE_MS);
  CHECK(!p.gliding() && p.x() == 0 && p.y() == 0);
  CHECK(!p.takeSend(2000, x, y));
  CHECK(!p.recenter(3000));  // already centred
}

void testAutoReturn() {
  NavPan p;
  p.grab(0, 0, 0);
  p.drag(40, 0, 10);
  p.release(100);
  CHECK(!p.tick(100 + NAV_PAN_RETURN_MS - 1));
  CHECK(!p.gliding());
  CHECK(p.tick(100 + NAV_PAN_RETURN_MS));
  CHECK(p.gliding());
  // Held for long does not return: only time without a touch counts.
  NavPan q;
  q.grab(0, 0, 0);
  q.drag(40, 0, 10);
  CHECK(!q.tick(10 + NAV_PAN_RETURN_MS * 2));
  CHECK(!q.gliding());
}

void testGrabDuringGlide() {
  NavPan p;
  p.grab(0, 0, 0);
  p.drag(100, 0, 10);
  p.release(20);
  p.recenter(100);
  p.tick(100 + NAV_PAN_GLIDE_MS / 2);
  const int16_t mid = p.x();
  p.grab(50, 50, 100 + NAV_PAN_GLIDE_MS / 2);  // caught where it is
  CHECK(!p.gliding() && p.x() == mid);
  p.drag(60, 50, 200);
  CHECK(p.x() == mid + 10);
}

void testClockWrap() {
  NavPan p;
  const uint32_t t0 = 0xFFFFFF00u;
  p.grab(0, 0, t0);
  p.drag(30, 0, t0 + 10);
  p.release(t0 + 20);
  CHECK(!p.tick(t0 + 20 + NAV_PAN_RETURN_MS - 1));
  CHECK(p.tick(t0 + 20 + NAV_PAN_RETURN_MS));
}

void testReset() {
  NavPan p;
  p.grab(0, 0, 0);
  p.drag(30, 30, 10);
  p.reset();
  int16_t x = 0, y = 0;
  CHECK(p.x() == 0 && p.y() == 0 && !p.held());
  CHECK(!p.takeSend(20, x, y));
}

void testEncode() {
  uint8_t m[BLE_NAV_PAN_LEN];
  navPanEncode(-40, 300, m);
  CHECK(m[0] == 0x08 && m[1] == 0xD8 && m[2] == 0xFF && m[3] == 0x2C && m[4] == 0x01);
}

}  // namespace

int main() {
  testEncode();
  testDragMovesWithFinger();
  testClamp();
  testSendRate();
  testTapRecentresWithGlide();
  testAutoReturn();
  testGrabDuringGlide();
  testClockWrap();
  testReset();
  return checksDone("nav_pan_test");
}
