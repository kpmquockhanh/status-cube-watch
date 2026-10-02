// Stand-in for net_ble.cpp in the `-noble` env (-DCUBE_NO_BLE): BLE is never
// initialised, so the cube never has a bond and runs WiFi polling only.
#ifdef CUBE_NO_BLE
#include "ble.h"

void bleBegin() {}
BleState bleState() { return BleState::Off; }
uint32_t blePasskey() { return 0; }
bool bleBonded() { return false; }
bool bleTake(Payload &) { return false; }
uint32_t bleLastGood() { return 0; }
void bleForgetBonds() {}
#endif  // CUBE_NO_BLE
