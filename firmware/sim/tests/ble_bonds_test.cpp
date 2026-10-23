#include "ble_bonds.h"
#include "check.h"

namespace {

using V = BleAuthVerdict;
using K = BondKind;

// args: encrypted, authenticated, bonded, newBond, bonds before this pairing, pairing window open
void testFirstDevice() {
  CHECK(bleAuthVerdict(true, true, true, true, 0, false) == V::Accept);    // passkey pairing, bonded
  CHECK(bleAuthVerdict(true, true, false, false, 0, false) == V::Accept);  // passkey pairing, no bond asked for
}

void testPairingFailed() {
  CHECK(bleAuthVerdict(false, false, false, false, 0, false) == V::Refuse);  // wrong passkey or refused
  CHECK(bleAuthVerdict(false, false, false, false, 1, true) == V::Refuse);
}

// A central with no input gets Just Works: encrypted but not authenticated. It
// never counts, window or not, and the bond it stored must go.
void testJustWorksRefused() {
  CHECK(bleAuthVerdict(true, false, true, true, 0, false) == V::RefuseDropBond);
  CHECK(bleAuthVerdict(true, false, false, false, 0, false) == V::Refuse);
  CHECK(bleAuthVerdict(true, false, true, true, 1, true) == V::RefuseDropBond);
  CHECK(bleAuthVerdict(true, false, false, false, 2, true) == V::Refuse);
}

// Review Focus 4: a third device pairing while the window is closed is refused
// and only its own new bond is deleted.
void testNewBondWindowClosed() {
  CHECK(bleAuthVerdict(true, true, true, true, 1, false) == V::RefuseDropBond);
  CHECK(bleAuthVerdict(true, true, true, true, 2, false) == V::RefuseDropBond);
}

void testNewBondWindowOpen() {
  CHECK(bleAuthVerdict(true, true, true, true, 1, true) == V::Accept);  // the phone beside the Mac
  CHECK(bleAuthVerdict(true, true, true, true, 2, true) == V::Accept);  // a replacement: evicted once its kind is known
}

// A stored bond re-encrypting is let in whatever the window says.
void testStoredBondReconnects() {
  CHECK(bleAuthVerdict(true, true, true, false, 1, false) == V::Accept);
  CHECK(bleAuthVerdict(true, true, true, false, 2, false) == V::Accept);
  CHECK(bleAuthVerdict(true, true, true, false, 2, true) == V::Accept);
}

void testUnbondedPairingWithBonds() {
  CHECK(bleAuthVerdict(true, true, false, false, 1, false) == V::Refuse);
  CHECK(bleAuthVerdict(true, true, false, false, 2, false) == V::Refuse);
}

void testEvict() {
  const uint32_t ages3[] = {30, 20, 10};
  const K a[] = {K::Mac, K::Phone, K::Phone};
  CHECK(bleBondToEvict(a, ages3, 3, 2) == 1);
  const K b[] = {K::Mac, K::Unknown};
  CHECK(bleBondToEvict(b, ages3, 2, 1) == -1);  // nothing known about the new one
  const K c[] = {K::Mac, K::Mac};
  CHECK(bleBondToEvict(c, ages3, 2, 1) == 0);
  CHECK(bleBondToEvict(c, ages3, 2, 0) == 1);  // whichever index is new, the other goes
  const K d[] = {K::Mac, K::Phone};
  CHECK(bleBondToEvict(d, ages3, 2, 1) == -1);  // one of each: nothing to replace
  const K e[] = {K::Unknown, K::Phone};
  CHECK(bleBondToEvict(e, ages3, 2, 1) == -1);  // an unknown bond is never taken for a Phone
  // Two older Phones (never expected, but deterministic): the oldest goes.
  const K f[] = {K::Phone, K::Phone, K::Phone};
  const uint32_t agesF[] = {5, 50, 1};
  CHECK(bleBondToEvict(f, agesF, 3, 2) == 1);
  CHECK(bleBondToEvict(c, ages3, 2, 5) == -1);  // newIndex out of range
  CHECK(bleBondToEvict(c, ages3, 2, -1) == -1);
}

// A device accepted as a new bond that drops before its first write would stay
// Unknown for good, so its bond is deleted when that link goes.
void testDropUnidentifiedOnDisconnect() {
  CHECK(bleDropBondOnDisconnect(true, true, K::Unknown));
  CHECK(!bleDropBondOnDisconnect(true, true, K::Mac));     // identified: kept
  CHECK(!bleDropBondOnDisconnect(true, true, K::Phone));
  CHECK(!bleDropBondOnDisconnect(true, false, K::Unknown));  // a stored bond reconnecting
  CHECK(!bleDropBondOnDisconnect(false, true, K::Unknown));  // never secured: nothing accepted
}

// The bond store is full (NimBLE overflow): only an Unknown bond may make room,
// never a known Mac or iPhone; with none, the write is refused.
void testStoreOverflowVictim() {
  const uint32_t ages[] = {30, 20, 10, 5};
  const bool none[] = {false, false, false, false};
  const K a[] = {K::Mac, K::Phone, K::Unknown};
  CHECK(bleStoreOverflowVictim(a, ages, none, 3, -1) == 2);
  const K b[] = {K::Mac, K::Phone, K::Mac};
  CHECK(bleStoreOverflowVictim(b, ages, none, 3, -1) == -1);  // refuse: Review Focus 4
  CHECK(bleStoreOverflowVictim(a, ages, none, 3, 2) == -1);    // the peer being written is not its own victim
  const K c[] = {K::Unknown, K::Mac, K::Unknown};
  CHECK(bleStoreOverflowVictim(c, ages, none, 3, -1) == 0);  // the oldest Unknown
  const bool conn0[] = {true, false, false};
  CHECK(bleStoreOverflowVictim(c, ages, conn0, 3, -1) == 2);  // one with no link first
  const bool both[] = {true, false, true};
  CHECK(bleStoreOverflowVictim(c, ages, both, 3, -1) == 0);  // all linked: still the oldest Unknown
  CHECK(bleStoreOverflowVictim(c, ages, none, 0, -1) == -1);
}

void testPairWindow() {
  CHECK(BLE_PAIR_WINDOW_MS == 120000);
  CHECK(!blePairWindowOpen(false, 0, 1000));  // not asked for at boot
  CHECK(blePairWindowOpen(true, 1000, 1000));
  CHECK(blePairWindowOpen(true, 1000, 1000 + BLE_PAIR_WINDOW_MS - 1));
  CHECK(!blePairWindowOpen(true, 1000, 1000 + BLE_PAIR_WINDOW_MS));
  CHECK(blePairWindowOpen(true, 0xFFFFF000u, 0xFFFFF000u + 60000));  // millis() wrap
}

// Advertising: with no bond while there is room; with bonds only while one of
// them is not connected; during the window for one link more (the replacement
// pairs before the old one is dropped).
void testAdvertise() {
  // No bond: the first device may come; a stranger holding a link does not shut it out.
  CHECK(bleShouldAdvertise(0, 0, 0, false));
  CHECK(bleShouldAdvertise(1, 0, 0, false));
  CHECK(!bleShouldAdvertise(2, 0, 0, false));
  // A Mac bonded and connected, no phone ever paired: nobody else can pair, so quiet.
  CHECK(!bleShouldAdvertise(1, 1, 1, false));
  CHECK(bleShouldAdvertise(0, 1, 0, false));  // the Mac is away: advertise for it
  // Mac and phone bonded.
  CHECK(bleShouldAdvertise(0, 2, 0, false));
  CHECK(bleShouldAdvertise(1, 2, 1, false));   // one of them is away
  CHECK(!bleShouldAdvertise(2, 2, 2, false));  // both up: stop
  CHECK(!bleShouldAdvertise(2, 2, 1, false));  // one up, one stranger pairing: no room
  CHECK(bleShouldAdvertise(1, 2, 0, false));   // a link still securing itself
  // The window: one more link, whatever the bonds.
  CHECK(bleShouldAdvertise(2, 2, 2, true));
  CHECK(bleShouldAdvertise(0, 0, 0, true));
  CHECK(!bleShouldAdvertise(3, 2, 2, true));
  // The window expires with both links up: advertising stops.
  CHECK(!bleShouldAdvertise(2, 2, 2, false));
}

// A passkey is shown only for a pairing that can be accepted.
void testShowPasskey() {
  CHECK(bleShowPasskey(0, false, false));   // the first device
  CHECK(!bleShowPasskey(1, false, false));  // a further device outside the window: refused, no code
  CHECK(!bleShowPasskey(2, false, false));
  CHECK(bleShowPasskey(2, false, true));    // in the window
  CHECK(bleShowPasskey(1, false, true));
  CHECK(bleShowPasskey(2, true, false));    // the bonded Mac pairing again (it forgot the cube)
  // Each code shown matches a pairing the verdict can accept.
  CHECK(bleAuthVerdict(true, true, true, true, 0, false) == BleAuthVerdict::Accept);
  CHECK(bleAuthVerdict(true, true, true, false, 2, false) == BleAuthVerdict::Accept);
  CHECK(bleAuthVerdict(true, true, true, true, 2, false) != BleAuthVerdict::Accept);
}

// A central that connects and never secures the link would hold one of the
// cube's connections, so it is dropped.
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
  testFirstDevice();
  testPairingFailed();
  testJustWorksRefused();
  testNewBondWindowClosed();
  testNewBondWindowOpen();
  testStoredBondReconnects();
  testUnbondedPairingWithBonds();
  testEvict();
  testDropUnidentifiedOnDisconnect();
  testStoreOverflowVictim();
  testPairWindow();
  testAdvertise();
  testShowPasskey();
  testUnsecuredLinkExpires();
  return checksDone("ble_bonds_test");
}
