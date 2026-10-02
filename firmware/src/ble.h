#pragma once
#include <stdint.h>

#include "payload.h"

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
// good data.
bool bleTake(Payload &out);
uint32_t bleLastGood();  // millis() of the last valid payload, 0 = never
void bleForgetBonds();   // forget the bonded Mac (the caller reboots)
