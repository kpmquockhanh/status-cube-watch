// Desktop stand-in for net_ble.cpp. A laptop has no BLE peripheral here, so
// CUBE_BLE selects what the cube believes:
//   (unset)|none  no Mac: the simulator behaves exactly as it did before BLE
//   live          bonded; a payload "arrives over BLE" every 5 s (it is fetched
//                 from the bridge over HTTP, the parsing is the real one)
//   stale         bonded; nothing ever arrives, so WiFi takes over after 15 s
//   pair          a Mac is pairing: the passkey screen shows

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include "../src/ble.h"
#include "../src/net.h"

namespace {

enum class Mode { None, Live, Stale, Pair };

Mode mode() {
  static const Mode m = [] {
    const char *e = getenv("CUBE_BLE");
    if (!e) return Mode::None;
    if (!strcmp(e, "live")) return Mode::Live;
    if (!strcmp(e, "stale")) return Mode::Stale;
    if (!strcmp(e, "pair")) return Mode::Pair;
    return Mode::None;
  }();
  return m;
}

uint32_t g_lastGood = 0;
uint32_t g_lastFetch = 0;

}  // namespace

void bleBegin() { Serial.printf("[ble] simulator mode %d (CUBE_BLE=live|stale|pair|none)\n", (int)mode()); }

BleState bleState() {
  switch (mode()) {
    case Mode::Live: return BleState::Connected;
    case Mode::Pair: return BleState::Pairing;
    default: return BleState::Advertising;
  }
}

uint32_t blePasskey() { return mode() == Mode::Pair ? 482913u : 0u; }

bool bleBonded() { return mode() == Mode::Live || mode() == Mode::Stale; }

bool bleTake(Payload &out) {
  if (mode() != Mode::Live) return false;
  const uint32_t now = millis();
  if (g_lastFetch && now - g_lastFetch < 5000) return false;
  g_lastFetch = now ? now : 1;
  if (!netFetch(out)) return false;
  g_lastGood = g_lastFetch;
  return true;
}

uint32_t bleLastGood() { return g_lastGood; }

void bleForgetBonds() { Serial.println("[ble] (sim) forget bonds"); }
