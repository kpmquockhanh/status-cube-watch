#pragma once
#include <stdint.h>

// Whether a BLE link may stay up once pairing (or re-encryption with a stored
// bond) has finished. Pure, so the host tests in sim/tests can pin it;
// net_ble.cpp applies it in onAuthenticationComplete.
enum class BleAuthVerdict : uint8_t {
  Accept,
  Refuse,          // disconnect
  RefuseDropBond,  // delete the bond this pairing just stored, then disconnect
};

// `bondsAtConnect` / `bondsNow`: bonds stored when the link came up and now;
// more now means this pairing stored one.
inline BleAuthVerdict bleAuthVerdict(bool encrypted, bool authenticated, bool bonded, int bondsAtConnect,
                                     int bondsNow) {
  if (!encrypted) return BleAuthVerdict::Refuse;  // wrong passkey or refused
  const bool newBond = bondsNow > bondsAtConnect;
  const BleAuthVerdict refuse = newBond ? BleAuthVerdict::RefuseDropBond : BleAuthVerdict::Refuse;
  // NimBLE does not enforce the MITM we ask for: a central with no input still
  // gets a Just Works link. Only a passkey pairing counts.
  if (!authenticated) return refuse;
  // One bonded Mac at a time: with a bond stored, only that bond gets in, not a
  // second device that just bonded nor one that paired without bonding.
  if (bondsAtConnect > 0 && (newBond || !bonded)) return refuse;
  return BleAuthVerdict::Accept;
}

// A link must be secured (onAuthenticationComplete accepted it) this soon after
// it comes up, or it is dropped: NimBLE stops advertising while a central is
// connected, so one that never pairs would keep the Mac out until a reboot.
// Long enough to read the six digits and type them on the Mac.
constexpr uint32_t BLE_SECURE_WITHIN_MS = 60000;

inline bool bleUnsecuredExpired(bool linkUp, bool secured, uint32_t connectedAt, uint32_t now) {
  return linkUp && !secured && now - connectedAt >= BLE_SECURE_WITHIN_MS;  // unsigned: wrap-safe
}
