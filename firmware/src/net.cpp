#include "net.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "settings.h"

namespace {
char g_error[64] = "";
// A join plus DHCP takes several seconds, and the core retries a dropped link
// by itself, so a poll that finds WiFi down must not restart the join. Only a
// link that has stayed down this long since the last start, nudge or poll that
// found it up gets a WiFi.reconnect().
constexpr uint32_t REJOIN_AFTER_MS = 30000;
uint32_t g_joinSince = 0;

void fail(const char *msg) { strlcpy(g_error, msg, sizeof(g_error)); }
}  // namespace

void netJoinSaved() {
  const Settings &s = settings();
  // A null passphrase is how the WiFi library spells "open network".
  WiFi.begin(s.ssid, s.pass[0] ? s.pass : nullptr);
  Serial.printf("[net] connecting to %s\n", s.ssid);
}

void netStart() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);  // the radio idles between 5s polls
  netJoinSaved();
  g_joinSince = millis();
}

void netStop() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[net] wifi off");
}

bool netBegin() {
  netStart();
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[net] %s  rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  fail("wifi timeout");
  Serial.println("[net] could not join");
  return false;
}

bool netOnline() { return WiFi.status() == WL_CONNECTED; }

const char *netLastError() { return g_error; }

bool netFetch(Payload &out) {
  if (!netOnline()) {
    if (millis() - g_joinSince >= REJOIN_AFTER_MS) {
      WiFi.reconnect();
      g_joinSince = millis();
    }
    fail("wifi down");
    return false;
  }
  g_joinSince = millis();

  // This runs on the main loop, so a dead bridge freezes touch and animation
  // for as long as these allow. The bridge is on the LAN and serves a prebuilt
  // document, so a healthy one answers well inside both. No keep-alive: the
  // client is rebuilt every poll, so a reused socket would only linger.
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(2000);
  if (!http.begin(settings().bridge)) {
    fail("bad BRIDGE_URL");
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(g_error, sizeof(g_error), "http %d", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    snprintf(g_error, sizeof(g_error), "json: %s", err.c_str());
    return false;
  }

  Payload p{};
  if (!payloadFromJson(doc, p, g_error, sizeof(g_error))) return false;

  g_error[0] = '\0';
  out = p;
  return true;
}
