#pragma once
// When the navigation screen owns the display. Pure: no Arduino, host-tested in
// sim/tests/nav_session_test.cpp. The caller passes `now`.
//
// A decoded frame (not Ended) starts or keeps the ride; Ended stops it. With no
// frame for NAV_LOST_MS the phone counts as lost (the screen says so); after
// NAV_END_MS the ride ends by itself. Arrived is shown for NAV_ARRIVED_MS from
// its first frame, then the ride ends.
#include <stdint.h>

#include "nav_frame.h"

constexpr uint32_t NAV_LOST_MS = 3000;
constexpr uint32_t NAV_END_MS = 20000;
constexpr uint32_t NAV_ARRIVED_MS = 8000;

class NavSession {
 public:
  void frame(NavState s, uint32_t now) {
    if (s == NavState::Ended) {
      stop();
      return;
    }
    if (!active_) {
      active_ = true;
      started_ = true;
      arrived_ = false;
    }
    if (s == NavState::Arrived) {
      if (!arrived_) {
        arrived_ = true;
        arrivedAt_ = now;
      }
    } else {
      arrived_ = false;
    }
    lastFrame_ = now;
  }

  void tick(uint32_t now) {
    if (!active_) return;
    if (arrived_ && now - arrivedAt_ >= NAV_ARRIVED_MS) stop();
    else if (now - lastFrame_ >= NAV_END_MS) stop();
  }

  bool active() const { return active_; }
  bool phoneLost(uint32_t now) const { return active_ && now - lastFrame_ >= NAV_LOST_MS; }

  bool takeStarted() {
    const bool r = started_;
    started_ = false;
    return r;
  }
  bool takeEnded() {
    const bool r = ended_;
    ended_ = false;
    return r;
  }

 private:
  void stop() {
    if (!active_) return;
    active_ = false;
    arrived_ = false;
    ended_ = true;
  }

  bool active_ = false, arrived_ = false, started_ = false, ended_ = false;
  uint32_t lastFrame_ = 0, arrivedAt_ = 0;
};
