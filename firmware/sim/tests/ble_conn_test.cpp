#include "ble_conn.h"
#include "check.h"

namespace {

// ms -> spec units, so the cases below read like Apple's table.
constexpr uint16_t itvl(double ms) { return (uint16_t)(ms / 1.25); }
constexpr uint16_t tmo(double ms) { return (uint16_t)(ms / 10); }

void testChosenParamsAreAccepted() {
  CHECK(bleAppleAccepts(BLE_IDLE_PARAMS));
  // Longer than the 15 ms a Mac opens at, or the request saves nothing...
  CHECK(BLE_IDLE_PARAMS.minItvl * 1.25 >= 30);
  // ...yet a write from the Mac still starts well inside half a second.
  CHECK(BLE_IDLE_PARAMS.maxItvl * 1.25 * (BLE_IDLE_PARAMS.latency + 1) <= 500);
}

void testMinInterval() {
  CHECK(bleAppleAccepts({itvl(15), itvl(30), 0, tmo(2000)}));
  CHECK(!bleAppleAccepts({itvl(11.25), itvl(30), 0, tmo(2000)}));  // under 15 ms
  CHECK(!bleAppleAccepts({itvl(20), itvl(35), 0, tmo(2000)}));     // not a multiple of 15 ms
}

void testMaxInterval() {
  CHECK(!bleAppleAccepts({itvl(30), itvl(43.75), 0, tmo(2000)}));  // under min + 15 ms
  CHECK(bleAppleAccepts({itvl(30), itvl(45), 0, tmo(2000)}));
  CHECK(bleAppleAccepts({itvl(15), itvl(15), 0, tmo(2000)}));      // the one equal pair allowed
  CHECK(!bleAppleAccepts({itvl(30), itvl(30), 0, tmo(2000)}));
}

void testLatency() {
  CHECK(bleAppleAccepts({itvl(15), itvl(30), 30, tmo(6000)}));
  CHECK(!bleAppleAccepts({itvl(15), itvl(30), 31, tmo(6000)}));
}

void testSilentSpan() {
  // max x (latency + 1) <= 2 s. With the timeout capped at 6 s, the 3x rule
  // below already keeps it under 2 s, so these sit just either side.
  CHECK(bleAppleAccepts({itvl(30), itvl(105), 18, tmo(6000)}));   // 1995 ms
  CHECK(!bleAppleAccepts({itvl(30), itvl(105), 19, tmo(6000)}));  // 2100 ms
}

void testSupervisionTimeout() {
  CHECK(bleAppleAccepts({itvl(15), itvl(30), 0, tmo(2000)}));
  CHECK(!bleAppleAccepts({itvl(15), itvl(30), 0, tmo(1990)}));
  CHECK(bleAppleAccepts({itvl(15), itvl(30), 0, tmo(6000)}));
  CHECK(!bleAppleAccepts({itvl(15), itvl(30), 0, tmo(6010)}));
  // and longer than three silent spans: 675 ms x 3 = 2025 ms
  CHECK(bleAppleAccepts({itvl(60), itvl(675), 0, tmo(2030)}));
  CHECK(!bleAppleAccepts({itvl(60), itvl(675), 0, tmo(2020)}));
}

void testNimbleExampleParamsAreRejected() {
  // 30..60 ms with a 1.8 s timeout, as in NimBLE's server example: too short a
  // timeout for a Mac, so copying it would have done nothing.
  CHECK(!bleAppleAccepts({24, 48, 0, 180}));
}

}  // namespace

int main() {
  testChosenParamsAreAccepted();
  testMinInterval();
  testMaxInterval();
  testLatency();
  testSilentSpan();
  testSupervisionTimeout();
  testNimbleExampleParamsAreRejected();
  return checksDone("ble_conn_test");
}
