#include "ble_auth.h"
#include "check.h"

namespace {

using V = BleAuthVerdict;

// args: encrypted, authenticated, bonded, bonds at connect, bonds now
void testFirstMac() {
  CHECK(bleAuthVerdict(true, true, true, 0, 1) == V::Accept);    // passkey pairing, bonded
  CHECK(bleAuthVerdict(true, true, false, 0, 0) == V::Accept);   // passkey pairing, no bond asked for
}

void testPairingFailed() {
  CHECK(bleAuthVerdict(false, false, false, 0, 0) == V::Refuse);  // wrong passkey or refused
  CHECK(bleAuthVerdict(false, false, false, 1, 1) == V::Refuse);
}

// A central with no input gets Just Works: encrypted but not authenticated.
// It must not count as the bonded Mac, and the bond it stored must go.
void testJustWorksRefused() {
  CHECK(bleAuthVerdict(true, false, true, 0, 1) == V::RefuseDropBond);
  CHECK(bleAuthVerdict(true, false, false, 0, 0) == V::Refuse);
  CHECK(bleAuthVerdict(true, false, true, 1, 2) == V::RefuseDropBond);
  CHECK(bleAuthVerdict(true, false, false, 1, 1) == V::Refuse);
}

// The bonded Mac coming back re-encrypts with its stored, passkey-made bond.
void testBondedMacReconnects() { CHECK(bleAuthVerdict(true, true, true, 1, 1) == V::Accept); }

// One bond at a time: with a bond stored, nobody else gets in.
void testSecondDeviceRefused() {
  CHECK(bleAuthVerdict(true, true, true, 1, 2) == V::RefuseDropBond);  // bonded alongside
  CHECK(bleAuthVerdict(true, true, false, 1, 1) == V::Refuse);         // paired without bonding
}

// A central that connects and never secures the link would hold the cube's one
// connection, with advertising stopped, so the Mac could not get in.
void testUnsecuredLinkExpires() {
  const uint32_t t0 = 5000;
  CHECK(!bleUnsecuredExpired(false, false, t0, t0 + BLE_SECURE_WITHIN_MS));  // no link
  CHECK(!bleUnsecuredExpired(true, false, t0, t0 + BLE_SECURE_WITHIN_MS - 1));  // still pairing
  CHECK(bleUnsecuredExpired(true, false, t0, t0 + BLE_SECURE_WITHIN_MS));
  CHECK(!bleUnsecuredExpired(true, true, t0, t0 + 10 * BLE_SECURE_WITHIN_MS));  // the Mac stays
  CHECK(bleUnsecuredExpired(true, false, 0xFFFFFF00u, 0xFFFFFF00u + BLE_SECURE_WITHIN_MS));  // millis() wrap
  CHECK(BLE_SECURE_WITHIN_MS >= 30000);  // time to read six digits and type them on the Mac
}

}  // namespace

int main() {
  testFirstMac();
  testPairingFailed();
  testJustWorksRefused();
  testBondedMacReconnects();
  testSecondDeviceRefused();
  testUnsecuredLinkExpires();
  return checksDone("ble_auth_test");
}
