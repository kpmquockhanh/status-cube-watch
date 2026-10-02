#include "net.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "settings.h"

namespace {
char g_error[64] = "";

void fail(const char *msg) { strlcpy(g_error, msg, sizeof(g_error)); }
}  // namespace

bool netBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);  // the radio idles between 5s polls
  const Settings &s = settings();
  // A null passphrase is how the WiFi library spells "open network".
  WiFi.begin(s.ssid, s.pass[0] ? s.pass : nullptr);
  Serial.printf("[net] connecting to %s", s.ssid);
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
    WiFi.reconnect();
    fail("wifi down");
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.setReuse(true);
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
