#include "check.h"
#include "nav_session.h"

namespace {

void testStartEnd() {
  NavSession n;
  CHECK(!n.active());
  CHECK(!n.takeStarted());
  n.frame(NavState::Navigating, 100);
  CHECK(n.active());
  CHECK(n.takeStarted());
  CHECK(!n.takeStarted());
  n.frame(NavState::Navigating, 200);
  CHECK(!n.takeStarted());
  n.frame(NavState::Ended, 300);
  CHECK(!n.active());
  CHECK(n.takeEnded());
  CHECK(!n.takeEnded());
  n.frame(NavState::Ended, 400);  // nothing to end
  CHECK(!n.takeEnded());
}

void testActiveStates() {
  NavSession a;
  a.frame(NavState::Rerouting, 0);
  CHECK(a.active());
  NavSession b;
  b.frame(NavState::OffRouteNoSignal, 0);
  CHECK(b.active());
}

void testArrived() {
  NavSession n;
  n.frame(NavState::Arrived, 1000);
  CHECK(n.active());
  n.frame(NavState::Arrived, 5000);  // does not extend
  n.tick(1000 + NAV_ARRIVED_MS - 1);
  CHECK(n.active());
  n.frame(NavState::Arrived, 1000 + NAV_ARRIVED_MS - 1);
  n.tick(1000 + NAV_ARRIVED_MS);
  CHECK(!n.active());
  CHECK(n.takeStarted());
  CHECK(n.takeEnded());
}

void testLost() {
  NavSession n;
  n.frame(NavState::Navigating, 0);
  n.tick(2999);
  CHECK(!n.phoneLost(2999));
  CHECK(n.phoneLost(3000));
  n.tick(19999);
  CHECK(n.active());
  n.tick(NAV_END_MS);
  CHECK(!n.active());
  CHECK(n.takeEnded());
  CHECK(!n.phoneLost(NAV_END_MS));
}

void testRecovery() {
  NavSession n;
  n.frame(NavState::Navigating, 0);
  n.tick(15000);
  CHECK(n.phoneLost(15000));
  n.frame(NavState::Navigating, 15000);
  CHECK(!n.phoneLost(15000));
  n.tick(15001);
  CHECK(n.active());
  n.tick(15000 + NAV_END_MS - 1);
  CHECK(n.active());
}

void testWrap() {
  const uint32_t t0 = 0xFFFFF000u;
  NavSession n;
  n.frame(NavState::Navigating, t0);
  CHECK(!n.phoneLost(t0 + 2999));
  CHECK(n.phoneLost(t0 + 3000));
  n.tick(t0 + NAV_END_MS - 1);
  CHECK(n.active());
  n.tick(t0 + NAV_END_MS);
  CHECK(!n.active());
  NavSession a;
  a.frame(NavState::Arrived, t0);
  a.tick(t0 + NAV_ARRIVED_MS - 1);
  CHECK(a.active());
  a.tick(t0 + NAV_ARRIVED_MS);
  CHECK(!a.active());
}

}  // namespace

int main() {
  testStartEnd();
  testActiveStates();
  testArrived();
  testLost();
  testRecovery();
  testWrap();
  return checksDone("nav_session_test");
}
