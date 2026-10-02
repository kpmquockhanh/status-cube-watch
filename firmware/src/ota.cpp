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

  // Do NOT call WiFi.setSleep(false) here: BLE is always initialised, and the
  // ESP32 coexistence layer aborts ("Should enable WiFi modem sleep when both
  // WiFi and Bluetooth are enabled") if WiFi power-save is NONE while BT is on.
  // net.cpp keeps modem sleep on (WIFI_PS_MIN_MODEM); OTA runs under it and may
  // need a retry if an invitation packet is missed.

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

void otaEnd() { ArduinoOTA.end(); }
