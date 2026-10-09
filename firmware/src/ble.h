#pragma once
#include <stddef.h>
#include <stdint.h>

#include "payload.h"
#include "volume_frame.h"

// The BLE side of the cube: a NimBLE GATT server the Mac app writes payloads
// to (see docs/ble-protocol.md). net_ble.cpp is the hardware implementation;
// sim/ble_sim.cpp is the desktop stand-in.

enum class BleState : uint8_t {
  Off,
  Advertising,  // waiting for a Mac
  Pairing,      // a Mac is pairing: blePasskey() is the code to type
  Connected,
};

void bleBegin();  // once, at boot, before anything asks bleBonded()
BleState bleState();
uint32_t blePasskey();  // the 6-digit code while Pairing, else 0
bool bleBonded();       // a Mac is bonded
// True when a complete, valid payload arrived since the last call; it is
// copied to `out`. `out` is untouched otherwise, so the display keeps the last
// good data. Call it every loop pass: it also does the BLE housekeeping that
// must run off the NimBLE task (dropping a link that never secured itself).
bool bleTake(Payload &out);
uint32_t bleLastGood();  // millis() of the last valid payload, 0 = never
void bleForgetBonds();   // forget the bonded Mac (the caller reboots)

// Settings the Mac writes over BLE (settings_json.h). True when a JSON object
// arrived since the last call; it is copied to `out` (NUL-terminated, at most
// `cap` bytes). The main loop applies it, then calls bleSettingsReply().
bool bleTakeSettings(char *out, size_t cap);
// Tells the Mac how the write went (a SettingsResult) and refreshes what a
// read of the Settings characteristic returns.
void bleSettingsReply(uint8_t result);
// Settings were saved on the cube itself (an on-device editor): refreshes what
// a read of the Settings characteristic returns. Main loop only.
void bleSettingsChanged();

// Tells the Mac a Pomodoro phase ended: Control `04 <ended> <next>` with the
// PomoPhase values (0 focus, 1 short, 2 long). Fire and forget; dropped when no
// Mac is subscribed.
void bleNotifyPomodoro(uint8_t ended, uint8_t next);

// The Mac's output volume (Volume characteristic, fw_rev 5). True when it
// changed since the last call, including the change to unknown (known=false)
// when the link drops. Main loop only.
bool bleTakeVolume(MacVolume &out);
// Asks the Mac to set its output: Control `05 <level> <muted>`. Fire and forget.
void bleSendVolume(uint8_t level, bool muted);
// Asks the Mac to press a media key: Control `06 <key>` (fw_rev 6). Fire and forget.
void bleSendMedia(MediaKey key);
// Asks the Mac to unlock its screen: Control `07` (fw_rev 7). Fire and forget;
// the Mac acts only while it is locked and "Unlock with cube" is on.
void bleSendUnlock();
