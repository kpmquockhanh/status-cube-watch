#pragma once
#include <stdint.h>

// Who may stay bonded and connected: one Mac and one iPhone, both at once. Pure,
// so the host tests in sim/tests can pin it; net_ble.cpp applies it.

// What a bond is, learned from the first write after the link is secured: Nav
// means a Phone; Payload, Settings or Volume mean a Mac. Stored in NVS (`ble`
// namespace, key = the identity address as 12 hex digits, value `m`/`p`).
enum class BondKind : uint8_t { Unknown, Mac, Phone };

// Whether a BLE link may stay up once pairing (or re-encryption with a stored
// bond) has finished; net_ble.cpp applies it in onAuthenticationComplete.
enum class BleAuthVerdict : uint8_t {
  Accept,
  Refuse,          // disconnect
  RefuseDropBond,  // delete the bond this pairing just stored, then disconnect
};

// How long "Pair new device" (portal) keeps pairing open after the reboot.
constexpr uint32_t BLE_PAIR_WINDOW_MS = 120000;

// `newBond`: this pairing stored a bond that was not there when the link came
// up. `bondsBefore`: bonds stored when the link came up. `pairingOpen`: the
// window after "Pair new device" is running.
inline BleAuthVerdict bleAuthVerdict(bool encrypted, bool authenticated, bool bonded, bool newBond,
                                     int bondsBefore, bool pairingOpen) {
  if (!encrypted) return BleAuthVerdict::Refuse;  // wrong passkey or refused
  const BleAuthVerdict refuse = newBond ? BleAuthVerdict::RefuseDropBond : BleAuthVerdict::Refuse;
  // NimBLE does not enforce the MITM we ask for: a central with no input still
  // gets a Just Works link. Only a passkey pairing counts.
  if (!authenticated) return refuse;
  // A stored (passkey-made) bond coming back.
  if (bonded && !newBond) return BleAuthVerdict::Accept;
  // Anything new: the first device always, a further one only in the window.
  // A third device pairing outside it is refused and its bond deleted; the
  // bonds already stored are untouched.
  if (bondsBefore == 0 || pairingOpen) return BleAuthVerdict::Accept;
  return refuse;
}

// Index of the bond a newly identified bond of `kinds[newIndex]` replaces, or
// -1: another bond of the same, known kind, the oldest if there are several
// (`ages`: larger = older). An Unknown kind never replaces or is replaced.
inline int bleBondToEvict(const BondKind *kinds, const uint32_t *ages, int n, int newIndex) {
  if (newIndex < 0 || newIndex >= n || kinds[newIndex] == BondKind::Unknown) return -1;
  int best = -1;
  for (int i = 0; i < n; i++) {
    if (i == newIndex || kinds[i] != kinds[newIndex]) continue;
    if (best < 0 || ages[i] > ages[best]) best = i;
  }
  return best;
}

// The window runs for BLE_PAIR_WINDOW_MS from `openedAt` (millis() when BLE
// came up after a "Pair new device" reboot). Unsigned: wrap-safe.
inline bool blePairWindowOpen(bool requestedAtBoot, uint32_t openedAt, uint32_t now) {
  return requestedAtBoot && now - openedAt < BLE_PAIR_WINDOW_MS;
}

// Advertise while there is room for the Mac and the phone; during the window,
// for one link more, so a replacement can pair while the old device is still
// connected (it is dropped once the new one's kind is known).
inline bool bleShouldAdvertise(int links, bool pairingOpen) { return links < (pairingOpen ? 3 : 2); }

// A link must be secured (onAuthenticationComplete accepted it) this soon after
// it comes up, or it is dropped: an unsecured central would hold one of the
// cube's links. Long enough to read the six digits and type them.
constexpr uint32_t BLE_SECURE_WITHIN_MS = 60000;

inline bool bleUnsecuredExpired(bool linkUp, bool secured, uint32_t connectedAt, uint32_t now) {
  return linkUp && !secured && now - connectedAt >= BLE_SECURE_WITHIN_MS;  // unsigned: wrap-safe
}
