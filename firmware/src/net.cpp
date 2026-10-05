#include "net.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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

// The HTTP request runs on its own task. On the loop, a bridge that was down
// froze touch and animation for the connect and read timeouts, and a DNS
// lookup that never answered (a BRIDGE_URL with a host name) for up to 15 s.
//
// g_stage hands the buffers below back and forth. The loop writes g_url (and
// the result, for a failure it finds itself) only while Idle. The task writes
// g_fetched, g_fetchOk and g_fetchError only while Busy. The loop reads them
// only once Done. Each change of stage goes through g_mux, which is also the
// memory barrier between the two cores.
enum class Stage : uint8_t { Idle, Busy, Done };
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
volatile Stage g_stage = Stage::Idle;
TaskHandle_t g_task = nullptr;
char g_url[sizeof(Settings::bridge)];
Payload g_fetched;
bool g_fetchOk = false;
char g_fetchError[64] = "";
// HTTPClient, the socket calls and the JSON parse ran on the 8 KB loop stack
// before, so the task gets the same.
constexpr uint32_t FETCH_STACK = 8192;
// netStop() waits this long for a fetch in flight: past both timeouts below.
constexpr uint32_t STOP_WAIT_MS = 3500;

Stage stage() {
  portENTER_CRITICAL(&g_mux);
  const Stage s = g_stage;
  portEXIT_CRITICAL(&g_mux);
  return s;
}

void setStage(Stage s) {
  portENTER_CRITICAL(&g_mux);
  g_stage = s;
  portEXIT_CRITICAL(&g_mux);
}

void fetchFail(const char *msg) { strlcpy(g_fetchError, msg, sizeof(g_fetchError)); }

// On the fetch task. Fills g_fetched, or g_fetchError on failure.
bool fetchBridge() {
  // The bridge is on the LAN and serves a prebuilt document, so a healthy one
  // answers well inside both timeouts. No keep-alive: the client is rebuilt
  // every poll, so a reused socket would only linger.
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(2000);
  if (!http.begin(g_url)) {
    fetchFail("bad BRIDGE_URL");
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(g_fetchError, sizeof(g_fetchError), "http %d", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    snprintf(g_fetchError, sizeof(g_fetchError), "json: %s", err.c_str());
    return false;
  }

  memset(&g_fetched, 0, sizeof(g_fetched));
  return payloadFromJson(doc, g_fetched, g_fetchError, sizeof(g_fetchError));
}

void fetchTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // netRequest()
    g_fetchOk = fetchBridge();
    setStage(Stage::Done);
  }
}
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
  // Pulling the radio from under a socket in use is not something to rely on,
  // and a healthy bridge answers in milliseconds, so this rarely waits. A
  // lookup that hangs is not waited for: it fails on its own later.
  const uint32_t t0 = millis();
  while (stage() == Stage::Busy && millis() - t0 < STOP_WAIT_MS) delay(10);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  if (stage() == Stage::Done) setStage(Stage::Idle);  // nobody wants that result now
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

void netRequest() {
  if (stage() != Stage::Idle) return;  // one at a time; a finished one waits for netTake()
  if (!netOnline()) {
    if (millis() - g_joinSince >= REJOIN_AFTER_MS) {
      WiFi.reconnect();
      g_joinSince = millis();
    }
    g_fetchOk = false;
    fetchFail("wifi down");
    setStage(Stage::Done);
    return;
  }
  g_joinSince = millis();

  // Started on first use: a cube that only ever hears from the Mac over BLE
  // never spends the stack.
  if (!g_task && xTaskCreate(fetchTask, "fetch", FETCH_STACK, nullptr, 1, &g_task) != pdPASS) {
    g_task = nullptr;
    g_fetchOk = false;
    fetchFail("no memory for fetch task");
    setStage(Stage::Done);
    return;
  }
  strlcpy(g_url, settings().bridge, sizeof(g_url));  // settings may change while the task reads it
  setStage(Stage::Busy);
  xTaskNotifyGive(g_task);
}

NetFetch netTake(Payload &out) {
  if (stage() != Stage::Done) return NetFetch::None;
  const bool ok = g_fetchOk;
  if (ok) {
    out = g_fetched;
    g_error[0] = '\0';
  } else {
    strlcpy(g_error, g_fetchError, sizeof(g_error));
  }
  setStage(Stage::Idle);
  return ok ? NetFetch::Ok : NetFetch::Failed;
}
