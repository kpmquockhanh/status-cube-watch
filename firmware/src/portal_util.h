#pragma once
// Pure helpers for the setup portal. No Arduino or WiFi includes, so the host
// tests in sim/tests can build this with a plain compiler.
#include <string>

// The portal's 60 s "nobody joined, reboot and retry WiFi" countdown must not
// cut a Bluetooth pairing short or drop a live Mac link.
inline bool portalIdleRebootDue(bool autoRetry, bool bleBusy, unsigned long idleMs, unsigned long limitMs) {
  return autoRetry && !bleBusy && idleMs > limitMs;
}

// Reboot into the normal boot flow only when a Mac becomes bonded while the
// portal is up. An already-bonded cube reaching the portal (boot-time hold) must
// stay there so it can use "Forget paired Mac".
inline bool portalBondedTransition(bool bondedAtStart, bool bondedNow) { return !bondedAtStart && bondedNow; }

// SSIDs are 1..32 bytes. WPA passphrases are 8..63, but an open network has
// none, so the password is only length-checked where it is stored.
inline bool portalValidSsid(const std::string &s) { return !s.empty() && s.size() <= 32; }

// WPA passphrases are 8..63 characters; WiFi.begin rejects anything shorter, so
// storing one would leave the cube retrying a join that cannot succeed. Empty
// means an open network.
inline bool portalValidWifiPassword(const std::string &p) {
  return p.empty() || (p.size() >= 8 && p.size() <= 63);
}

// The bridge speaks plain HTTP on the LAN (see net.cpp). Anything else would
// only surface later as a confusing "bad BRIDGE_URL" on the display.
inline bool portalValidBridgeUrl(const std::string &u) {
  static const std::string scheme = "http://";
  if (u.size() > 127 || u.compare(0, scheme.size(), scheme) != 0) return false;
  if (u.find_first_of(" \t\r\n") != std::string::npos) return false;
  const std::string rest = u.substr(scheme.size());
  return !rest.empty() && rest[0] != ':' && rest[0] != '/';
}

// Values go back into the form as value="...", so a quote in an SSID must not
// end the attribute and a "<" must not start a tag.
inline std::string portalHtmlEscape(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

inline std::string portalTrim(const std::string &s) {
  const char *ws = " \t\r\n";
  const size_t a = s.find_first_not_of(ws);
  if (a == std::string::npos) return std::string();
  return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

// A blank password field means "keep what is stored", but only while the SSID
// is unchanged. Pointing the cube at a different network with a blank
// password means an open network, not the old network's password.
inline std::string portalResolvePassword(const std::string &oldSsid, const std::string &newSsid,
                                         const std::string &oldPass, const std::string &newPass) {
  if (!newPass.empty()) return newPass;
  return oldSsid == newSsid ? oldPass : std::string();
}

inline std::string portalKeepIfBlank(const std::string &oldValue, const std::string &newValue) {
  return newValue.empty() ? oldValue : newValue;
}

// Parses a whole-string decimal integer in [lo, hi]. A blank field keeps
// `keep`; anything else (letters, trailing junk, out of range) is rejected.
inline bool portalParseInt(const std::string &raw, int lo, int hi, int keep, int &out) {
  const std::string t = portalTrim(raw);
  if (t.empty()) { out = keep; return true; }
  if (t.size() > 4) return false;
  int v = 0;
  for (char c : t) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  if (v < lo || v > hi) return false;
  out = v;
  return true;
}
