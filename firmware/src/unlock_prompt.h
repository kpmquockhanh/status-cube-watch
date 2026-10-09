#pragma once
// The "tap to unlock" screen (docs/ble-protocol.md, "Unlock"). Pure: no
// Arduino, host-tested in sim/tests/unlock_prompt_test.cpp.
//
// The Mac sets VOL_FLAG_MAC_LOCKED while its screen is locked and "Unlock with
// cube" is on. The cube then covers the deck with a prompt; a tap sends Control
// 07 and the prompt says UNLOCKING until the Mac reports it is unlocked, or
// UNLOCK_WAIT_MS passes and the prompt asks again. A swipe puts the prompt away
// to show the deck; it comes back after UNLOCK_REARM_MS without a touch, or
// when the Mac locks again.
#include <stdint.h>

constexpr uint32_t UNLOCK_WAIT_MS = 6000;
constexpr uint32_t UNLOCK_REARM_MS = 30000;

enum class UnlockScreen : uint8_t { Hidden, Prompt, Waiting };

class UnlockPrompt {
 public:
  // The Mac's lock state; false also once the link drops.
  void fromMac(bool locked, uint32_t now) {
    if (locked && !locked_) dismissed_ = false;  // a fresh lock asks again
    if (!locked) waiting_ = false;
    locked_ = locked;
    (void)now;
  }

  // A finger is on the glass: it holds off the prompt's return.
  void touch(uint32_t now) { lastTouch_ = now; }

  // A tap on the prompt. True when Control 07 should go out: once per prompt,
  // never while the last one is still being answered.
  bool tap(uint32_t now) {
    if (screen(now) != UnlockScreen::Prompt) return false;
    waiting_ = true;
    sentAt_ = now;
    return true;
  }

  // A swipe: show the deck for now.
  void dismiss(uint32_t now) {
    dismissed_ = true;
    waiting_ = false;
    lastTouch_ = now;
  }

  UnlockScreen screen(uint32_t now) const {
    if (!locked_) return UnlockScreen::Hidden;
    if (waiting_) return UnlockScreen::Waiting;
    if (dismissed_ && now - lastTouch_ < UNLOCK_REARM_MS) return UnlockScreen::Hidden;
    return UnlockScreen::Prompt;
  }

  // True when what screen() returns changed since the last call, so the caller
  // redraws: a wait that ran out, a dismissed prompt coming back.
  bool tick(uint32_t now) {
    if (waiting_ && now - sentAt_ >= UNLOCK_WAIT_MS) waiting_ = false;
    if (dismissed_ && now - lastTouch_ >= UNLOCK_REARM_MS) dismissed_ = false;
    const UnlockScreen s = screen(now);
    const bool changed = s != last_;
    last_ = s;
    return changed;
  }

 private:
  bool locked_ = false;
  bool waiting_ = false;
  bool dismissed_ = false;
  uint32_t sentAt_ = 0;
  uint32_t lastTouch_ = 0;
  UnlockScreen last_ = UnlockScreen::Hidden;
};
