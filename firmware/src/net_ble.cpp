#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <esp_random.h>

#include "ble.h"
#include "ble_auth.h"
#include "ble_conn.h"
#include "ble_frame.h"
#include "settings_json.h"

// GATT server for the Mac app. NimBLE calls these callbacks from its own
// task, so they only copy bytes and set flags; parsing happens in bleTake(),
// on the main loop. See docs/ble-protocol.md.

namespace {

FrameAssembler g_asm;
NimBLECharacteristic *g_control = nullptr;

volatile BleState g_state = BleState::Off;
volatile uint32_t g_passkey = 0;
volatile uint32_t g_lastGood = 0;
volatile bool g_sendNow = false;
int g_bondsAtConnect = 0;
// The one link (advertising stops while it is up), for bleUnsecuredExpired().
volatile bool g_linkUp = false;
volatile bool g_secured = false;
volatile uint32_t g_connectAt = 0;
volatile uint16_t g_connHandle = 0;

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
char g_pending[BLE_MAX_PAYLOAD + 1];
size_t g_pendingLen = 0;
volatile bool g_pendingReady = false;

NimBLECharacteristic *g_settingsChar = nullptr;
char g_setPending[SETTINGS_JSON_MAX + 1];
volatile bool g_setReady = false;
// NimBLE stores whatever was written as the characteristic's value, so until it
// is republished a read returns that write, passwords and all.
volatile bool g_republish = false;

void notifyControl(const uint8_t *data, size_t len) {
  if (!g_control) return;
  // The (data, len) overload leaves the characteristic's stored value alone, so
  // the main loop and the NimBLE host task cannot race on it.
  g_control->notify(data, len);
}

// What a read of the Settings characteristic returns. Called from the main
// loop (and once at boot), never from the NimBLE task.
void publishSettings() {
  if (!g_settingsChar) return;
  char buf[SETTINGS_JSON_MAX + 1];
  const size_t n = settingsToJson(buf, sizeof(buf));
  if (n) g_settingsChar->setValue((const uint8_t *)buf, n);
}

struct ServerCb : NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    g_bondsAtConnect = NimBLEDevice::getNumBonds();
    g_asm.clear();
    g_connHandle = info.getConnHandle();
    g_connectAt = millis();
    g_secured = false;
    g_linkUp = true;
    g_state = BleState::Connected;
    Serial.printf("[ble] connect (bonds %d, interval %u ms)\n", g_bondsAtConnect,
                  (unsigned)(info.getConnInterval() * 5 / 4));
  }

  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    Serial.printf("[ble] disconnect %d\n", reason);
    g_passkey = 0;
    g_sendNow = false;
    g_linkUp = false;
    g_secured = false;
    g_state = BleState::Advertising;
    // Needed: NimBLE 2.x does not re-advertise by itself (advertiseOnDisconnect
    // defaults to off).
    NimBLEDevice::startAdvertising();
  }

  // Display-only IO capability: the stack asks us for the code to show.
  uint32_t onPassKeyDisplay() override {
    g_passkey = 100000 + esp_random() % 900000;  // never 0: main.cpp reads 0 as "no code"
    g_state = BleState::Pairing;
    Serial.printf("[ble] pairing passkey %06u\n", (unsigned)g_passkey);
    return g_passkey;
  }

  void onConfirmPassKey(NimBLEConnInfo &info, uint32_t) override { NimBLEDevice::injectConfirmPasskey(info, true); }

  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    g_passkey = 0;
    g_state = BleState::Connected;
    const BleAuthVerdict v = bleAuthVerdict(info.isEncrypted(), info.isAuthenticated(), info.isBonded(),
                                            g_bondsAtConnect, NimBLEDevice::getNumBonds());
    if (v == BleAuthVerdict::Accept) {
      g_secured = true;
      Serial.println("[ble] encrypted");
      // Pairing is done, so from here the link mostly idles (ble_conn.h). The
      // Mac answers in onConnParamsUpdate, or keeps its own if it declines.
      const BleConnParams &p = BLE_IDLE_PARAMS;
      NimBLEDevice::getServer()->updateConnParams(info.getConnHandle(), p.minItvl, p.maxItvl, p.latency, p.timeout);
      return;
    }
    Serial.printf("[ble] link refused (encrypted %d, passkey %d, bonded %d)\n", info.isEncrypted(),
                  info.isAuthenticated(), info.isBonded());
    if (v == BleAuthVerdict::RefuseDropBond) NimBLEDevice::deleteBond(info.getIdAddress());
    NimBLEDevice::getServer()->disconnect(info.getConnHandle());
  }

  void onConnParamsUpdate(NimBLEConnInfo &info) override {
    Serial.printf("[ble] conn params: interval %u ms, latency %u, timeout %u ms\n",
                  (unsigned)(info.getConnInterval() * 5 / 4), (unsigned)info.getConnLatency(),
                  (unsigned)info.getConnTimeout() * 10);
  }
};

struct PayloadCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    const NimBLEAttValue v = chr->getValue();
    const FrameResult r = g_asm.feed(v.data(), v.size());
    if (r == FrameResult::Complete) {
      portENTER_CRITICAL(&g_mux);
      memcpy(g_pending, g_asm.json(), g_asm.size() + 1);
      g_pendingLen = g_asm.size();
      g_pendingReady = true;
      portEXIT_CRITICAL(&g_mux);
      const uint8_t ack[2] = {BLE_CTRL_ACK, g_asm.seq()};
      notifyControl(ack, sizeof(ack));
    } else if (r == FrameResult::Bad) {
      Serial.println("[ble] bad frame dropped");
    }
  }
};

// A Settings write is one ATT value (up to 512 bytes): copy it out and let the
// main loop apply it, since saving to NVS from the NimBLE task is not safe.
struct SettingsCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    const NimBLEAttValue v = chr->getValue();
    if (v.size() == 0 || v.size() > SETTINGS_JSON_MAX) {
      g_republish = true;  // on the main loop, as publishSettings() must be
      const uint8_t r[2] = {BLE_CTRL_SETTINGS, (uint8_t)SettingsResult::Invalid};
      notifyControl(r, sizeof(r));
      return;
    }
    portENTER_CRITICAL(&g_mux);
    memcpy(g_setPending, v.data(), v.size());
    g_setPending[v.size()] = '\0';
    g_setReady = true;
    portEXIT_CRITICAL(&g_mux);
  }
};

struct ControlCb : NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t subValue) override {
    if (subValue & 1) g_sendNow = true;  // sent from bleTake(), on the main loop
  }
};

ServerCb g_serverCb;
PayloadCb g_payloadCb;
ControlCb g_controlCb;
SettingsCb g_settingsCb;

}  // namespace

void bleBegin() {
  NimBLEDevice::init("Claude Cube");
  NimBLEDevice::setSecurityAuth(true, true, true);  // bonding, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&g_serverCb);
  NimBLEService *svc = server->createService(BLE_SERVICE_UUID);

  NimBLECharacteristic *payload = svc->createCharacteristic(
      BLE_PAYLOAD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  payload->setCallbacks(&g_payloadCb);

  g_control = svc->createCharacteristic(BLE_CONTROL_UUID, NIMBLE_PROPERTY::NOTIFY);
  g_control->setCallbacks(&g_controlCb);

  NimBLECharacteristic *info = svc->createCharacteristic(
      BLE_INFO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);
  const uint8_t infoVal[2] = {BLE_PROTO_VER, BLE_FW_REV};
  info->setValue(infoVal, sizeof(infoVal));

  g_settingsChar = svc->createCharacteristic(
      BLE_SETTINGS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN |
                             NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  g_settingsChar->setCallbacks(&g_settingsCb);
  publishSettings();

  // No svc->start(): NimBLE 2.x starts the GATT server when advertising begins.
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);  // in the advertisement: the Mac scans by it
  adv->enableScanResponse(true);  // must precede setName(): only then does the name go to the scan response
  adv->setName("Claude Cube");    // too long beside a 128-bit UUID in one packet
  const bool started = adv->start();
  Serial.printf("[ble] advertising start=%d\n", (int)started);
  if (!started) Serial.println("[ble] ERROR: advertising failed to start");
  g_state = BleState::Advertising;
  Serial.printf("[ble] advertising, bonds %d\n", NimBLEDevice::getNumBonds());
}

BleState bleState() { return g_state; }

uint32_t blePasskey() { return g_passkey; }

bool bleBonded() { return NimBLEDevice::getNumBonds() > 0; }

uint32_t bleLastGood() { return g_lastGood; }

void bleForgetBonds() {
  NimBLEDevice::deleteAllBonds();
  Serial.println("[ble] bonds deleted");
}

bool bleTake(Payload &out) {
  if (g_republish) {
    g_republish = false;
    publishSettings();
  }
  if (bleUnsecuredExpired(g_linkUp, g_secured, g_connectAt, millis())) {
    g_linkUp = false;  // once; onDisconnect follows
    Serial.println("[ble] link never secured: dropping it");
    NimBLEDevice::getServer()->disconnect(g_connHandle);
  }
  if (g_sendNow) {
    g_sendNow = false;
    const uint8_t m = BLE_CTRL_SEND_NOW;
    notifyControl(&m, 1);
  }
  if (!g_pendingReady) return false;

  static char local[BLE_MAX_PAYLOAD + 1];
  portENTER_CRITICAL(&g_mux);
  memcpy(local, g_pending, g_pendingLen + 1);
  g_pendingReady = false;
  portEXIT_CRITICAL(&g_mux);

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, local);
  if (err) {
    Serial.printf("[ble] json: %s\n", err.c_str());
    return false;
  }
  Payload p{};
  char why[64] = "";
  if (!payloadFromJson(doc, p, why, sizeof(why))) {
    Serial.printf("[ble] payload rejected: %s\n", why);
    return false;
  }
  out = p;
  const uint32_t now = millis();
  g_lastGood = now ? now : 1;  // 0 means "never"
  return true;
}

bool bleTakeSettings(char *out, size_t cap) {
  if (!g_setReady) return false;
  portENTER_CRITICAL(&g_mux);
  strlcpy(out, g_setPending, cap);
  g_setReady = false;
  portEXIT_CRITICAL(&g_mux);
  return true;
}

void bleSettingsReply(uint8_t result) {
  publishSettings();
  const uint8_t m[2] = {BLE_CTRL_SETTINGS, result};
  notifyControl(m, sizeof(m));
}

void bleNotifyPomodoro(uint8_t ended, uint8_t next) {
  uint8_t m[BLE_POMO_ENDED_LEN];
  const size_t len = bleEncodePomoEnded(ended, next, m);
  notifyControl(m, len);
}
