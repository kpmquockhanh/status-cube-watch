#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_random.h>

#include "ble.h"
#include "ble_bonds.h"
#include "ble_conn.h"
#include "ble_frame.h"
#include "nav_frame.h"
#include "settings_json.h"

// GATT server for the Mac app and the iPhone, both bonded and connected at
// once. NimBLE calls these callbacks from its own task (small stack), so they
// only copy bytes and set flags; parsing JSON, NVS writes and evicting a
// replaced bond happen on the main loop, in bleTake(). See docs/ble-protocol.md.

namespace {

constexpr int MAX_LINKS = CONFIG_BT_NIMBLE_MAX_CONNECTIONS;  // 2 + one transient during the pairing window
constexpr int MAX_BONDS = CONFIG_BT_NIMBLE_MAX_BONDS;
static_assert(MAX_LINKS >= 3, "two links plus a replacement pairing need 3 connections");
static_assert(MAX_BONDS >= 3, "a Mac, an iPhone and a replacement pairing need 3 bonds");

FrameAssembler g_asm;
// Navigation frames: 8 KB of reassembly plus two 4 KB scenes, all static,
// never on the NimBLE task's stack. g_navWork is decoded into on the NimBLE
// task; g_navOut is the hand-over copy bleTakeNav() reads under g_mux.
NavAssembler g_navAsm;
NavScene g_navWork;
NavScene g_navOut;
volatile bool g_navReady = false;
NimBLECharacteristic *g_control = nullptr;

volatile BleState g_state = BleState::Off;
volatile uint32_t g_passkey = 0;
volatile uint32_t g_lastGood = 0;
volatile bool g_sendNow = false;

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

// One per connection. Written on the NimBLE task; the main loop reads the
// fields it needs under g_mux.
struct Link {
  bool used;
  uint16_t handle;
  uint32_t connectAt;
  bool secured;
  bool dropping;   // bleUnsecuredExpired already asked for the disconnect
  BondKind kind;   // Unknown until secured and identified
  bool kindDirty;  // learned on this link: the main loop stores it and evicts
  bool newBond;    // pairing on this link stored a bond that was not there before
  NimBLEAddress id;
  int nBefore;  // bonds stored when the link came up, and which
  NimBLEAddress before[MAX_BONDS];
};
Link g_links[MAX_LINKS];

// The kind of every bond, loaded from NVS at boot (ble/<12 hex digits>).
struct KindEntry {
  bool used;
  uint8_t addr[6];
  BondKind kind;
};
KindEntry g_kinds[MAX_BONDS + 1];

// Bonds to delete on the main loop: a new bond whose link dropped before it said
// what it is (bleDropBondOnDisconnect).
NimBLEAddress g_dropQueue[MAX_LINKS];
int g_dropCount = 0;

// The pairing window after a "Pair new device" reboot (NVS ble/pw).
volatile bool g_pairRequested = false;
volatile uint32_t g_pairOpenedAt = 0;

// The latest Volume write, copied out of the NimBLE task; bleTakeVolume() parses it.
uint8_t g_volPending[VOLUME_FRAME_MAX];
size_t g_volLen = 0;
volatile bool g_volReady = false;
volatile bool g_volDropped = false;  // the Mac's link dropped: forget its state
MacVolume g_vol{};                   // main loop only
char g_pending[BLE_MAX_PAYLOAD + 1];
size_t g_pendingLen = 0;
volatile bool g_pendingReady = false;

NimBLECharacteristic *g_settingsChar = nullptr;
char g_setPending[SETTINGS_JSON_MAX + 1];
volatile bool g_setReady = false;
// NimBLE stores whatever was written as the characteristic's value, so until it
// is republished a read returns that write, passwords and all.
volatile bool g_republish = false;

bool pairingOpen() { return blePairWindowOpen(g_pairRequested, g_pairOpenedAt, millis()); }

// "aabbccddeeff", most significant byte first, as NimBLEAddress::toString().
void kindKey(const uint8_t *addr, char out[13]) {
  snprintf(out, 13, "%02x%02x%02x%02x%02x%02x", addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

BondKind kindOf(const NimBLEAddress &a) {
  BondKind k = BondKind::Unknown;
  portENTER_CRITICAL(&g_mux);
  for (const KindEntry &e : g_kinds)
    if (e.used && memcmp(e.addr, a.getVal(), 6) == 0) k = e.kind;
  portEXIT_CRITICAL(&g_mux);
  return k;
}

void kindSet(const NimBLEAddress &a, BondKind k) {
  portENTER_CRITICAL(&g_mux);
  KindEntry *slot = nullptr;
  for (KindEntry &e : g_kinds)
    if (e.used && memcmp(e.addr, a.getVal(), 6) == 0) slot = &e;
  if (!slot)
    for (KindEntry &e : g_kinds)
      if (!e.used && !slot) slot = &e;
  if (slot) {
    slot->used = true;
    memcpy(slot->addr, a.getVal(), 6);
    slot->kind = k;
  }
  portEXIT_CRITICAL(&g_mux);
}

// Main loop only (NVS).
void kindStore(const NimBLEAddress &a, BondKind k) {
  char key[13];
  kindKey(a.getVal(), key);
  Preferences p;
  if (!p.begin("ble", false)) return;
  if (k == BondKind::Unknown) p.remove(key);
  else p.putUChar(key, k == BondKind::Mac ? 'm' : 'p');
  p.end();
}

void kindForget(const NimBLEAddress &a) {
  portENTER_CRITICAL(&g_mux);
  for (KindEntry &e : g_kinds)
    if (e.used && memcmp(e.addr, a.getVal(), 6) == 0) e.used = false;
  portEXIT_CRITICAL(&g_mux);
  kindStore(a, BondKind::Unknown);
}

const char *kindName(BondKind k) { return k == BondKind::Mac ? "Mac" : k == BondKind::Phone ? "iPhone" : "unknown"; }

// NimBLE task only (the link table is written there).
Link *linkFor(uint16_t handle) {
  for (Link &l : g_links)
    if (l.used && l.handle == handle) return &l;
  return nullptr;
}

void requestParams(uint16_t handle, const BleConnParams &p) {
  NimBLEDevice::getServer()->updateConnParams(handle, p.minItvl, p.maxItvl, p.latency, p.timeout);
}

// The first write on a secured link says what the peer is. NimBLE task.
void learnKind(const NimBLEConnInfo &info, BondKind kind) {
  Link *l = linkFor(info.getConnHandle());
  if (!l || !l->secured || l->kind != BondKind::Unknown) return;
  portENTER_CRITICAL(&g_mux);
  l->kind = kind;
  l->kindDirty = true;
  portEXIT_CRITICAL(&g_mux);
  kindSet(l->id, kind);
  Serial.printf("[ble] link %u is the %s\n", (unsigned)l->handle, kindName(kind));
  if (kind == BondKind::Phone) requestParams(l->handle, BLE_NAV_PARAMS);
}

void advertiseIfRoom() {
  if (bleShouldAdvertise(NimBLEDevice::getServer()->getConnectedCount(), pairingOpen()))
    NimBLEDevice::startAdvertising();
}

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
  void onConnect(NimBLEServer *server, NimBLEConnInfo &info) override {
    Link *l = nullptr;
    for (Link &x : g_links)
      if (!x.used && !l) l = &x;
    if (!l) {  // more links than slots: cannot happen with MAX_LINKS = the stack's limit
      server->disconnect(info.getConnHandle());
      return;
    }
    Link fresh{};
    fresh.handle = info.getConnHandle();
    fresh.connectAt = millis();
    fresh.kind = BondKind::Unknown;
    const int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n && i < MAX_BONDS; i++) fresh.before[fresh.nBefore++] = NimBLEDevice::getBondedAddress(i);
    fresh.used = true;
    portENTER_CRITICAL(&g_mux);
    *l = fresh;
    portEXIT_CRITICAL(&g_mux);
    if (g_state != BleState::Pairing) g_state = BleState::Connected;
    const int links = server->getConnectedCount();
    Serial.printf("[ble] connect %u (links %d, bonds %d, interval %u ms)\n", (unsigned)fresh.handle, links, n,
                  (unsigned)(info.getConnInterval() * 5 / 4));
    // A connection stops advertising; keep it going while the other device may
    // still come (bleShouldAdvertise).
    advertiseIfRoom();
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &info, int reason) override {
    Link *l = linkFor(info.getConnHandle());
    const BondKind kind = l ? l->kind : BondKind::Unknown;
    const bool wasSecured = l && l->secured;
    if (l) {
      const bool drop = bleDropBondOnDisconnect(l->secured, l->newBond, l->kind);
      portENTER_CRITICAL(&g_mux);
      if (drop && g_dropCount < MAX_LINKS) g_dropQueue[g_dropCount++] = l->id;
      l->used = false;
      portEXIT_CRITICAL(&g_mux);
      if (drop) Serial.printf("[ble] new bond %s left before saying what it is: deleting it\n", l->id.toString().c_str());
    }
    Serial.printf("[ble] disconnect %u (%s) %d\n", (unsigned)info.getConnHandle(), kindName(kind), reason);
    if (!wasSecured) g_passkey = 0;  // the link that was pairing
    if (kind == BondKind::Mac) {
      g_sendNow = false;
      g_asm.clear();  // a restarted helper may reuse a seq
      portENTER_CRITICAL(&g_mux);
      g_volReady = false;
      g_volDropped = true;
      portEXIT_CRITICAL(&g_mux);
    } else if (kind == BondKind::Phone) {
      g_navAsm.clear();
    }
    const int links = server->getConnectedCount();
    if (g_passkey == 0) g_state = links ? BleState::Connected : BleState::Advertising;
    // Needed: NimBLE 2.x does not re-advertise by itself (advertiseOnDisconnect
    // defaults to off).
    advertiseIfRoom();
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
    Link *l = linkFor(info.getConnHandle());
    const NimBLEAddress id = info.getIdAddress();
    bool known = false;
    int before = 0;
    if (l) {
      before = l->nBefore;
      for (int i = 0; i < l->nBefore; i++)
        if (l->before[i] == id) known = true;
    }
    const bool newBond = info.isBonded() && !known;
    const bool open = pairingOpen();
    const BleAuthVerdict v =
        bleAuthVerdict(info.isEncrypted(), info.isAuthenticated(), info.isBonded(), newBond, before, open);
    if (v == BleAuthVerdict::Accept && l) {
      const BondKind kind = kindOf(id);
      portENTER_CRITICAL(&g_mux);
      l->id = id;
      l->kind = kind;
      l->secured = true;
      l->newBond = newBond;
      portEXIT_CRITICAL(&g_mux);
      // One device per window: it closes once a new bond beside others is in.
      if (newBond && before > 0) g_pairRequested = false;
      Serial.printf("[ble] encrypted %u: %s%s\n", (unsigned)l->handle, kindName(kind), newBond ? " (new bond)" : "");
      // The phone streams frames; the Mac's link mostly idles (ble_conn.h). The
      // peer answers in onConnParamsUpdate, or keeps its own if it declines.
      requestParams(l->handle, kind == BondKind::Phone ? BLE_NAV_PARAMS : BLE_IDLE_PARAMS);
      return;
    }
    Serial.printf("[ble] link refused (encrypted %d, passkey %d, bonded %d, new %d, bonds %d, window %d)\n",
                  info.isEncrypted(), info.isAuthenticated(), info.isBonded(), newBond, before, open);
    if (v == BleAuthVerdict::RefuseDropBond) NimBLEDevice::deleteBond(id);
    NimBLEDevice::getServer()->disconnect(info.getConnHandle());
  }

  void onConnParamsUpdate(NimBLEConnInfo &info) override {
    Serial.printf("[ble] conn params %u: interval %u ms, latency %u, timeout %u ms\n", (unsigned)info.getConnHandle(),
                  (unsigned)(info.getConnInterval() * 5 / 4), (unsigned)info.getConnLatency(),
                  (unsigned)info.getConnTimeout() * 10);
  }
};

struct PayloadCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    learnKind(info, BondKind::Mac);
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

// Navigation frames from the iPhone: reassembled and decoded here, into static
// scenes (never the stack), and handed to the loop by bleTakeNav(). No ack: the
// phone writes without response and the newest frame wins.
struct NavCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    learnKind(info, BondKind::Phone);
    const NimBLEAttValue v = chr->getValue();
    const FrameResult r = g_navAsm.feed(v.data(), v.size());
    if (r == FrameResult::Complete) {
      if (!navDecode(g_navAsm.data(), g_navAsm.size(), g_navWork)) {
        Serial.println("[ble] nav frame rejected");
        return;
      }
      portENTER_CRITICAL(&g_mux);
      memcpy(&g_navOut, &g_navWork, sizeof(NavScene));
      g_navReady = true;
      portEXIT_CRITICAL(&g_mux);
    } else if (r == FrameResult::Bad) {
      Serial.println("[ble] bad nav chunk dropped");
    }
  }
};

// A Settings write is one ATT value (up to 512 bytes): copy it out and let the
// main loop apply it, since saving to NVS from the NimBLE task is not safe.
struct SettingsCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    learnKind(info, BondKind::Mac);
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

struct VolumeCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    learnKind(info, BondKind::Mac);
    const NimBLEAttValue v = chr->getValue();
    if (v.size() == 0 || v.size() > VOLUME_FRAME_MAX) return;
    portENTER_CRITICAL(&g_mux);
    memcpy(g_volPending, v.data(), v.size());
    g_volLen = v.size();
    g_volReady = true;
    portEXIT_CRITICAL(&g_mux);
  }
};

struct ControlCb : NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t subValue) override {
    if (subValue & 1) g_sendNow = true;  // sent from bleTake(), on the main loop
  }
};

// A bonded Mac caches the GATT table and never rediscovers on its own, so a
// firmware that adds a characteristic stays invisible to it. When the firmware
// revision differs from the one that last advertised, flag Service Changed for
// the whole range: NimBLE persists it against each bonded peer's CCCD and
// indicates it once that peer reconnects and restores encryption, and macOS
// then rediscovers. Needs the GATT server started (advertising starts it).
void announceGattChange() {
  Preferences p;
  if (!p.begin("ble", false)) return;
  const uint8_t last = p.getUChar("gr", 0);
  if (last != BLE_FW_REV) {
    NimBLEDevice::getServer()->sendServiceChangedIndication();
    p.putUChar("gr", BLE_FW_REV);
    Serial.printf("[ble] GATT rev %u -> %u: service changed queued for bonded peers\n", last, BLE_FW_REV);
  }
  p.end();
}

// Loads every bond's kind into g_kinds. Firmware before fw_rev 8 bonded only a
// Mac, so a bond it left behind (no kind stored, `gr` 1..7) is that Mac. Also
// reads and clears the "Pair new device" flag. Before announceGattChange(),
// which rewrites `gr`.
void loadBondState() {
  Preferences p;
  if (!p.begin("ble", false)) return;
  const uint8_t rev = p.getUChar("gr", 0);
  const bool legacy = rev != 0 && rev < 8;
  const int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n; i++) {
    const NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
    char key[13];
    kindKey(a.getVal(), key);
    const uint8_t v = p.getUChar(key, 0);
    BondKind k = v == 'm' ? BondKind::Mac : v == 'p' ? BondKind::Phone : BondKind::Unknown;
    if (k == BondKind::Unknown && legacy) {
      k = BondKind::Mac;
      p.putUChar(key, 'm');
    }
    if (k != BondKind::Unknown) kindSet(a, k);
    Serial.printf("[ble] bond %s: %s\n", a.toString().c_str(), kindName(k));
  }
  if (p.getBool("pw", false)) {
    p.remove("pw");
    g_pairRequested = true;
  }
  p.end();
}

// New bonds whose link dropped before their first write (main loop).
void dropUnidentifiedBonds() {
  for (;;) {
    NimBLEAddress a;
    bool any = false;
    portENTER_CRITICAL(&g_mux);
    if (g_dropCount > 0) {
      a = g_dropQueue[--g_dropCount];
      any = true;
    }
    portEXIT_CRITICAL(&g_mux);
    if (!any) return;
    if (kindOf(a) != BondKind::Unknown) continue;  // it reconnected and identified itself meanwhile
    kindForget(a);
    NimBLEDevice::deleteBond(a);
  }
}

// NimBLE's bond store is full. Its default (ble_store_util_status_rr) deletes
// the oldest bond, which may be the Mac. Here only an Unknown bond may make
// room (bleStoreOverflowVictim); otherwise the write is refused and the new
// pairing does not bond. NimBLE host task.
struct StoreCb : NimBLEDeviceCallbacks {
  int onStoreStatus(struct ble_store_status_event *event, void *) override {
    // FULL is a warning before a pairing; the real decision comes at OVERFLOW.
    if (event->event_code != BLE_STORE_EVENT_OVERFLOW) return 0;
    const int type = event->overflow.obj_type;
    const ble_addr_t *writing = nullptr;
    if (type == BLE_STORE_OBJ_TYPE_OUR_SEC || type == BLE_STORE_OBJ_TYPE_PEER_SEC)
      writing = &event->overflow.value->sec.peer_addr;
    else if (type == BLE_STORE_OBJ_TYPE_CCCD)
      writing = &event->overflow.value->cccd.peer_addr;
    else if (type == BLE_STORE_OBJ_TYPE_CSFC)
      writing = &event->overflow.value->csfc.peer_addr;

    const int n = min(NimBLEDevice::getNumBonds(), MAX_BONDS + 1);
    NimBLEAddress addrs[MAX_BONDS + 1];
    BondKind kinds[MAX_BONDS + 1];
    uint32_t ages[MAX_BONDS + 1];
    bool linked[MAX_BONDS + 1];
    int self = -1;
    for (int i = 0; i < n; i++) {
      addrs[i] = NimBLEDevice::getBondedAddress(i);
      kinds[i] = kindOf(addrs[i]);
      ages[i] = (uint32_t)(n - i);  // oldest first, as storeLearnedKinds
      linked[i] = false;
      for (const Link &l : g_links)
        if (l.used && l.secured && l.id == addrs[i]) linked[i] = true;
      if (writing && memcmp(addrs[i].getVal(), writing->val, 6) == 0) self = i;
    }
    const int victim = bleStoreOverflowVictim(kinds, ages, linked, n, self);
    if (victim < 0) {
      Serial.printf("[ble] bond store full (type %d): no unknown bond to drop, write refused\n", type);
      return BLE_HS_ESTORE_CAP;
    }
    Serial.printf("[ble] bond store full (type %d): dropping unknown bond %s\n", type,
                  addrs[victim].toString().c_str());
    portENTER_CRITICAL(&g_mux);
    for (KindEntry &e : g_kinds)
      if (e.used && memcmp(e.addr, addrs[victim].getVal(), 6) == 0) e.used = false;
    portEXIT_CRITICAL(&g_mux);
    return NimBLEDevice::deleteBond(addrs[victim]) ? 0 : BLE_HS_ESTORE_CAP;
  }
};

// A link learned its kind (NimBLE task): store it, and if it replaces an older
// bond of the same kind, delete that one (ble_gap_unpair also drops its link).
void storeLearnedKinds() {
  for (int i = 0; i < MAX_LINKS; i++) {
    bool dirty = false;
    NimBLEAddress id;
    BondKind kind = BondKind::Unknown;
    portENTER_CRITICAL(&g_mux);
    Link &l = g_links[i];
    if (l.used && l.kindDirty) {
      l.kindDirty = false;
      dirty = true;
      id = l.id;
      kind = l.kind;
    }
    portEXIT_CRITICAL(&g_mux);
    if (!dirty) continue;

    const int n = min(NimBLEDevice::getNumBonds(), MAX_BONDS + 1);
    NimBLEAddress addrs[MAX_BONDS + 1];
    BondKind kinds[MAX_BONDS + 1];
    uint32_t ages[MAX_BONDS + 1];
    int self = -1;
    for (int j = 0; j < n; j++) {
      addrs[j] = NimBLEDevice::getBondedAddress(j);
      kinds[j] = kindOf(addrs[j]);
      ages[j] = (uint32_t)(n - j);  // NimBLE's store keeps bonds oldest first
      if (addrs[j] == id) self = j;
    }
    if (self < 0) continue;  // a link paired without bonding: nothing to remember
    kindStore(id, kind);
    const int evict = bleBondToEvict(kinds, ages, n, self);
    if (evict < 0) continue;
    Serial.printf("[ble] new %s replaces %s: bond deleted\n", kindName(kind), addrs[evict].toString().c_str());
    kindForget(addrs[evict]);
    NimBLEDevice::deleteBond(addrs[evict]);
  }
}

ServerCb g_serverCb;
PayloadCb g_payloadCb;
ControlCb g_controlCb;
SettingsCb g_settingsCb;
VolumeCb g_volumeCb;
NavCb g_navCb;
StoreCb g_storeCb;

}  // namespace

void bleBegin() {
  NimBLEDevice::init("Claude Cube");
  NimBLEDevice::setDeviceCallbacks(&g_storeCb);
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

  NimBLECharacteristic *volume = svc->createCharacteristic(
      BLE_VOLUME_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  volume->setCallbacks(&g_volumeCb);

  NimBLECharacteristic *nav = svc->createCharacteristic(
      BLE_NAV_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC |
                        NIMBLE_PROPERTY::WRITE_AUTHEN);
  nav->setCallbacks(&g_navCb);

  loadBondState();

  // No svc->start(): NimBLE 2.x starts the GATT server when advertising begins.
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);  // in the advertisement: the Mac scans by it
  adv->enableScanResponse(true);  // must precede setName(): only then does the name go to the scan response
  adv->setName("Claude Cube");    // too long beside a 128-bit UUID in one packet
  const bool started = adv->start();
  Serial.printf("[ble] advertising start=%d\n", (int)started);
  if (!started) Serial.println("[ble] ERROR: advertising failed to start");
  if (started) announceGattChange();
  g_state = BleState::Advertising;
  if (g_pairRequested) {
    g_pairOpenedAt = millis();
    Serial.printf("[ble] pairing window open for %u s\n", (unsigned)(BLE_PAIR_WINDOW_MS / 1000));
  }
  Serial.printf("[ble] advertising, bonds %d\n", NimBLEDevice::getNumBonds());
}

BleState bleState() { return g_state; }

uint32_t blePasskey() { return g_passkey; }

bool bleBonded() { return NimBLEDevice::getNumBonds() > 0; }

namespace {
bool bondedKind(BondKind k) {
  const int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n; i++)
    if (kindOf(NimBLEDevice::getBondedAddress(i)) == k) return true;
  return false;
}
}  // namespace

bool bleMacBonded() { return bondedKind(BondKind::Mac); }

bool blePhoneBonded() { return bondedKind(BondKind::Phone); }

bool bleUnknownBonded() { return bondedKind(BondKind::Unknown); }

uint32_t bleLastGood() { return g_lastGood; }

void bleForget(BondKind k) {
  for (int i = NimBLEDevice::getNumBonds() - 1; i >= 0; i--) {
    const NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
    if (kindOf(a) != k) continue;
    kindForget(a);
    NimBLEDevice::deleteBond(a);
    Serial.printf("[ble] %s bond %s deleted\n", kindName(k), a.toString().c_str());
  }
}

void bleRequestPairingAtBoot() {
  Preferences p;
  if (!p.begin("ble", false)) return;
  p.putBool("pw", true);
  p.end();
  Serial.println("[ble] pairing window requested for the next boot");
}

bool blePairingOpen() { return pairingOpen(); }

bool bleTakeNav(NavScene &out) {
  if (!g_navReady) return false;
  portENTER_CRITICAL(&g_mux);
  memcpy(&out, &g_navOut, sizeof(NavScene));
  g_navReady = false;
  portEXIT_CRITICAL(&g_mux);
  return true;
}

bool bleTake(Payload &out) {
  if (g_republish) {
    g_republish = false;
    publishSettings();
  }
  const uint32_t t = millis();
  for (int i = 0; i < MAX_LINKS; i++) {
    uint16_t drop = 0xFFFF;
    portENTER_CRITICAL(&g_mux);
    Link &l = g_links[i];
    if (!l.dropping && bleUnsecuredExpired(l.used, l.secured, l.connectAt, t)) {
      l.dropping = true;  // once; onDisconnect follows
      drop = l.handle;
    }
    portEXIT_CRITICAL(&g_mux);
    if (drop != 0xFFFF) {
      Serial.printf("[ble] link %u never secured: dropping it\n", (unsigned)drop);
      NimBLEDevice::getServer()->disconnect(drop);
    }
  }
  storeLearnedKinds();
  dropUnidentifiedBonds();
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

void bleSettingsChanged() { publishSettings(); }

bool bleTakeVolume(MacVolume &out) {
  uint8_t buf[VOLUME_FRAME_MAX];
  size_t len = 0;
  bool ready, dropped;
  portENTER_CRITICAL(&g_mux);
  ready = g_volReady;
  dropped = g_volDropped;
  if (ready) {
    len = g_volLen;
    memcpy(buf, g_volPending, len);
  }
  g_volReady = g_volDropped = false;
  portEXIT_CRITICAL(&g_mux);

  bool changed = false;
  if (dropped && g_vol.known) {
    g_vol = MacVolume{};
    changed = true;
  }
  if (ready) {
    MacVolume v = g_vol;
    if (!volumeParse(buf, len, v)) Serial.println("[ble] volume write rejected");
    else if (!volumeSame(v, g_vol)) {
      g_vol = v;
      changed = true;
    }
  }
  out = g_vol;
  return changed;
}

void bleSendMedia(MediaKey key) {
  uint8_t m[BLE_MEDIA_REQ_LEN];
  mediaRequestEncode(key, m);
  notifyControl(m, sizeof(m));
}

void bleSendUnlock() {
  uint8_t m[BLE_UNLOCK_REQ_LEN];
  unlockRequestEncode(m);
  notifyControl(m, sizeof(m));
  Serial.println("[ble] unlock requested");
}

void bleSendVolume(uint8_t level, bool muted) {
  uint8_t m[BLE_VOLUME_REQ_LEN];
  volumeRequestEncode(level, muted, m);
  notifyControl(m, sizeof(m));
}
