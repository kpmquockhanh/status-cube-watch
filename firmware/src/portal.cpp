#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <string>

#include "portal_util.h"
#include "settings.h"
#include "ui.h"

namespace {

constexpr uint32_t IDLE_RETRY_MS = 60000;

WebServer server(80);
DNSServer dns;

std::string formPage(const Settings &cur, const std::string &error) {
  std::string h =
      "<!doctype html><html><head><meta charset=utf-8>"
      "<meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>Claude cube setup</title><style>"
      "body{font:16px system-ui,sans-serif;background:#0b0d12;color:#e7ecf5;margin:0;padding:24px;max-width:420px}"
      "h1{font-size:20px;margin:0 0 16px}"
      "label{display:block;margin:14px 0 4px;color:#7c8598;font-size:13px}"
      "input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #2a3242;"
      "background:#151922;color:#e7ecf5;font-size:16px}"
      "button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:8px;background:#ff8a5b;"
      "color:#000;font-size:16px;font-weight:600}"
      ".err{color:#ff6b6b;margin-bottom:8px}.hint{color:#7c8598;font-size:12px;margin-top:4px}"
      "</style></head><body><h1>Claude cube setup</h1>";
  if (!error.empty()) h += "<div class=err>" + portalHtmlEscape(error) + "</div>";
  h += "<form method=post action=/save>"
       "<label>WiFi network</label><input name=ssid maxlength=32 required value=\"" +
       portalHtmlEscape(cur.ssid) +
       "\">"
       "<label>WiFi password</label><input name=pass type=password maxlength=63 "
       "placeholder=\"leave blank to keep\">"
       "<div class=hint>Blank keeps the saved password unless you change the network "
       "(then it means an open network).</div>"
       "<label>Bridge URL</label><input name=bridge maxlength=127 required value=\"" +
       portalHtmlEscape(cur.bridge) +
       "\">"
       "<div class=hint>The machine running bridge/server.mjs, e.g. "
       "http://192.168.1.50:8787/api/status</div>"
       "<label>OTA password (optional)</label><input name=otapass type=password maxlength=63 "
       "placeholder=\"leave blank to keep\">"
       "<button>Save and reboot</button></form></body></html>";
  return h;
}

void sendForm(int code, const std::string &error) {
  server.send(code, "text/html", formPage(settings(), error).c_str());
}

void handleRoot() { sendForm(200, ""); }

void handleSave() {
  const Settings &cur = settings();
  const std::string ssid = server.arg("ssid").c_str();
  const std::string bridge = portalTrim(server.arg("bridge").c_str());

  if (!portalValidSsid(ssid)) return sendForm(400, "The WiFi network name must be 1 to 32 characters.");
  if (!portalValidBridgeUrl(bridge))
    return sendForm(400, "The bridge URL must look like http://host:port/path (plain http, no spaces).");

  const std::string pass =
      portalResolvePassword(cur.ssid, ssid, cur.pass, server.arg("pass").c_str());
  if (!portalValidWifiPassword(pass))
    return sendForm(400, "A WiFi password must be 8 to 63 characters (or empty for an open network).");
  const std::string ota = portalKeepIfBlank(cur.otaPass, server.arg("otapass").c_str());
  if (ota.size() > 63) return sendForm(400, "The OTA password can be at most 63 characters.");

  Settings next{};
  strlcpy(next.ssid, ssid.c_str(), sizeof(next.ssid));
  strlcpy(next.pass, pass.c_str(), sizeof(next.pass));
  strlcpy(next.bridge, bridge.c_str(), sizeof(next.bridge));
  strlcpy(next.otaPass, ota.c_str(), sizeof(next.otaPass));
  if (!settingsSave(next)) return sendForm(500, "Could not write the settings to flash.");

  const std::string done =
      "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<body style='font:16px system-ui;background:#0b0d12;color:#e7ecf5;padding:24px'>"
      "Saved. The cube is rebooting and will join <b>" +
      portalHtmlEscape(ssid) + "</b>.</body>";
  server.send(200, "text/html", done.c_str());
  delay(1000);  // let the reply leave before the radio goes away
  ESP.restart();
}

// Phones probe a known URL (/generate_204, /hotspot-detect.html, ...) to
// decide whether a network needs a sign-in page. Redirecting everything to
// the form is what makes the page open by itself.
void handleNotFound() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
  server.send(302, "text/plain", "");
}

}  // namespace

[[noreturn]] void portalRun(Display &lcd, bool autoRetry) {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ap[24];
  snprintf(ap, sizeof(ap), "claude-cube-%02X%02X", mac[4], mac[5]);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap);
  const IPAddress ip = WiFi.softAPIP();
  dns.start(53, "*", ip);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("[portal] AP %s at %s\n", ap, ip.toString().c_str());
  uiPortal(lcd, ap);

  uint32_t lastActive = millis();
  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    if (WiFi.softAPgetStationNum() > 0) lastActive = millis();
    if (autoRetry && millis() - lastActive > IDLE_RETRY_MS) {
      Serial.println("[portal] nobody joined -- rebooting to retry the saved network");
      ESP.restart();
    }
    delay(5);
  }
}
