#include <cstring>
#include <fstream>
#include <string>

#include "check.h"
#include "unlock_prompt.h"
#include "volume_frame.h"

namespace {

void testHiddenUntilLocked() {
  UnlockPrompt u;
  CHECK(u.screen(0) == UnlockScreen::Hidden);
  CHECK(!u.tap(0));  // a tap on the deck sends nothing
  u.fromMac(true, 100);
  CHECK(u.screen(100) == UnlockScreen::Prompt);
  u.fromMac(false, 200);
  CHECK(u.screen(200) == UnlockScreen::Hidden);
}

void testTapSendsOnce() {
  UnlockPrompt u;
  u.fromMac(true, 0);
  CHECK(u.tap(1000));
  CHECK(u.screen(1000) == UnlockScreen::Waiting);
  CHECK(!u.tap(1500));  // no repeat while the Mac answers
  u.fromMac(false, 2000);  // unlocked
  CHECK(u.screen(2000) == UnlockScreen::Hidden);
}

void testWaitTimesOut() {
  UnlockPrompt u;
  u.fromMac(true, 0);
  u.tick(0);
  CHECK(u.tap(1000));
  CHECK(u.tick(1000));  // Prompt -> Waiting
  CHECK(!u.tick(1000 + UNLOCK_WAIT_MS - 1));
  CHECK(u.tick(1000 + UNLOCK_WAIT_MS));  // back to the prompt
  CHECK(u.screen(1000 + UNLOCK_WAIT_MS) == UnlockScreen::Prompt);
  CHECK(u.tap(1000 + UNLOCK_WAIT_MS));  // and it can ask again
}

void testDismissAndRearm() {
  UnlockPrompt u;
  u.fromMac(true, 0);
  u.tick(0);
  u.dismiss(5000);
  CHECK(u.tick(5000));  // Prompt -> Hidden
  CHECK(u.screen(5000) == UnlockScreen::Hidden);
  CHECK(!u.tap(5100));
  u.touch(20000);  // using the deck holds it off
  CHECK(u.screen(20000 + UNLOCK_REARM_MS - 1) == UnlockScreen::Hidden);
  CHECK(u.tick(20000 + UNLOCK_REARM_MS));
  CHECK(u.screen(20000 + UNLOCK_REARM_MS) == UnlockScreen::Prompt);
}

void testFreshLockRearms() {
  UnlockPrompt u;
  u.fromMac(true, 0);
  u.dismiss(100);
  u.fromMac(true, 200);  // the same lock again changes nothing
  CHECK(u.screen(200) == UnlockScreen::Hidden);
  u.fromMac(false, 300);
  u.fromMac(true, 400);
  CHECK(u.screen(400) == UnlockScreen::Prompt);
}

void testWrapSafe() {
  UnlockPrompt u;
  u.fromMac(true, 0);
  const uint32_t t = 0xFFFFFFF0u;
  CHECK(u.tap(t));
  CHECK(u.screen(t + 100) == UnlockScreen::Waiting);  // across the millis() wrap
  u.tick(t + UNLOCK_WAIT_MS);
  CHECK(u.screen(t + UNLOCK_WAIT_MS) == UnlockScreen::Prompt);
}

void testFlagParse() {
  MacVolume v{};
  const uint8_t locked[] = {1, 40, VOL_FLAG_CAN_SET | VOL_FLAG_MAC_LOCKED};
  CHECK(volumeParse(locked, sizeof(locked), v) && v.macLocked && v.canSet);
  MacVolume w = v;
  const uint8_t open[] = {1, 40, VOL_FLAG_CAN_SET};
  CHECK(volumeParse(open, sizeof(open), w) && !w.macLocked);
  CHECK(!volumeSame(v, w));  // a lock change alone reaches the main loop
}

// unlock <bytes>
void testFixture() {
  std::ifstream f("fixtures/ble-frames.txt");
  std::string line;
  int n = 0;
  while (std::getline(f, line)) {
    if (line.rfind("unlock ", 0) != 0) continue;
    uint8_t got[BLE_UNLOCK_REQ_LEN];
    unlockRequestEncode(got);
    CHECK(line == "unlock 07" && got[0] == 0x07);
    n++;
  }
  CHECK(n == 1);
}

}  // namespace

int main() {
  testHiddenUntilLocked();
  testTapSendsOnce();
  testWaitTimesOut();
  testDismissAndRearm();
  testFreshLockRearms();
  testWrapSafe();
  testFlagParse();
  testFixture();
  return checksDone("unlock_prompt_test");
}
