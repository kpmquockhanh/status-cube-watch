#include "ota.h"

#include <ArduinoOTA.h>
#include <WiFi.h>

#include "settings.h"
#include "ui.h"

namespace {
Display *g_lcd = nullptr;
int g_lastPct = -1;
}  // namespace

void otaBegin(Display &lcd) {
  g_lcd = &lcd;

  // OTA invitations are single UDP packets. With modem sleep on, the radio
  // misses roughly half of them and uploads time out. This is a USB-powered
  // desk device, so stay awake.
  WiFi.setSleep(false);

  ArduinoOTA.setHostname("claude-cube");
  const char *pass = settings().otaPass;
  if (pass[0]) {
    ArduinoOTA.setPassword(pass);
  } else {
    Serial.println("[ota] no OTA password set -- anyone on this network can flash the cube "
                   "(set one in the setup portal)");
  }

  ArduinoOTA.onStart([]() {
    g_lastPct = -1;
    Serial.println("[ota] start");
    uiOta(*g_lcd, 0);
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    const int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
    if (pct == g_lastPct) return;  // every frame is a full-screen push; one per percent is plenty
    g_lastPct = pct;
    uiOta(*g_lcd, (uint8_t)pct);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[ota] done, rebooting");
    uiMessage(*g_lcd, "UPDATED", "rebooting");
  });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("[ota] error %u\n", (unsigned)err);
    uiMessage(*g_lcd, "UPDATE FAILED", "try again");
  });

  ArduinoOTA.begin();
  Serial.println("[ota] ready as claude-cube.local");
}

void otaHandle() { ArduinoOTA.handle(); }
