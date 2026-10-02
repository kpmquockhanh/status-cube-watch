#include "check.h"
#include "idle_sleep.h"

namespace {

constexpr uint32_t T = 900000;  // 15 min

void testStaysAwakeBeforeTimeout() {
  IdleSleep s;
  s.begin(0);
  CHECK(s.update(T - 1, false, T));
}

void testSleepsAfterTimeout() {
  IdleSleep s;
  s.begin(0);
  CHECK(!s.update(T, false, T));
  CHECK(!s.update(T + 5000, false, T));
}

void testDisabledNeverSleeps() {
  IdleSleep s;
  s.begin(0);
  CHECK(s.update(0xF0000000u, false, 0));
}

void testDataKeepsAwake() {
  IdleSleep s;
  s.begin(0);
  for (uint32_t t = 5000; t < 4 * T; t += 5000) {
    s.data(t);
    CHECK(s.update(t, false, T));
  }
}

void testDataDoesNotWake() {
  IdleSleep s;
  s.begin(0);
  CHECK(!s.update(T, false, T));
  s.data(T + 10);
  CHECK(!s.update(T + 20, false, T));
}

void testTouchWakesAndIsConsumed() {
  IdleSleep s;
  s.begin(0);
  s.update(T, false, T);
  CHECK(s.touch(T + 100));    // woke: swallow this touch
  CHECK(s.update(T + 100, false, T));
  CHECK(!s.touch(T + 200));   // already awake: normal touch
  CHECK(s.update(T + 200 + T - 1, false, T));
  CHECK(!s.update(T + 200 + T, false, T));
}

void testBlockedKeepsAwakeAndRestartsTimer() {
  IdleSleep s;
  s.begin(0);
  CHECK(s.update(T * 3, true, T));        // blocked at 3T
  CHECK(s.update(T * 3 + T - 1, false, T));
  CHECK(!s.update(T * 4, false, T));
}

void testBlockedWakesSleeper() {
  IdleSleep s;
  s.begin(0);
  CHECK(!s.update(T, false, T));
  CHECK(s.update(T + 1, true, T));
}

void testMillisWrap() {
  IdleSleep s;
  s.begin(0xFFFFFFFFu - 1000);
  CHECK(s.update(0xFFFFFFFFu - 1000 + T - 1, false, T));
  CHECK(!s.update(0xFFFFFFFFu - 1000 + T, false, T));
}

void testStampNewerThanNow() {  // touch stamped later in the same loop pass than update()'s `now`
  IdleSleep s;
  s.begin(0);
  CHECK(!s.update(T, false, T));
  CHECK(s.touch(T + 3));
  CHECK(s.update(T, false, T));
}

}  // namespace

int main() {
  testStaysAwakeBeforeTimeout();
  testSleepsAfterTimeout();
  testDisabledNeverSleeps();
  testDataKeepsAwake();
  testDataDoesNotWake();
  testTouchWakesAndIsConsumed();
  testBlockedKeepsAwakeAndRestartsTimer();
  testBlockedWakesSleeper();
  testMillisWrap();
  testStampNewerThanNow();
  return checksDone("idle_sleep_test");
}
