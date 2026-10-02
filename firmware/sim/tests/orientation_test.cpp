#include <cmath>

#include "check.h"
#include "orientation.h"

namespace {

// up = -1: upside down. up = +1: upright. az = 0: standing on an edge.
bool feed(Orientation &o, uint32_t t, float up, float az = 0.0f, bool busy = false) {
  return o.update(t, up, az, busy);
}

void testStartsUpright() {
  Orientation o;
  CHECK(o.rotation() == 0);
}

void testFlipsAfterHold() {  // also the cold start: boots upside down, corrected after 1 s
  Orientation o;
  CHECK(!feed(o, 0, -1.0f));
  CHECK(!feed(o, 999, -1.0f));
  CHECK(feed(o, 1000, -1.0f));
  CHECK(o.rotation() == 2);
  CHECK(!feed(o, 1200, -1.0f));  // already flipped: no repeat
}

void testInterruptedHoldRestarts() {
  Orientation o;
  feed(o, 0, -1.0f);
  feed(o, 500, 0.0f);            // back to the dead zone: candidate cleared
  feed(o, 600, -1.0f);           // new candidate starts at 600
  CHECK(!feed(o, 1500, -1.0f));  // only 900 ms held
  CHECK(feed(o, 1600, -1.0f));
}

void testFlatNeverFlips() {
  Orientation o;
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o, t, -0.3f, 1.0f));
  CHECK(o.rotation() == 0);
  Orientation o2;  // face down is flat too
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o2, t, -0.3f, -1.0f));
  CHECK(o2.rotation() == 0);
}

void testTiltedFaceUpNeverFlips() {  // |up| is over 0.6 but the face still points more at the ceiling
  Orientation o;
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o, t, -0.7f, 0.9f));
  CHECK(o.rotation() == 0);
}

void testDeadZoneNeverFlips() {
  Orientation o;
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o, t, -0.5f, 0.1f));
  CHECK(o.rotation() == 0);
}

void testExactThresholdDoesNotFlip() {
  Orientation o;
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o, t, -0.6f, 0.0f));
  CHECK(o.rotation() == 0);
}

void testFlipsBackWithHysteresis() {
  Orientation o;
  feed(o, 0, -1.0f);
  CHECK(feed(o, 1000, -1.0f));
  CHECK(o.rotation() == 2);
  for (uint32_t t = 2000; t < 6000; t += 200) CHECK(!feed(o, t, 0.5f));  // not upright enough
  CHECK(o.rotation() == 2);
  CHECK(!feed(o, 7000, 1.0f));
  CHECK(feed(o, 8000, 1.0f));
  CHECK(o.rotation() == 0);
}

void testBusyDefersThenFlips() {
  Orientation o;
  feed(o, 0, -1.0f);
  CHECK(!feed(o, 2000, -1.0f, 0.0f, true));  // held long enough, but a finger is down
  CHECK(o.rotation() == 0);
  CHECK(feed(o, 2015, -1.0f, 0.0f, false));  // finger up, still wanted: flips at once
  CHECK(o.rotation() == 2);
}

void testBusyThenNoLongerWantedDoesNotFlip() {
  Orientation o;
  feed(o, 0, -1.0f);
  feed(o, 2000, -1.0f, 0.0f, true);
  CHECK(!feed(o, 2015, 1.0f, 0.0f, false));  // turned back upright meanwhile
  CHECK(o.rotation() == 0);
}

void testNanKeepsOrientation() {
  Orientation o;
  const float nan = std::nanf("");
  for (uint32_t t = 0; t < 5000; t += 200) CHECK(!feed(o, t, nan, nan));
  CHECK(o.rotation() == 0);
  Orientation f;  // flipped state survives garbage too
  feed(f, 0, -1.0f);
  feed(f, 1000, -1.0f);
  for (uint32_t t = 2000; t < 5000; t += 200) CHECK(!feed(f, t, nan, nan));
  CHECK(f.rotation() == 2);
}

void testMillisWrap() {
  Orientation o;
  const uint32_t t0 = 0xFFFFFF00u;
  feed(o, t0, -1.0f);
  CHECK(!feed(o, t0 + 999u, -1.0f));
  CHECK(feed(o, t0 + 1000u, -1.0f));  // t0 + 1000 wrapped past zero
}

void testStampNewerThanNow() {  // a timestamp taken later in the same pass is not "~49 days ago"
  Orientation o;
  feed(o, 100, -1.0f);
  CHECK(!feed(o, 98, -1.0f));
  CHECK(o.rotation() == 0);
}

}  // namespace

int main() {
  testStartsUpright();
  testFlipsAfterHold();
  testInterruptedHoldRestarts();
  testFlatNeverFlips();
  testTiltedFaceUpNeverFlips();
  testDeadZoneNeverFlips();
  testExactThresholdDoesNotFlip();
  testFlipsBackWithHysteresis();
  testBusyDefersThenFlips();
  testBusyThenNoLongerWantedDoesNotFlip();
  testNanKeepsOrientation();
  testMillisWrap();
  testStampNewerThanNow();
  return checksDone("orientation_test");
}
