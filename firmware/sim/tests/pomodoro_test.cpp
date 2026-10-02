#include <cstdint>
#include <cstring>

#include "check.h"
#include "pomodoro.h"

namespace {

constexpr uint32_t MIN = 60000;
const PomoConfig CFG{25 * MIN, 5 * MIN, 15 * MIN, 4};

// A fake clock around one Pomodoro, the way main.cpp drives the real one.
struct Rig {
  Pomodoro p{CFG};
  uint32_t now;
  explicit Rig(uint32_t start = 1000) : now(start) {}
  void advance(uint32_t ms) {
    now += ms;
    p.tick(now);
  }
  void press() { p.longPress(now); }
};

void testIdle() {
  Rig r;
  const PomoView v = r.p.view();
  CHECK(v.state == POMO_IDLE);
  CHECK(v.phase == PHASE_FOCUS);
  CHECK(v.fraction == 100);
  CHECK(v.displaySec == 1500);
  CHECK(v.completed == 0);
  CHECK(v.sessions == 4);
  r.advance(180 * MIN);  // an idle timer does not run
  CHECK(r.p.view().state == POMO_IDLE);
  CHECK(!r.p.takeAlert());
}

void testCountdown() {
  Rig r;
  r.press();
  CHECK(r.p.view().state == POMO_FOCUS);
  r.advance(1);
  CHECK(r.p.view().displaySec == 1500);  // rounds up: 25:00 for the first second
  r.advance(999);
  CHECK(r.p.view().displaySec == 1499);
  r.advance(59 * 1000);  // 60 s in
  CHECK(r.p.view().displaySec == 1440);
  CHECK(r.p.view().fraction == 96);
}

void testFocusEnds() {
  Rig r;
  r.press();
  r.advance(25 * MIN - 1);
  CHECK(r.p.view().state == POMO_FOCUS);
  CHECK(!r.p.takeAlert());
  r.advance(1);
  const PomoView v = r.p.view();
  CHECK(v.state == POMO_DONE);
  CHECK(v.phase == PHASE_FOCUS);
  CHECK(v.next == PHASE_SHORT);
  CHECK(v.completed == 1);
  CHECK(v.fraction == 0);
  CHECK(v.displaySec == 0);
  CHECK(r.p.takeAlert());
  CHECK(!r.p.takeAlert());  // once per phase end
}

// Review Focus 2: tick can be starved for seconds by a blocking HTTP fetch.
void testHugeGap() {
  Rig r;
  r.press();
  r.advance(5 * 60 * MIN);
  CHECK(r.p.view().state == POMO_DONE);
  CHECK(r.p.view().completed == 1);
  CHECK(r.p.takeAlert());
  r.advance(60 * MIN);  // DONE does not run on or count again
  CHECK(r.p.view().completed == 1);
  CHECK(!r.p.takeAlert());
}

void testBreakCycle() {
  Rig r;
  r.press();
  r.advance(25 * MIN);
  CHECK(r.p.takeAlert());
  r.press();  // start the break
  PomoView v = r.p.view();
  CHECK(v.state == POMO_BREAK);
  CHECK(v.phase == PHASE_SHORT);
  CHECK(v.leftMs == 5 * MIN);
  CHECK(v.completed == 1);
  r.advance(5 * MIN);
  v = r.p.view();
  CHECK(v.state == POMO_DONE);
  CHECK(v.phase == PHASE_SHORT);
  CHECK(v.next == PHASE_FOCUS);
  CHECK(v.completed == 1);
  CHECK(r.p.takeAlert());
}

void testFullSetAndLongBreak() {
  Rig r;
  for (int i = 1; i <= 4; i++) {
    r.press();  // start focus (from IDLE, then from DONE)
    r.advance(25 * MIN);
    CHECK(r.p.view().state == POMO_DONE);
    CHECK(r.p.view().completed == i);
    if (i < 4) {
      CHECK(r.p.view().next == PHASE_SHORT);
      r.press();
      r.advance(5 * MIN);
      CHECK(r.p.view().next == PHASE_FOCUS);
    }
  }
  CHECK(r.p.view().next == PHASE_LONG);
  CHECK(r.p.view().completed == 4);

  r.press();
  PomoView v = r.p.view();
  CHECK(v.state == POMO_BREAK);
  CHECK(v.phase == PHASE_LONG);
  CHECK(v.leftMs == 15 * MIN);
  CHECK(v.completed == 4);  // the set counts as complete while the long break runs

  r.advance(15 * MIN);
  v = r.p.view();
  CHECK(v.state == POMO_DONE);
  CHECK(v.completed == 0);  // and starts over once it ends
  CHECK(v.next == PHASE_FOCUS);
}

void testPauseAndResume() {
  Rig r;
  r.press();
  r.advance(60000);
  r.press();
  CHECK(r.p.view().state == POMO_PAUSED);
  const uint32_t left = r.p.view().leftMs;
  r.advance(10 * MIN);  // paused time is not counted
  CHECK(r.p.view().leftMs == left);
  CHECK(!r.p.takeAlert());
  r.press();
  CHECK(r.p.view().state == POMO_FOCUS);
  r.advance(60000);
  CHECK(r.p.view().leftMs == 25 * MIN - 120000);
}

void testPauseDuringBreakResumesBreak() {
  Rig r;
  r.press();
  r.advance(25 * MIN);
  r.press();  // break
  r.advance(60000);
  r.press();  // pause
  CHECK(r.p.view().state == POMO_PAUSED);
  CHECK(r.p.view().phase == PHASE_SHORT);
  r.press();  // resume
  CHECK(r.p.view().state == POMO_BREAK);
}

// Review Focus 2: the pause press lands after the phase has actually ended.
void testPressAtTheEnd() {
  Rig r;
  r.press();
  r.advance(25 * MIN - 1);
  r.now += 1;
  r.p.longPress(r.now);  // elapsed == left: the phase is over, so this is not a pause
  CHECK(r.p.view().state == POMO_DONE);
  CHECK(r.p.takeAlert());
}

void testReset() {
  Rig r;
  r.press();
  r.advance(25 * MIN);  // DONE with an alert nobody has taken
  r.press();            // break, completed == 1
  r.p.reset();
  const PomoView v = r.p.view();
  CHECK(v.state == POMO_IDLE);
  CHECK(v.completed == 0);
  CHECK(v.leftMs == 25 * MIN);
  CHECK(v.phase == PHASE_FOCUS);
  CHECK(!r.p.takeAlert());
  r.press();  // and it starts again from the top
  CHECK(r.p.view().state == POMO_FOCUS);
}

void testResetClearsPendingAlert() {
  Rig r;
  r.press();
  r.advance(25 * MIN);  // alert pending
  r.p.reset();
  CHECK(!r.p.takeAlert());
}

// Review Focus 3: millis() wraps every ~49 days.
void testClockWrap() {
  Rig r(0xFFFFFF00u);
  r.press();
  r.advance(1000);  // crosses the wrap
  CHECK(r.p.view().state == POMO_FOCUS);
  CHECK(r.p.view().leftMs == 25 * MIN - 1000);
}

void testCustomConfig() {
  const PomoConfig cfg{1000, 500, 1500, 2};
  Pomodoro p(cfg);
  p.longPress(0);
  p.tick(1000);
  CHECK(p.view().state == POMO_DONE);
  CHECK(p.view().completed == 1);
  CHECK(p.view().next == PHASE_SHORT);
  p.longPress(1000);
  p.tick(1500);
  CHECK(p.view().next == PHASE_FOCUS);
  p.longPress(1500);
  p.tick(2500);
  CHECK(p.view().completed == 2);
  CHECK(p.view().next == PHASE_LONG);  // sessions is configurable
}

void testFormat() {
  char b[8];
  pomoFormatTime(1500, b, sizeof(b));
  CHECK(!strcmp(b, "25:00"));
  pomoFormatTime(0, b, sizeof(b));
  CHECK(!strcmp(b, "00:00"));
  pomoFormatTime(59, b, sizeof(b));
  CHECK(!strcmp(b, "00:59"));
  pomoFormatTime(3599, b, sizeof(b));
  CHECK(!strcmp(b, "59:59"));
  pomoFormatTime(5999, b, sizeof(b));
  CHECK(!strcmp(b, "99:59"));
  pomoFormatTime(6000, b, sizeof(b));
  CHECK(!strcmp(b, "99:59"));  // clamped, never wider than five characters
  pomoFormatTime(0xFFFFFFFFu, b, sizeof(b));
  CHECK(!strcmp(b, "99:59"));
}

void testSetConfigWhileIdle() {
  Rig r;
  r.p.setConfig(PomoConfig{50 * MIN, 10 * MIN, 30 * MIN, 2});
  PomoView v = r.p.view();
  CHECK(v.state == POMO_IDLE);
  CHECK(v.displaySec == 3000);  // the idle clock shows the new focus length
  CHECK(v.fraction == 100);
  CHECK(v.sessions == 2);
  r.press();
  r.advance(50 * MIN - 1);
  CHECK(r.p.view().state == POMO_FOCUS);
  r.advance(1);
  CHECK(r.p.view().state == POMO_DONE);  // the new length is honoured
}

// A running phase is never resized under the user.
void testSetConfigIgnoredWhenNotIdle() {
  const PomoConfig other{50 * MIN, 10 * MIN, 30 * MIN, 2};

  Rig run;
  run.press();
  run.advance(MIN);
  run.p.setConfig(other);
  CHECK(run.p.view().displaySec == 24 * 60);
  CHECK(run.p.view().sessions == 4);

  Rig pau;
  pau.press();
  pau.advance(MIN);
  pau.press();  // paused
  pau.p.setConfig(other);
  CHECK(pau.p.view().state == POMO_PAUSED);
  CHECK(pau.p.view().displaySec == 24 * 60);

  Rig done;
  done.press();
  done.advance(25 * MIN);
  done.p.setConfig(other);
  CHECK(done.p.view().state == POMO_DONE);
  CHECK(done.p.view().sessions == 4);
  done.press();  // starts the break with the ORIGINAL 5 min
  CHECK(done.p.view().displaySec == 5 * 60);
}

}  // namespace

int main() {
  testIdle();
  testCountdown();
  testFocusEnds();
  testHugeGap();
  testBreakCycle();
  testFullSetAndLongBreak();
  testPauseAndResume();
  testPauseDuringBreakResumesBreak();
  testPressAtTheEnd();
  testReset();
  testResetClearsPendingAlert();
  testClockWrap();
  testCustomConfig();
  testFormat();
  testSetConfigWhileIdle();
  testSetConfigIgnoredWhenNotIdle();
  return checksDone("pomodoro_test");
}
