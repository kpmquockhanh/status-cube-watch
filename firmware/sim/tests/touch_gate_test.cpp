#include "touch_gate.h"
#include "check.h"

namespace {

constexpr uint32_t FB = TouchGate::FALLBACK_MS;

// A gate that has seen INT announce a finger, which has since lifted.
TouchGate trustedIdle(uint32_t now) {
  TouchGate g;
  g.readDone(true, true, true, now);
  g.readDone(true, false, false, now);
  return g;
}

void testUntrustedReadsEveryPass() {
  TouchGate g;
  CHECK(!g.trusted());
  g.readDone(true, false, false, 1000);
  CHECK(g.shouldRead(false, 1000));
  CHECK(g.shouldRead(false, 1015));
}

void testAnnouncedFingerEarnsTrust() {
  TouchGate g;
  g.readDone(true, false, false, 1000);
  g.readDone(true, true, true, 1015);
  CHECK(g.trusted());
  g.readDone(true, false, false, 1100);
  CHECK(!g.shouldRead(false, 1115));
  CHECK(!g.shouldRead(false, 1100 + FB - 1));
  CHECK(g.shouldRead(false, 1100 + FB));  // the fallback
  CHECK(g.shouldRead(true, 1115));        // an edge
}

void testUnannouncedFingerDoesNotEarnTrust() {
  TouchGate g;
  g.readDone(true, true, false, 1000);
  CHECK(!g.trusted());
  // Edges while the finger stays down (some variants pulse while it moves)
  // say nothing about whether INT announces a landing one.
  g.readDone(true, true, true, 1015);
  CHECK(!g.trusted());
}

void testFingerDownReadsEveryPass() {
  TouchGate g = trustedIdle(1000);
  g.readDone(true, true, true, 2000);
  CHECK(g.shouldRead(false, 2015));
  g.readDone(true, true, false, 2015);
  CHECK(g.shouldRead(false, 2030));
  g.readDone(true, false, false, 2030);  // lifted
  CHECK(!g.shouldRead(false, 2045));
  CHECK(g.trusted());
}

void testMissedFingerRevokesTrust() {
  TouchGate g = trustedIdle(1000);
  g.readDone(true, true, false, 1000 + FB);  // the fallback found a finger INT never announced
  CHECK(!g.trusted());
  g.readDone(true, false, false, 1300);
  CHECK(g.shouldRead(false, 1315));
  // and the next announced finger restores it
  g.readDone(true, true, true, 1400);
  CHECK(g.trusted());
}

void testDroppedReadIsNotALift() {
  TouchGate g = trustedIdle(1000);
  g.readDone(true, true, true, 2000);
  g.readDone(false, false, false, 2015);
  CHECK(g.shouldRead(false, 2030));
  g.readDone(true, true, false, 2030);  // still the same touch
  CHECK(g.trusted());
}

void testDroppedReadWhileIdleWaitsForTheFallback() {
  TouchGate g = trustedIdle(1000);
  g.readDone(false, false, false, 2000);
  CHECK(!g.shouldRead(false, 2015));
  CHECK(g.shouldRead(false, 2000 + FB));
}

void testMillisWrap() {
  TouchGate g = trustedIdle(0xFFFFFF80u);
  CHECK(!g.shouldRead(false, 0x10));                // 144 ms later, past the wrap
  CHECK(g.shouldRead(false, 0xFFFFFF80u + FB));     // wraps to 0x7A
}

}  // namespace

int main() {
  testUntrustedReadsEveryPass();
  testAnnouncedFingerEarnsTrust();
  testUnannouncedFingerDoesNotEarnTrust();
  testFingerDownReadsEveryPass();
  testMissedFingerRevokesTrust();
  testDroppedReadIsNotALift();
  testDroppedReadWhileIdleWaitsForTheFallback();
  testMillisWrap();
  return checksDone("touch_gate_test");
}
