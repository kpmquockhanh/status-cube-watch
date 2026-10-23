#include "check.h"
#include "transport_policy.h"

namespace {

// bonded, wifi configured, no keepWifi: the common case.
TransportDecision upd(TransportPolicy &p, uint32_t now, uint32_t bleGood) {
  return p.update(now, bleGood, true, true, false);
}

void testNoWifiConfiguredNeverTurnsWifiOn() {
  TransportPolicy p;
  p.begin(0);
  for (uint32_t t = 0; t < 120000; t += 5000) {
    const TransportDecision d = p.update(t, 0, true, false, false);
    CHECK(!d.wifiOn);
    CHECK(!d.bleLive);
  }
}

void testNoBondMeansWifiAtOnce() {
  TransportPolicy p;
  p.begin(1000);
  CHECK(p.update(1000, 0, false, true, false).wifiOn);
}

void testNeverLiveUsesStart() {  // bonded, BLE has never delivered: 15 s grace from boot, then WiFi
  TransportPolicy p;
  p.begin(1000);
  CHECK(!upd(p, 1000, 0).wifiOn);
  CHECK(!upd(p, 15999, 0).wifiOn);
  CHECK(upd(p, 16000, 0).wifiOn);
}

void testLiveKeepsWifiOff() {
  TransportPolicy p;
  p.begin(0);
  for (uint32_t t = 5000; t <= 60000; t += 5000) {
    const TransportDecision d = upd(p, t, t);  // a payload every 5 s
    CHECK(d.bleLive);
    CHECK(!d.wifiOn);
  }
}

void testStaleStartsWifiAfter15s() {
  TransportPolicy p;
  p.begin(0);
  CHECK(upd(p, 2000, 2000).bleLive);
  CHECK(upd(p, 16999, 2000).bleLive);   // 14999 ms old
  CHECK(!upd(p, 16999, 2000).wifiOn);
  const TransportDecision d = upd(p, 17000, 2000);  // 15000 ms old
  CHECK(!d.bleLive);
  CHECK(d.wifiOn);
}

void testWifiDropsOnlyAfterBleLive30s() {
  TransportPolicy p;
  p.begin(0);
  CHECK(upd(p, 20000, 0).wifiOn);  // BLE never came: WiFi on
  for (uint32_t t = 20000; t < 50000; t += 5000) CHECK(upd(p, t, t).wifiOn);  // live since 20000, not yet 30 s
  CHECK(upd(p, 45000, 45000).wifiOn);
  CHECK(!upd(p, 50000, 50000).wifiOn);  // 30 s of continuous BLE
}

void testFlappingBleDoesNotDropWifi() {
  TransportPolicy p;
  p.begin(0);
  CHECK(upd(p, 20000, 0).wifiOn);
  CHECK(upd(p, 20000, 20000).wifiOn);   // live again
  CHECK(upd(p, 35000, 20000).wifiOn);   // stale again (15 s): liveSince must reset
  CHECK(upd(p, 36000, 36000).wifiOn);   // live
  CHECK(upd(p, 60000, 60000).wifiOn);   // only 24 s since this live run began
  CHECK(!upd(p, 66000, 66000).wifiOn);  // 30 s
}

void testKeepWifiAlwaysOn() {
  TransportPolicy p;
  p.begin(0);
  for (uint32_t t = 0; t <= 60000; t += 5000) CHECK(p.update(t, t ? t : 0, true, true, true).wifiOn);
}

void testMillisWrap() {  // millis() rolls over after 49 days
  TransportPolicy p;
  p.begin(0xFFFFF000u);
  CHECK(upd(p, 0xFFFFFF00u, 0xFFFFFF00u).bleLive);
  CHECK(upd(p, 0x00000100u, 0xFFFFFF00u).bleLive);          // 512 ms old across the wrap
  CHECK(!upd(p, 0x00000100u, 0xFFFFFF00u).wifiOn);
  CHECK(upd(p, 0x00004000u, 0xFFFFFF00u).wifiOn);           // ~16.6 s old across the wrap
}

void testBootDecisions() {
  CHECK(portalOnJoinFail(false));   // no bond: the portal is the only way to fix WiFi
  CHECK(!portalOnJoinFail(true));   // bonded: a failed join is not an emergency
  CHECK(pairWaitAtBoot(false, false));
  CHECK(!pairWaitAtBoot(false, true));
  CHECK(!pairWaitAtBoot(true, false));
}

// A ride keeps WiFi off: the Mac away (BLE stale) would otherwise bring it up.
void testRideKeepsWifiOff() {
  TransportPolicy p;
  p.begin(0);
  for (uint32_t t = 0; t <= 600000; t += 1000) CHECK(!p.update(t, 0, true, true, false, true).wifiOn);
  // The ride ends: the normal rule (BLE stale for 15 s) applies at once.
  CHECK(p.update(601000, 0, true, true, false, false).wifiOn);
}

void testRideTurnsWifiOffWhenItStarts() {
  TransportPolicy p;
  p.begin(0);
  CHECK(p.update(20000, 0, true, true, false).wifiOn);  // Mac away: WiFi up
  CHECK(!p.update(21000, 0, true, true, false, true).wifiOn);  // ride starts: off
  CHECK(p.update(60000, 0, true, true, false, false).wifiOn);  // ride over
}

void testRidePhoneOnly() {  // no Mac bond: WiFi on until a ride starts, off during it, on after
  TransportPolicy p;
  p.begin(0);
  CHECK(p.update(0, 0, false, true, false).wifiOn);
  CHECK(!p.update(1000, 0, false, true, false, true).wifiOn);
  CHECK(!p.update(300000, 0, false, true, false, true).wifiOn);
  CHECK(p.update(301000, 0, false, true, false, false).wifiOn);
}

void testRideWithWifiAlwaysOn() {  // WIFI_ALWAYS_ON wins over a ride
  TransportPolicy p;
  p.begin(0);
  CHECK(p.update(0, 0, true, true, true, true).wifiOn);
  CHECK(p.update(60000, 0, false, true, true, true).wifiOn);
  CHECK(!p.update(60000, 0, false, false, true, true).wifiOn);  // still nothing to join
}

void testBootWifiPlan() {
  // No bond, a network: join at boot, and the portal if the join fails.
  BootWifiPlan b = bootWifiPlan(false, false, false, true);
  CHECK(b.join && b.portalOnFail);
  // No bond, no network: nothing to join (the "waiting for a Mac" screen).
  b = bootWifiPlan(false, false, false, false);
  CHECK(!b.join && !b.portalOnFail);
  // A Mac: no boot join; the policy decides later.
  b = bootWifiPlan(true, true, false, true);
  CHECK(!b.join && !b.portalOnFail);
  // An iPhone and no Mac: no boot join, never the portal.
  b = bootWifiPlan(true, false, true, true);
  CHECK(!b.join && !b.portalOnFail);
  b = bootWifiPlan(true, false, true, false);
  CHECK(!b.join && !b.portalOnFail);
  // Both.
  b = bootWifiPlan(true, true, true, true);
  CHECK(!b.join && !b.portalOnFail);
  // Only a bond that never said what it is: join, but a failed join is no reason for the portal.
  b = bootWifiPlan(true, false, false, true);
  CHECK(b.join && !b.portalOnFail);
}

}  // namespace

int main() {
  testRideKeepsWifiOff();
  testRideTurnsWifiOffWhenItStarts();
  testRidePhoneOnly();
  testRideWithWifiAlwaysOn();
  testBootWifiPlan();
  testNoWifiConfiguredNeverTurnsWifiOn();
  testNoBondMeansWifiAtOnce();
  testNeverLiveUsesStart();
  testLiveKeepsWifiOff();
  testStaleStartsWifiAfter15s();
  testWifiDropsOnlyAfterBleLive30s();
  testFlappingBleDoesNotDropWifi();
  testKeepWifiAlwaysOn();
  testMillisWrap();
  testBootDecisions();
  return checksDone("transport_policy_test");
}
