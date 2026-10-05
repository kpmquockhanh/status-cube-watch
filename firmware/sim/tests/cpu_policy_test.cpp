#include "check.h"
#include "cpu_policy.h"

namespace {

void testFastAtBoot() {
  CpuPolicy p;
  p.begin(5000);
  CHECK(p.update(5000, false) == CPU_MHZ_FAST);
  CHECK(p.update(5000 + CPU_HOLD_MS - 1, false) == CPU_MHZ_FAST);
  CHECK(p.update(5000 + CPU_HOLD_MS, false) == CPU_MHZ_SLOW);
}

void testIdleIsSlow() {
  CpuPolicy p;
  p.begin(0);
  for (uint32_t t = CPU_HOLD_MS; t < 600000; t += 1000) CHECK(p.update(t, false) == CPU_MHZ_SLOW);
}

void testBusyIsFastAndHolds() {
  CpuPolicy p;
  p.begin(0);
  CHECK(p.update(10000, false) == CPU_MHZ_SLOW);
  CHECK(p.update(10010, true) == CPU_MHZ_FAST);   // finger down or ring moving
  CHECK(p.update(10500, true) == CPU_MHZ_FAST);
  CHECK(p.update(10500 + CPU_HOLD_MS - 1, false) == CPU_MHZ_FAST);  // the frame that answers the tap
  CHECK(p.update(10500 + CPU_HOLD_MS, false) == CPU_MHZ_SLOW);
}

void testGapsShorterThanHoldStayFast() {  // animations chained with a pause between them
  CpuPolicy p;
  p.begin(0);
  uint32_t t = 20000;
  for (int i = 0; i < 10; i++) {
    CHECK(p.update(t, true) == CPU_MHZ_FAST);
    t += CPU_HOLD_MS / 2;
    CHECK(p.update(t, false) == CPU_MHZ_FAST);
  }
}

void testMillisWrap() {
  CpuPolicy p;
  p.begin(0);
  p.update(0xFFFFFFFFu - 100, true);
  CHECK(p.update(0xFFFFFFFFu - 100 + CPU_HOLD_MS - 1, false) == CPU_MHZ_FAST);
  CHECK(p.update(0xFFFFFFFFu - 100 + CPU_HOLD_MS, false) == CPU_MHZ_SLOW);
}

void testStampNewerThanNow() {  // pollTouch reads millis() itself, after loop()'s `now`
  CpuPolicy p;
  p.begin(0);
  p.update(50000, true);
  CHECK(p.update(49990, false) == CPU_MHZ_FAST);
}

}  // namespace

int main() {
  testFastAtBoot();
  testIdleIsSlow();
  testBusyIsFastAndHolds();
  testGapsShorterThanHoldStayFast();
  testMillisWrap();
  testStampNewerThanNow();
  return checksDone("cpu_policy_test");
}
