#pragma once
// Which transport feeds the cube. Pure (no Arduino) so it is host-tested.
// BLE is preferred: while a complete payload arrived in the last BLE_LIVE_MS
// the WiFi radio stays off. When BLE goes stale, WiFi comes up and polls the
// bridge; it drops again only after BLE has been live for BLE_HOLD_MS in a
// row, so a flaky link does not flap the radio.

#include <stdint.h>

constexpr uint32_t BLE_LIVE_MS = 15000;
constexpr uint32_t BLE_HOLD_MS = 30000;

struct TransportDecision {
  bool bleLive;  // a payload arrived over BLE within BLE_LIVE_MS
  bool wifiOn;   // WiFi should be up and polling
};

class TransportPolicy {
 public:
  // `startMs` is when the cube booted: the grace period for BLE to deliver its
  // first payload is measured from it.
  void begin(uint32_t startMs) { start_ = startMs; wifiOn_ = false; wasLive_ = false; liveSince_ = 0; }

  // bleLastGood: millis() of the last valid BLE payload, 0 if none yet.
  // bonded: a Mac is bonded. wifiConfigured: there is an SSID to join.
  // keepWifi: keep WiFi up even while BLE is live (OTA needs it).
  TransportDecision update(uint32_t now, uint32_t bleLastGood, bool bonded, bool wifiConfigured, bool keepWifi) {
    const bool live = bleLastGood != 0 && (uint32_t)(now - bleLastGood) < BLE_LIVE_MS;
    if (live && !wasLive_) liveSince_ = now;
    wasLive_ = live;

    if (!wifiConfigured) {
      wifiOn_ = false;
    } else if (keepWifi || !bonded) {
      wifiOn_ = true;  // nobody to wait for, or WiFi explicitly wanted
    } else if (live) {
      if (wifiOn_ && (uint32_t)(now - liveSince_) >= BLE_HOLD_MS) wifiOn_ = false;
    } else {
      const uint32_t ref = bleLastGood ? bleLastGood : start_;
      if ((uint32_t)(now - ref) >= BLE_LIVE_MS) wifiOn_ = true;
    }
    return {live, wifiOn_};
  }

 private:
  uint32_t start_ = 0;
  uint32_t liveSince_ = 0;
  bool wifiOn_ = false;
  bool wasLive_ = false;
};

// A failed WiFi join opens the setup portal by itself only when there is no
// bond: without a Mac the cube has no other way to get data. With a bond the
// portal is still reachable by holding the screen at boot.
inline bool portalOnJoinFail(bool bonded) { return !bonded; }

// First boot of a cube that has neither a Mac nor a network: show the "pair
// with Mac" screen instead of the empty deck.
inline bool pairWaitAtBoot(bool bonded, bool haveWifi) { return !bonded && !haveWifi; }
