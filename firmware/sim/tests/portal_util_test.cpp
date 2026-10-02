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

void testIdleReboot() {
  CHECK(portalIdleRebootDue(true, false, 60001, 60000));
  CHECK(!portalIdleRebootDue(true, false, 60000, 60000));
  CHECK(!portalIdleRebootDue(true, true, 600000, 60000));  // pairing or connected
  CHECK(!portalIdleRebootDue(false, false, 600000, 60000));
}

}  // namespace

int main() {
  testIdleReboot();
  testBondTransition();
  testSsid();
  testBridgeUrl();
  testEscape();
  testTrim();
  testPassword();
  testKeepIfBlank();
  return checksDone("portal_util_test");
}
