// Desktop stand-in for net_ble.cpp. A laptop has no BLE peripheral here, so
// CUBE_BLE selects what the cube believes:
//   (unset)|none  no Mac: the simulator behaves exactly as it did before BLE
//   live          bonded; a payload "arrives over BLE" every 5 s (it is fetched
//                 from the bridge over HTTP, the parsing is the real one)
//   stale         bonded; nothing ever arrives, so WiFi takes over after 15 s
//   pair          a Mac is pairing: the passkey screen shows
// CUBE_VOLUME (only while bonded) is what the Mac reports for the Volume card,
// once: a level 0..100, `muted` (56, muted), `fixed` (an HDMI output), or
// `none` / unset (no Volume write: the card shows NO MAC). CUBE_LOCKED=1 adds
// the Mac-locked flag, so the unlock prompt shows; a tap logs [ble] (sim) unlock.

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../src/ble.h"

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

bool simFetch(Payload &out, char *err, size_t errLen);  // net_sim.cpp

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
  char err[64];
  if (!simFetch(out, err, sizeof(err))) return false;
  g_lastGood = g_lastFetch;
  return true;
}

uint32_t bleLastGood() { return g_lastGood; }

void bleForgetBonds() { Serial.println("[ble] (sim) forget bonds"); }

bool bleTakeSettings(char *, size_t) { return false; }

void bleSettingsReply(uint8_t) {}

void bleSettingsChanged() {}

void bleNotifyPomodoro(uint8_t ended, uint8_t next) {
  Serial.printf("[ble] (sim) pomodoro ended %u, next %u\n", (unsigned)ended, (unsigned)next);
}

bool bleTakeVolume(MacVolume &out) {
  static bool sent = false;
  if (sent || !bleBonded()) return false;
  const char *e = getenv("CUBE_VOLUME");
  const char *lk = getenv("CUBE_LOCKED");  // the Mac is locked: the unlock prompt shows
  const bool locked = lk && !strcmp(lk, "1");
  if (locked && (!e || !strcmp(e, "none"))) e = "56";  // the lock rides on a Volume write
  if (!e || !strcmp(e, "none")) return false;
  MacVolume v{};
  v.known = true;
  v.canSet = v.canMute = true;
  snprintf(v.name, sizeof(v.name), "MacBook Pro Speakers");
  if (!strcmp(e, "muted")) {
    v.level = 56;
    v.muted = true;
  } else if (!strcmp(e, "fixed")) {
    v.level = 100;
    v.canSet = false;
    snprintf(v.name, sizeof(v.name), "HDMI");
  } else {
    v.level = (uint8_t)constrain(atoi(e), 0, 100);
  }
  v.macLocked = locked;
  sent = true;
  out = v;
  return true;
}

void bleSendMedia(MediaKey key) {
  static const char *const names[] = {"play/pause", "next", "previous"};
  Serial.printf("[ble] (sim) media %s\n", names[(int)key]);
}

void bleSendUnlock() { Serial.println("[ble] (sim) unlock"); }

void bleSendVolume(uint8_t level, bool muted) {
  Serial.printf("[ble] (sim) volume %u%s\n", (unsigned)level, muted ? " muted" : "");
}
