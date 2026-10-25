#pragma once
#include <stddef.h>
#include <stdint.h>

#include "ble_bonds.h"
#include "nav_frame.h"
#include "payload.h"
#include "volume_frame.h"

// The BLE side of the cube: a NimBLE GATT server the Mac app writes payloads
// to and the iPhone writes navigation frames to, both bonded and connected at
// once (see docs/ble-protocol.md). net_ble.cpp is the hardware implementation;
// sim/ble_sim.cpp and sim/bench.cpp are the stand-ins.

enum class BleState : uint8_t {
  Off,
  Advertising,  // no link up
  Pairing,      // a device that may be accepted is pairing: blePasskey() is the code to type
  Connected,    // at least one link up
};

void bleBegin();  // once, at boot, before anything asks bleBonded()
BleState bleState();
uint32_t blePasskey();  // the 6-digit code while Pairing, else 0
bool bleBonded();       // any bond (the first-boot "waiting for a Mac" screen)
bool bleMacBonded();    // a bond known to be a Mac (Volume card, transport policy)
bool blePhoneBonded();  // a bond known to be an iPhone
bool bleUnknownBonded();  // a bond that never said what it is (no write yet)
// Forgets the bonds of exactly that kind (Unknown = only the unidentified
// ones). The caller reboots.
void bleForget(BondKind k);
// "Pair new device": sets NVS ble/pw so the next boot opens the pairing window
// (BLE_PAIR_WINDOW_MS) for a device beside the bonded ones. The caller reboots.
void bleRequestPairingAtBoot();
bool blePairingOpen();  // that window is running
// True when a complete, valid navigation frame arrived since the last call; it
// is copied to `out` (about 4 KB: keep `out` static, not on a small stack).
// Main loop only.
bool bleTakeNav(NavScene &out);
// True when a complete, valid payload arrived since the last call; it is
// copied to `out`. `out` is untouched otherwise, so the display keeps the last
// good data. Call it every loop pass: it also does the BLE housekeeping that
// must run off the NimBLE task (dropping a link that never secured itself).
bool bleTake(Payload &out);
uint32_t bleLastGood();  // millis() of the last valid payload (Payload writes only), 0 = never

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
// Answers a Claude prompt on the Mac: Control `09` (fw_rev 10, answer_frame.h), to the Mac's
// link only. False, with a `[ble] answer dropped: <reason>` line, when the ids or key are
// not encodable, no Mac link is subscribed to Control, or the message does not fit that
// link's MTU - 3. True means queued to the radio, not that the Mac acted. No retry.
bool bleSendAnswer(const char *agent, const char *prompt, char key);
// Tells the Mac which agent the detail screen shows: Control `0A` (fw_rev 11, detail_frame.h),
// nullptr or "" = the screen closed. The Mac then polls the bridge with ?detail=<agent> and the
// payload's Agents card carries `d`. Mac link only; false, with a `[ble] detail dropped: <reason>`
// line, on a bad id, no Mac subscribed to Control, or past the link's MTU - 3. No retry here:
// DetailView::sendDue paces the resend.
bool bleSendDetail(const char *agent);
// Sets the agent's folder hat: Control `0B` (fw_rev 11, detail_frame.h), `hat` 0..103 (shape * 8
// + colour), HAT_NONE or HAT_AUTO. Mac link only; false, with a `[ble] hat dropped: <reason>` line,
// as for bleSendDetail. Fire and forget: the next payload's tile `h` shows the result.
bool bleSendHat(const char *agent, uint8_t hat);
// Tells the iPhone where the dragged map is: Control `08 <x> <y>` (fw_rev 9, nav_pan.h).
// Fire and forget; the Mac ignores it.
void bleSendNavPan(int16_t x, int16_t y);
