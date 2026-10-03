#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

#include <string>

#include "portal_util.h"
#include "ble.h"
#include "net.h"
#include "settings.h"
#include "ui.h"

namespace {

// Not the ESP default 192.168.4.1: the station side may join a LAN that uses
// 192.168.4.x, and two interfaces on one subnet send replies out the wrong one.
const IPAddress AP_IP(192, 168, 71, 1);

WebServer server(80);
DNSServer dns;
// Made fresh each time the portal starts and put in both forms; see portalTokenOk.
char g_token[17] = "";

std::string numField(const char *label, const char *name, int v, int lo, int hi, const char *hint) {
  return std::string("<label>") + label + "</label><input name=" + name + " type=number inputmode=numeric min=" +
         std::to_string(lo) + " max=" + std::to_string(hi) + " value=" + std::to_string(v) + ">" +
         (hint[0] ? std::string("<div class=hint>") + hint + "</div>" : std::string());
}

std::string formPage(const Settings &cur, const std::string &error) {
  std::string h =
      "<!doctype html><html><head><meta charset=utf-8>"
      "<meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>Claude cube setup</title><style>"
      "body{font:16px system-ui,sans-serif;background:#0b0d12;color:#e7ecf5;margin:0;padding:24px;max-width:420px}"
      "h1{font-size:20px;margin:0 0 16px}h2{font-size:15px;margin:28px 0 0;color:#ff8a5b}"
      "label{display:block;margin:14px 0 4px;color:#7c8598;font-size:13px}"
      "input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #2a3242;"
      "background:#151922;color:#e7ecf5;font-size:16px}"
      "button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:8px;background:#ff8a5b;"
      "color:#000;font-size:16px;font-weight:600}"
      ".err{color:#ff6b6b;margin-bottom:8px}.hint{color:#7c8598;font-size:12px;margin-top:4px}"
      "</style></head><body><h1>Claude cube setup</h1>";
  if (!error.empty()) h += "<div class=err>" + portalHtmlEscape(error) + "</div>";
  const std::string token = std::string("<input type=hidden name=t value=") + g_token + ">";
  h += "<form method=post action=/save>" + token +
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
       "placeholder=\"leave blank to keep\">";
  if (cur.otaPass[0])
    h += "<label><input type=checkbox name=otaclear style='width:auto'> Remove the OTA password</label>";
  h += "<h2>Display</h2>";
  const DeviceSettings &d = deviceSettings();
  h += numField("Brightness", "bl", d.backlight, DEV_MIN_BACKLIGHT, DEV_MAX_BACKLIGHT, "10 (dim) to 255 (full)");
  h += numField("Screen sleep (minutes)", "sl", d.sleepMin, 0, DEV_MAX_SLEEP_MIN,
                "Screen turns off after this long with no touch and no new data. 0 = never.");
  h += numField("Auto-advance cards (seconds)", "rt", d.rotateSec, 0, DEV_MAX_ROTATE_SEC, "0 = off");
  h += numField("WiFi refresh (seconds)", "pi", d.pollSec, DEV_MIN_POLL_SEC, DEV_MAX_POLL_SEC,
                "How often the cube polls the bridge over WiFi.");
  h += "<h2>Pomodoro</h2>";
  const PomoSettings &ps = pomoSettings();
  h += numField("Focus (minutes)", "pf", ps.focusMin, POMO_MIN_MINUTES, POMO_MAX_MINUTES, "");
  h += numField("Short break (minutes)", "ps", ps.shortMin, POMO_MIN_MINUTES, POMO_MAX_MINUTES, "");
  h += numField("Long break (minutes)", "pl", ps.longMin, POMO_MIN_MINUTES, POMO_MAX_MINUTES, "");
  h += numField("Sessions before long break", "pn", ps.sessions, POMO_MIN_SESSIONS, POMO_MAX_SESSIONS, "");
  h += "<button>Save and reboot</button></form>";
  if (bleBonded())
    h += "<form method=post action=/forget>" + token + "<button style='background:#2a3242;color:#e7ecf5'>"
         "Forget paired Mac</button></form>"
         "<div class=hint>Also remove &ldquo;Claude Cube&rdquo; in the Mac's Bluetooth settings.</div>";
  h += "</body></html>";
  return h;
}

void sendForm(int code, const std::string &error) {
  server.send(code, "text/html", formPage(settings(), error).c_str());
}

// Phones probe a known URL (/generate_204, /hotspot-detect.html, ...) to
// decide whether a network needs a sign-in page. Redirecting everything to
// the form is what makes the page open by itself.
// Send the client back to the address it used: the AP address on the AP, the
// cube's LAN address when the form is opened over the station side.
void handleNotFound() {
  server.sendHeader("Location", "http://" + server.client().localIP().toString() + "/");
  server.send(302, "text/plain", "");
}

// See portalHostIs: the form is only served to a page that names the cube by
// its address, and a save or forget must also carry the form's token.
bool hostOk() {
  return portalHostIs(server.hostHeader().c_str(), server.client().localIP().toString().c_str());
}

bool postOk() {
  if (!hostOk()) {
    handleNotFound();
    return false;
  }
  if (portalTokenOk(server.arg("t").c_str(), g_token)) return true;
  sendForm(403, "That page was out of date (the cube restarted). Here is a fresh one: try again.");
  return false;
}

void handleRoot() {
  if (!hostOk()) return handleNotFound();
  sendForm(200, "");
}

void handleSave() {
  if (!postOk()) return;
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
  const std::string ota =
      portalOtaPassword(cur.otaPass, server.arg("otapass").c_str(), server.hasArg("otaclear"));
  if (ota.size() > 63) return sendForm(400, "The OTA password can be at most 63 characters.");

  const DeviceSettings &dc = deviceSettings();
  const PomoSettings &pc = pomoSettings();
  int bl, sl, rt, pi, pf, pshort, pl, pn;
  if (!portalParseInt(server.arg("bl").c_str(), DEV_MIN_BACKLIGHT, DEV_MAX_BACKLIGHT, dc.backlight, bl))
    return sendForm(400, "Brightness must be a number from 10 to 255.");
  if (!portalParseInt(server.arg("sl").c_str(), 0, DEV_MAX_SLEEP_MIN, dc.sleepMin, sl))
    return sendForm(400, "Screen sleep must be 0 to 240 minutes.");
  if (!portalParseInt(server.arg("rt").c_str(), 0, DEV_MAX_ROTATE_SEC, dc.rotateSec, rt))
    return sendForm(400, "Auto-advance must be 0 to 255 seconds.");
  if (!portalParseInt(server.arg("pi").c_str(), DEV_MIN_POLL_SEC, DEV_MAX_POLL_SEC, dc.pollSec, pi))
    return sendForm(400, "WiFi refresh must be 2 to 60 seconds.");
  if (!portalParseInt(server.arg("pf").c_str(), POMO_MIN_MINUTES, POMO_MAX_MINUTES, pc.focusMin, pf) ||
      !portalParseInt(server.arg("ps").c_str(), POMO_MIN_MINUTES, POMO_MAX_MINUTES, pc.shortMin, pshort) ||
      !portalParseInt(server.arg("pl").c_str(), POMO_MIN_MINUTES, POMO_MAX_MINUTES, pc.longMin, pl))
    return sendForm(400, "Pomodoro durations must be 1 to 99 minutes.");
  if (!portalParseInt(server.arg("pn").c_str(), POMO_MIN_SESSIONS, POMO_MAX_SESSIONS, pc.sessions, pn))
    return sendForm(400, "Pomodoro sessions must be 1 to 9.");

  Settings next{};
  strlcpy(next.ssid, ssid.c_str(), sizeof(next.ssid));
  strlcpy(next.pass, pass.c_str(), sizeof(next.pass));
  strlcpy(next.bridge, bridge.c_str(), sizeof(next.bridge));
  strlcpy(next.otaPass, ota.c_str(), sizeof(next.otaPass));
  if (!settingsSave(next) ||
      !deviceSettingsSave(DeviceSettings{(uint8_t)bl, (uint8_t)sl, (uint8_t)rt, (uint8_t)pi}) ||
      !pomoSettingsSave(PomoSettings{(uint8_t)pf, (uint8_t)pshort, (uint8_t)pl, (uint8_t)pn}))
    return sendForm(500, "Could not write the settings to flash.");

  const std::string done =
      "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<body style='font:16px system-ui;background:#0b0d12;color:#e7ecf5;padding:24px'>"
      "Saved. The cube is rebooting and will join <b>" +
      portalHtmlEscape(ssid) + "</b>.</body>";
  server.send(200, "text/html", done.c_str());
  delay(1000);  // let the reply leave before the radio goes away
  ESP.restart();
}

void handleForget() {
  if (!postOk()) return;
  bleForgetBonds();
  server.send(200, "text/html",
              "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
              "<body style='font:16px system-ui;background:#0b0d12;color:#e7ecf5;padding:24px'>"
              "Forgotten. The cube is rebooting and will wait to pair again.</body>");
  delay(1000);
  ESP.restart();
}

// AP+STA with BLE up is the most the radio carries at once, and platformio.ini
// records an out-of-memory crash in that corner: log the internal heap.
void logHeap(const char *when) {
  Serial.printf("[portal] %s: internal heap free %u, largest block %u\n", when,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

}  // namespace

[[noreturn]] void portalRun(Display &lcd, bool autoRetry) {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ap[24];
  snprintf(ap, sizeof(ap), "claude-cube-%02X%02X", mac[4], mac[5]);
  snprintf(g_token, sizeof(g_token), "%08x%08x", (unsigned)esp_random(), (unsigned)esp_random());

  // AP+STA: the station side can rejoin the saved network while the AP serves
  // the form (the SoftAP takes the station's channel). The core's own
  // reconnect is off: PortalSta decides when a join may disturb the AP, and a
  // join still running from before the portal is stopped, not doubled.
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false);
  WiFi.softAP(ap);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  const IPAddress ip = WiFi.softAPIP();
  const String apIp = ip.toString();
  dns.start(53, "*", ip);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/forget", HTTP_POST, handleForget);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("[portal] AP %s at %s\n", ap, apIp.c_str());
  logHeap("started");

  PortalSta sta;
  sta.begin(millis(), !autoRetry);  // autoRetry: a join has just failed, so wait a period
  bool staWasUp = WiFi.status() == WL_CONNECTED;
  String lanIp = staWasUp ? WiFi.localIP().toString() : String();
  uiPortal(lcd, ap, apIp.c_str(), lanIp.c_str());

  bool showingPair = false;
  uint32_t paintedKey = 0;
  const bool bondedAtStart = bleBonded();
  for (;;) {
    dns.processNextRequest();
    server.handleClient();
    // An unpaired cube whose WiFi is unreachable lands here, and it must still
    // be pairable over Bluetooth: show the code, hold off the reboot, and
    // restart into the normal boot flow once the Mac is bonded.
    const BleState bs = bleState();
    if (bs == BleState::Pairing) {
      const uint32_t key = blePasskey();
      if (!showingPair || key != paintedKey) {
        uiBlePair(lcd, key);
        paintedKey = key;
      }
      showingPair = true;
    } else if (showingPair) {
      uiPortal(lcd, ap, apIp.c_str(), lanIp.c_str());
      showingPair = false;
    }
    if (portalBondedTransition(bondedAtStart, bleBonded())) {
      Serial.println("[portal] Mac paired over Bluetooth -- rebooting");
      delay(500);
      ESP.restart();
    }

    const bool phoneOnAp = WiFi.softAPgetStationNum() > 0;
    const bool staUp = WiFi.status() == WL_CONNECTED;
    if (staUp != staWasUp) {
      staWasUp = staUp;
      lanIp = staUp ? WiFi.localIP().toString() : String();
      if (staUp) {
        Serial.printf("[portal] joined %s, form also at %s\n", settings().ssid, lanIp.c_str());
        logHeap("joined");
      } else {
        Serial.println("[portal] station link lost");
      }
      if (!showingPair) uiPortal(lcd, ap, apIp.c_str(), lanIp.c_str());
    }
    switch (sta.update(millis(), settingsHaveWifi(), !phoneOnAp && bs != BleState::Pairing, staUp)) {
      case PortalStaAction::Begin:
        netJoinSaved();
        break;
      case PortalStaAction::Stop:
        WiFi.disconnect(false);  // station only; the AP stays up
        Serial.println("[portal] join attempt ended");
        break;
      case PortalStaAction::None:
        break;
    }
    const bool bleBusy = bs == BleState::Pairing || bs == BleState::Connected;
    if (portalStaRecoveredReboot(autoRetry, staUp, phoneOnAp, bleBusy)) {
      Serial.println("[portal] saved network is back -- rebooting");
      delay(500);
      ESP.restart();
    }
    delay(5);
  }
}
