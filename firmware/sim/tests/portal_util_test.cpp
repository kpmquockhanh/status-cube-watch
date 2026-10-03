#include <string>

#include "check.h"
#include "portal_util.h"

namespace {

void testSsid() {
  CHECK(!portalValidSsid(""));
  CHECK(portalValidSsid("a"));
  CHECK(portalValidSsid(std::string(32, 'x')));
  CHECK(!portalValidSsid(std::string(33, 'x')));
  // Final review I5: WiFi.begin refuses a passphrase under 8 characters.
  CHECK(portalValidWifiPassword(""));  // open network
  CHECK(!portalValidWifiPassword("1234567"));
  CHECK(portalValidWifiPassword("12345678"));
  CHECK(portalValidWifiPassword(std::string(63, 'x')));
  CHECK(!portalValidWifiPassword(std::string(64, 'x')));
}

void testBridgeUrl() {
  CHECK(portalValidBridgeUrl("http://192.168.1.50:8787/api/status"));
  CHECK(portalValidBridgeUrl("http://host"));
  CHECK(portalValidBridgeUrl("http://" + std::string(120, 'a')));   // 127 bytes
  CHECK(!portalValidBridgeUrl("http://" + std::string(121, 'a')));  // 128 bytes
  CHECK(!portalValidBridgeUrl(""));
  CHECK(!portalValidBridgeUrl("http://"));
  CHECK(!portalValidBridgeUrl("http://:8787/x"));
  CHECK(!portalValidBridgeUrl("http:///x"));
  CHECK(!portalValidBridgeUrl("https://host/x"));  // the bridge is plain HTTP
  CHECK(!portalValidBridgeUrl("ftp://host"));
  CHECK(!portalValidBridgeUrl("http://bad host/"));
  CHECK(!portalValidBridgeUrl("http://host/\n"));
  CHECK(!portalValidBridgeUrl("192.168.1.50:8787"));
}

void testEscape() {
  CHECK(portalHtmlEscape("plain") == "plain");
  CHECK(portalHtmlEscape("a\"b") == "a&quot;b");
  CHECK(portalHtmlEscape("<script>") == "&lt;script&gt;");
  CHECK(portalHtmlEscape("Tom & Jerry's") == "Tom &amp; Jerry&#39;s");
  CHECK(portalHtmlEscape("\"><img src=x>") == "&quot;&gt;&lt;img src=x&gt;");
  CHECK(portalHtmlEscape("") == "");
}

void testTrim() {
  CHECK(portalTrim("  http://h/ \r\n") == "http://h/");
  CHECK(portalTrim("x") == "x");
  CHECK(portalTrim("   ") == "");
  CHECK(portalTrim("") == "");
  CHECK(portalTrim(" a b ") == "a b");
}

void testPassword() {
  CHECK(portalResolvePassword("home", "home", "old", "new") == "new");
  CHECK(portalResolvePassword("home", "other", "old", "new") == "new");
  CHECK(portalResolvePassword("home", "home", "old", "") == "old");
  CHECK(portalResolvePassword("home", "cafe", "old", "") == "");
  CHECK(portalResolvePassword("", "home", "", "") == "");
}

void testKeepIfBlank() {
  CHECK(portalKeepIfBlank("old", "") == "old");
  CHECK(portalKeepIfBlank("old", "new") == "new");
  CHECK(portalKeepIfBlank("", "") == "");
}

void testBondTransition() {
  CHECK(portalBondedTransition(false, true));
  CHECK(!portalBondedTransition(true, true));  // already paired: stay in the portal
  CHECK(!portalBondedTransition(false, false));
  CHECK(!portalBondedTransition(true, false));
}

// The station side of the AP+STA portal: bounded join attempts, only while
// nobody is on the AP and no Mac is pairing.
void testStaFirstAttempt() {
  PortalSta now;
  now.begin(1000, true);  // asked-for portal: try at once
  CHECK(now.update(1000, true, true, false) == PortalStaAction::Begin);
  PortalSta later;
  later.begin(1000, false);  // WiFi just failed: wait a full period first
  CHECK(later.update(1000, true, true, false) == PortalStaAction::None);
  CHECK(later.update(1000 + PortalSta::EVERY_MS - 1, true, true, false) == PortalStaAction::None);
  CHECK(later.update(1000 + PortalSta::EVERY_MS, true, true, false) == PortalStaAction::Begin);
}

void testStaAttemptIsBounded() {
  PortalSta s;
  s.begin(0, true);
  CHECK(s.update(0, true, true, false) == PortalStaAction::Begin);
  CHECK(s.update(PortalSta::ATTEMPT_MS - 1, true, true, false) == PortalStaAction::None);
  CHECK(s.update(PortalSta::ATTEMPT_MS, true, true, false) == PortalStaAction::Stop);
  // Parked until the next period, counted from the start of the last attempt.
  CHECK(s.update(PortalSta::EVERY_MS - 1, true, true, false) == PortalStaAction::None);
  CHECK(s.update(PortalSta::EVERY_MS, true, true, false) == PortalStaAction::Begin);
}

void testStaParksWhenBusy() {
  PortalSta s;
  s.begin(0, true);
  CHECK(s.update(0, true, false, false) == PortalStaAction::None);  // a phone or a Mac: no attempt
  CHECK(s.update(500, true, true, false) == PortalStaAction::Begin);
  CHECK(s.update(600, true, false, false) == PortalStaAction::Stop);  // a phone joins mid-attempt
  CHECK(s.update(700, true, false, false) == PortalStaAction::None);
}

void testStaNoSsid() {
  PortalSta s;
  s.begin(0, true);
  CHECK(s.update(0, false, true, false) == PortalStaAction::None);
  CHECK(s.update(10 * PortalSta::EVERY_MS, false, true, false) == PortalStaAction::None);
}

void testStaConnectedStays() {
  PortalSta s;
  s.begin(0, true);
  CHECK(s.update(0, true, true, false) == PortalStaAction::Begin);
  // Joined inside the window: never stopped, even past it or with a phone on.
  CHECK(s.update(3000, true, true, true) == PortalStaAction::None);
  CHECK(s.update(PortalSta::ATTEMPT_MS + 1, true, false, true) == PortalStaAction::None);
  // The link drops with nobody around: the next attempt starts at once.
  CHECK(s.update(PortalSta::ATTEMPT_MS + 2, true, true, false) == PortalStaAction::Begin);
}

// With AP+STA the form is reachable from the LAN, so a request must name the
// cube by the address it reached it on (no DNS rebinding) and carry the token
// that only the served form contains (no cross-site POST).
void testHost() {
  CHECK(portalHostIs("192.168.71.1", "192.168.71.1"));
  CHECK(portalHostIs("192.168.71.1:80", "192.168.71.1"));
  CHECK(!portalHostIs("evil.example", "192.168.71.1"));
  CHECK(!portalHostIs("192.168.71.10", "192.168.71.1"));
  CHECK(!portalHostIs("", "192.168.71.1"));
  CHECK(!portalHostIs("192.168.71.1:8080", "192.168.71.1"));
}

void testToken() {
  CHECK(portalTokenOk("a1b2c3d4", "a1b2c3d4"));
  CHECK(!portalTokenOk("", "a1b2c3d4"));
  CHECK(!portalTokenOk("a1b2c3d5", "a1b2c3d4"));
  CHECK(!portalTokenOk("", ""));  // no token made yet: nothing is trusted
}

void testRecoveredReboot() {
  CHECK(portalStaRecoveredReboot(true, true, false, false));
  CHECK(!portalStaRecoveredReboot(false, true, false, false));  // asked-for portal stays up
  CHECK(!portalStaRecoveredReboot(true, false, false, false));
  CHECK(!portalStaRecoveredReboot(true, true, true, false));  // a phone is on the form
  CHECK(!portalStaRecoveredReboot(true, true, false, true));  // a Mac is pairing
}

void testOtaPassword() {
  CHECK(portalOtaPassword("old", "", false) == "old");  // blank keeps
  CHECK(portalOtaPassword("old", "new", false) == "new");
  CHECK(portalOtaPassword("old", "", true).empty());  // the clear box wins
  CHECK(portalOtaPassword("old", "new", true).empty());
}

}  // namespace

void testParseInt() {
  int v = -1;
  CHECK(portalParseInt("42", 1, 99, 7, v) && v == 42);
  CHECK(portalParseInt("  ", 1, 99, 7, v) && v == 7);  // blank keeps
  CHECK(portalParseInt("0", 0, 9, 5, v) && v == 0);
  CHECK(!portalParseInt("100", 1, 99, 7, v));
  CHECK(!portalParseInt("0", 1, 99, 7, v));
  CHECK(!portalParseInt("1x", 1, 99, 7, v));
  CHECK(!portalParseInt("-3", 0, 99, 7, v));
  CHECK(!portalParseInt("12345", 0, 99999, 7, v));
}

int main() {
  testStaFirstAttempt();
  testStaAttemptIsBounded();
  testStaParksWhenBusy();
  testStaNoSsid();
  testStaConnectedStays();
  testHost();
  testToken();
  testRecoveredReboot();
  testOtaPassword();
  testBondTransition();
  testSsid();
  testBridgeUrl();
  testEscape();
  testTrim();
  testPassword();
  testKeepIfBlank();
  testParseInt();
  return checksDone("portal_util_test");
}
