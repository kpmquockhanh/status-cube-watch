# Pomodoro card, WiFi config portal and OTA: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the cube a standalone desk device: change WiFi and bridge settings from a captive portal, flash it over WiFi, and run a Pomodoro timer on it from a dedicated touch-controlled card.

**Architecture:** A new `settings` module (NVS over `config.h` defaults) feeds `net.cpp`, a blocking `portal` module (SoftAP + DNS catch-all + one-page form) and `ota` (ArduinoOTA). The Pomodoro is a pure state machine (`pomodoro.cpp`) plus a pure gesture classifier (`gesture.cpp`), both host-tested. The Pomodoro card is a virtual last card in the deck, drawn by `ui.cpp` through the existing gauge-card renderer (extended with a `RingStyle`). The bridge and payload contract are untouched.

**Tech Stack:** PlatformIO / Arduino-ESP32, LovyanGFX, ArduinoJson, `Preferences` (NVS), `WebServer` + `DNSServer`, `ArduinoOTA`; desktop sim (SDL2, `make`); plain `c++ -std=c++17` host tests.

**Spec:** `docs/superpowers/specs/2026-10-02-pomodoro-portal-ota-design.md`

## Global Constraints

- Firmware stays within C++11 (arduino-esp32's default): no default member initializers in types that are brace-initialized as aggregates, no C++14 library features. The sim and host tests build as C++17.
- Code shared with the simulator (`main.cpp`, `ui.cpp`, `payload.cpp`, `pomodoro.cpp`, `gesture.cpp`) may only use what `firmware/sim/Arduino.h` provides (`millis`, `delay`, `Serial`, `min`/`max`/`constrain`, `<cstring>`). Hardware-only code (`settings.cpp`, `portal.cpp`, `ota.cpp`) gets a sim stand-in in `firmware/sim/`.
- `pomodoro.cpp` and `gesture.cpp` take time as an argument and include no Arduino header, so host tests can build them.
- Settings precedence: NVS value if the key exists, else the `config.h` default. An empty NVS plus a working `config.h` must behave exactly as before.
- Names are fixed by the spec: OTA hostname `claude-cube`; portal AP `claude-cube-XXXX` (last two MAC bytes, upper-case hex), open, address `192.168.4.1`; NVS namespace `cube` with keys `ssid`, `pass`, `bridge`, `otapass`.
- Timings: WiFi join timeout 20 s (80 x 250 ms); boot hold for the portal 5000 ms; portal idle auto-retry 60 s; long-press 600 ms; reset-press 2000 ms; OTA/Pomodoro config constants as below.
- Pomodoro constants live in `config.h`: `POMO_FOCUS_MIN` 25, `POMO_BREAK_MIN` 5, `POMO_LONG_MIN` 15, `POMO_SESSIONS` 4. Minutes must be 1..99 and sessions 1..9 (compile-time `static_assert`). Defaults are also provided in `main.cpp` so an old `config.h` still builds.
- The Pomodoro card is the only card whose text the firmware formats (`MM:SS`). It is not in the payload, not counted against `MAX_CARDS`, and `payload.h/.cpp`, `bridge/cards.mjs` and the payload contract do not change.
- `firmware/src/config.h` is gitignored and holds real credentials: edit it only to add the new constants, never print or commit its secrets.
- This directory is **not a git repository**. Every "Commit" step below is conditional: if `git rev-parse --is-inside-work-tree` succeeds, commit; if not, skip the step (do not run `git init` unless the user asks). Commit messages end with the attribution trailer given in the session's system reminder.
- Verification commands (run from the repo root unless a `cd` is shown):
  - host tests: `cd firmware/sim && make test`
  - firmware build: `cd firmware && pio run`
  - sim build: `cd firmware/sim && make`
  - screenshots: `cd firmware/sim && make build/cube-shot && ./build/cube-shot build/shot @<view>`; open the PNG with the Read tool to look at it.

## Review Focus

Failure modes the spec implies but a straight implementation would not test. Each has a test in the task that owns the code.

1. **A long-press must not also fire a tap or swipe when the finger lifts, and a hold on a non-Pomodoro card must still be ignored on release** (Task 8: `gesture_test`).
2. **A phase must end correctly when `tick` is not called for a long time (a blocked HTTP fetch, a long gap), and when the pause press lands exactly at the end of a phase** (Task 7: `pomodoro_test`).
3. **`millis()` wraparound (about every 49 days) must not corrupt the timer or the gesture timing** (Task 7 and Task 8).
4. **Portal form safety: an SSID or bridge URL containing `"`, `<`, `&` must not break the form; a blank password keeps the stored one only while the SSID is unchanged; bridge URLs with spaces or a non-`http://` scheme are refused** (Task 1: `portal_util_test`).
5. **Bridge unreachable (WiFi fine): the deck must still contain the Pomodoro card, the timer must stay correct, and the alert must still fire** (Task 9: empty-payload shot; Task 10: manual check with a dead `BRIDGE_URL`).

---

### Task 1: Host-test harness and portal helpers

**Files:**
- Create: `firmware/sim/tests/check.h`
- Create: `firmware/sim/tests/portal_util_test.cpp`
- Create: `firmware/src/portal_util.h`
- Modify: `firmware/sim/Makefile`

**Interfaces:**
- Produces (used by Task 4): in `portal_util.h`, all `inline`, namespace-free, taking `std::string`:
  - `bool portalValidSsid(const std::string&)`: 1..32 bytes
  - `bool portalValidBridgeUrl(const std::string&)`: `http://` + non-empty host, <= 127 bytes, no whitespace
  - `std::string portalHtmlEscape(const std::string&)`
  - `std::string portalTrim(const std::string&)`
  - `std::string portalResolvePassword(oldSsid, newSsid, oldPass, newPass)`: new if non-empty; else old only when the SSID is unchanged; else empty
  - `std::string portalKeepIfBlank(old, neu)`: `neu` unless blank, then `old`
- Produces (used by Tasks 7, 8): `check.h` with `CHECK(cond)` and `int checksDone(const char *name)`; the Makefile `test` target and `TESTS` list.

- [ ] **Step 1: Write the test helper**

Create `firmware/sim/tests/check.h`:

```cpp
#pragma once
// Tiny assertion helper for the host-side tests: no framework, one include.
#include <cstdio>

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    g_checks++;                                                              \
    if (!(cond)) {                                                           \
      g_failed++;                                                            \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                        \
  } while (0)

// Prints the summary line and returns the process exit code.
static int checksDone(const char *name) {
  std::printf("%s: %d checks, %d failed\n", name, g_checks, g_failed);
  return g_failed ? 1 : 0;
}
```

- [ ] **Step 2: Write the failing test**

Create `firmware/sim/tests/portal_util_test.cpp`:

```cpp
#include <string>

#include "check.h"
#include "portal_util.h"

namespace {

void testSsid() {
  CHECK(!portalValidSsid(""));
  CHECK(portalValidSsid("a"));
  CHECK(portalValidSsid(std::string(32, 'x')));
  CHECK(!portalValidSsid(std::string(33, 'x')));
}

void testBridgeUrl() {
  CHECK(portalValidBridgeUrl("http://192.168.1.50:8787/api/status"));
  CHECK(portalValidBridgeUrl("http://host"));
  CHECK(portalValidBridgeUrl("http://" + std::string(120, 'a')));   // 127 bytes
  CHECK(!portalValidBridgeUrl("http://" + std::string(121, 'a')));  // 128 bytes
  CHECK(!portalValidBridgeUrl(""));
  CHECK(!portalValidBridgeUrl("http://"));
  CHECK(!portalValidBridgeUrl("http://:8787/x"));
  CHECK(!portalValidBridgeUrl("http:///x"));
  CHECK(!portalValidBridgeUrl("https://host/x"));  // the bridge is plain HTTP
  CHECK(!portalValidBridgeUrl("ftp://host"));
  CHECK(!portalValidBridgeUrl("http://bad host/"));
  CHECK(!portalValidBridgeUrl("http://host/\n"));
  CHECK(!portalValidBridgeUrl("192.168.1.50:8787"));
}

void testEscape() {
  CHECK(portalHtmlEscape("plain") == "plain");
  CHECK(portalHtmlEscape("a\"b") == "a&quot;b");
  CHECK(portalHtmlEscape("<script>") == "&lt;script&gt;");
  CHECK(portalHtmlEscape("Tom & Jerry's") == "Tom &amp; Jerry&#39;s");
  CHECK(portalHtmlEscape("\"><img src=x>") == "&quot;&gt;&lt;img src=x&gt;");
  CHECK(portalHtmlEscape("") == "");
}

void testTrim() {
  CHECK(portalTrim("  http://h/ \r\n") == "http://h/");
  CHECK(portalTrim("x") == "x");
  CHECK(portalTrim("   ") == "");
  CHECK(portalTrim("") == "");
  CHECK(portalTrim(" a b ") == "a b");
}

void testPassword() {
  // A typed password always wins.
  CHECK(portalResolvePassword("home", "home", "old", "new") == "new");
  CHECK(portalResolvePassword("home", "other", "old", "new") == "new");
  // Blank keeps the stored one, but only for the same network.
  CHECK(portalResolvePassword("home", "home", "old", "") == "old");
  // A different network with a blank password is an open network.
  CHECK(portalResolvePassword("home", "cafe", "old", "") == "");
  // No stored password and a blank field stays blank.
  CHECK(portalResolvePassword("", "home", "", "") == "");
}

void testKeepIfBlank() {
  CHECK(portalKeepIfBlank("old", "") == "old");
  CHECK(portalKeepIfBlank("old", "new") == "new");
  CHECK(portalKeepIfBlank("", "") == "");
}

}  // namespace

int main() {
  testSsid();
  testBridgeUrl();
  testEscape();
  testTrim();
  testPassword();
  testKeepIfBlank();
  return checksDone("portal_util_test");
}
```

- [ ] **Step 3: Add the test target to the Makefile**

In `firmware/sim/Makefile`, replace the `.PHONY` line and add the test rules just above `clean:`. Use Edit with:

old:
```make
clean:
	rm -rf build

.PHONY: all run shot clean
```
new:
```make
# Host-side unit tests for the logic that has no hardware in it. A plain
# compiler: no SDL, LovyanGFX or Arduino. Each test links only the sources it
# is given an extra rule for below.
TESTS := build/portal_util_test

build/%_test: tests/%_test.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -Wall -Wextra -I../src -Itests $(filter %.cpp,$^) -o $@

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -rf build

.PHONY: all run shot test clean
```

- [ ] **Step 4: Run it to verify it fails**

Run: `cd firmware/sim && make test`
Expected: FAIL to compile with `'portal_util.h' file not found`.

- [ ] **Step 5: Write the helpers**

Create `firmware/src/portal_util.h`:

```cpp
#pragma once
// Pure helpers for the setup portal. No Arduino or WiFi includes, so the host
// tests in sim/tests can build this with a plain compiler.
#include <string>

// SSIDs are 1..32 bytes. WPA passphrases are 8..63, but an open network has
// none, so the password is only length-checked where it is stored.
inline bool portalValidSsid(const std::string &s) { return !s.empty() && s.size() <= 32; }

// The bridge speaks plain HTTP on the LAN (see net.cpp). Anything else would
// only surface later as a confusing "bad BRIDGE_URL" on the display.
inline bool portalValidBridgeUrl(const std::string &u) {
  static const std::string scheme = "http://";
  if (u.size() > 127 || u.compare(0, scheme.size(), scheme) != 0) return false;
  if (u.find_first_of(" \t\r\n") != std::string::npos) return false;
  const std::string rest = u.substr(scheme.size());
  return !rest.empty() && rest[0] != ':' && rest[0] != '/';
}

// Values go back into the form as value="...", so a quote in an SSID must not
// end the attribute and a "<" must not start a tag.
inline std::string portalHtmlEscape(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

inline std::string portalTrim(const std::string &s) {
  const char *ws = " \t\r\n";
  const size_t a = s.find_first_not_of(ws);
  if (a == std::string::npos) return std::string();
  return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

// A blank password field means "keep what is stored", but only while the SSID
// is unchanged. Pointing the cube at a different network with a blank
// password means an open network, not the old network's password.
inline std::string portalResolvePassword(const std::string &oldSsid, const std::string &newSsid,
                                         const std::string &oldPass, const std::string &newPass) {
  if (!newPass.empty()) return newPass;
  return oldSsid == newSsid ? oldPass : std::string();
}

inline std::string portalKeepIfBlank(const std::string &oldValue, const std::string &newValue) {
  return newValue.empty() ? oldValue : newValue;
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cd firmware/sim && make test`
Expected: `portal_util_test: 33 checks, 0 failed` (the count may differ by a few; the point is `0 failed` and exit status 0).

- [ ] **Step 7: Commit (only if inside a git repo)**

```bash
git add firmware/sim/tests firmware/sim/Makefile firmware/src/portal_util.h
git commit -m "test: host-side test harness and portal helpers"
```

---

### Task 2: Settings (NVS) and the network layer

**Files:**
- Create: `firmware/src/settings.h`
- Create: `firmware/src/settings.cpp`
- Create: `firmware/sim/settings_sim.cpp`
- Modify: `firmware/src/net.h`, `firmware/src/net.cpp`, `firmware/sim/net_sim.cpp`
- Modify: `firmware/src/main.cpp`, `firmware/sim/Makefile`, `firmware/src/config.h.example`

**Interfaces:**
- Produces (used by Tasks 4, 5):
  - `struct Settings { char ssid[33]; char pass[64]; char bridge[128]; char otaPass[64]; }`
  - `void settingsLoad()`: call once at boot
  - `const Settings &settings()`
  - `bool settingsSave(const Settings &s)`: writes NVS and updates the in-memory copy; the caller reboots
  - `bool settingsHaveWifi()`
  - `bool netBegin()` (was `void`): `false` if not connected after about 20 s

- [ ] **Step 1: Write `settings.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

// Everything the setup portal can change. Stored in NVS; any key that was
// never stored falls back to config.h, so a freshly flashed cube behaves
// exactly as it did before the portal existed.
struct Settings {
  char ssid[33];     // WiFi SSID, up to 32 bytes
  char pass[64];     // WPA passphrase, up to 63 bytes; empty = open network
  char bridge[128];  // bridge URL, up to 127 bytes
  char otaPass[64];  // ArduinoOTA password; empty = none
};

void settingsLoad();  // call once at boot, before anything reads settings()
const Settings &settings();
// Persists `s` and makes it the current settings. The caller reboots.
bool settingsSave(const Settings &s);
// True when there is any SSID to join, from NVS or from config.h.
bool settingsHaveWifi();
```

- [ ] **Step 2: Write `settings.cpp`**

```cpp
#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

namespace {

constexpr char NS[] = "cube";
Settings g_settings;

void loadKey(Preferences &p, const char *key, char *dst, size_t cap, const char *fallback) {
  if (p.isKey(key)) {
    p.getString(key, dst, cap);
  } else {
    strlcpy(dst, fallback, cap);
  }
}

void useDefaults() {
  strlcpy(g_settings.ssid, WIFI_SSID, sizeof(g_settings.ssid));
  strlcpy(g_settings.pass, WIFI_PASSWORD, sizeof(g_settings.pass));
  strlcpy(g_settings.bridge, BRIDGE_URL, sizeof(g_settings.bridge));
  strlcpy(g_settings.otaPass, OTA_PASSWORD, sizeof(g_settings.otaPass));
}

}  // namespace

void settingsLoad() {
  Preferences p;
  // Read-write even though we only read: a read-only open of a namespace that
  // does not exist yet fails and logs an error on every first boot.
  if (!p.begin(NS, false)) {
    Serial.println("[settings] NVS unavailable -- using config.h");
    useDefaults();
    return;
  }
  loadKey(p, "ssid", g_settings.ssid, sizeof(g_settings.ssid), WIFI_SSID);
  loadKey(p, "pass", g_settings.pass, sizeof(g_settings.pass), WIFI_PASSWORD);
  loadKey(p, "bridge", g_settings.bridge, sizeof(g_settings.bridge), BRIDGE_URL);
  loadKey(p, "otapass", g_settings.otaPass, sizeof(g_settings.otaPass), OTA_PASSWORD);
  p.end();
}

const Settings &settings() { return g_settings; }

bool settingsSave(const Settings &s) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  // putString returns the byte count, which is 0 for an empty string even on
  // success, so success here just means the namespace opened. An empty value
  // is stored on purpose: a blank password means "open network", not "use the
  // config.h one".
  p.putString("ssid", s.ssid);
  p.putString("pass", s.pass);
  p.putString("bridge", s.bridge);
  p.putString("otapass", s.otaPass);
  p.end();
  g_settings = s;
  return true;
}

bool settingsHaveWifi() { return g_settings.ssid[0] != '\0'; }
```

- [ ] **Step 3: Write the sim stand-in `firmware/sim/settings_sim.cpp`**

```cpp
// Desktop stand-in for settings.cpp: a laptop has no NVS, so the simulator
// always runs on the config.h defaults and saving is a no-op.

#include <Arduino.h>

#include "../src/config.h"
#include "../src/settings.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

namespace {
Settings g_settings;
}

void settingsLoad() {
  strlcpy(g_settings.ssid, WIFI_SSID, sizeof(g_settings.ssid));
  strlcpy(g_settings.pass, WIFI_PASSWORD, sizeof(g_settings.pass));
  strlcpy(g_settings.bridge, BRIDGE_URL, sizeof(g_settings.bridge));
  strlcpy(g_settings.otaPass, OTA_PASSWORD, sizeof(g_settings.otaPass));
}

const Settings &settings() { return g_settings; }

bool settingsSave(const Settings &s) {
  g_settings = s;
  Serial.println("[settings] (sim) not persisted");
  return true;
}

bool settingsHaveWifi() { return g_settings.ssid[0] != '\0'; }
```

- [ ] **Step 4: Make `net` read from settings**

`firmware/src/net.h`: change `void netBegin();` to:

```cpp
// Joins the stored network. Returns false if it is still not connected after
// about 20 s, so the caller can fall back to the setup portal.
bool netBegin();
```

`firmware/src/net.cpp`: replace `#include "config.h"` with `#include "settings.h"`. Replace the whole `netBegin` function with:

```cpp
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
```

and change `http.begin(BRIDGE_URL)` to `http.begin(settings().bridge)`.

`firmware/sim/net_sim.cpp`: change `void netBegin() {` to `bool netBegin() {` and add `return true;` as its last line:

```cpp
bool netBegin() {
  Serial.printf("[net] simulator -> %s\n", bridgeUrl().c_str());
  return true;
}
```

- [ ] **Step 5: Use settings in `main.cpp`**

Edits in `firmware/src/main.cpp`:
1. After `#include "payload.h"` add `#include "settings.h"`.
2. In `setup()`, after `Serial.println("\n[boot] claude-status-cube");` add `settingsLoad();`.
3. Replace `uiMessage(lcd, "CONNECTING", WIFI_SSID);` with `uiMessage(lcd, "CONNECTING", settings().ssid);`.

(`netBegin();` stays as a bare call for now; Task 4 uses its result.)

- [ ] **Step 6: Wire the sim Makefile and document the config**

In `firmware/sim/Makefile` change:

```make
SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp \
        net_sim.cpp touch_sim.cpp sdl_main.cpp
```
to:
```make
SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp \
        net_sim.cpp touch_sim.cpp settings_sim.cpp sdl_main.cpp
```

Append to `firmware/src/config.h.example`:

```c
// Optional: default OTA password. The setup portal can set or change it; with
// neither, OTA still works but anyone on the network can flash the cube.
// #define OTA_PASSWORD  "choose-one"
```

- [ ] **Step 7: Build everything**

Run: `cd firmware && pio run`
Expected: `[SUCCESS]`.

Run: `cd firmware/sim && make`
Expected: `built build/cube-sim` with no errors.

Behaviour is unchanged at this point: with an empty NVS, `settings()` returns the `config.h` values.

- [ ] **Step 8: Commit (only if inside a git repo)**

```bash
git add firmware/src firmware/sim
git commit -m "feat(firmware): NVS-backed settings with config.h fallback"
```

---

### Task 3: Portal screen and non-payload screenshots

**Files:**
- Modify: `firmware/src/ui.h`, `firmware/src/ui.cpp`
- Replace: `firmware/sim/shot.cpp`

**Interfaces:**
- Produces (used by Tasks 4, 5, 9): `void uiPortal(Display &lcd, const char *apName)`; the shot tool's `@name` views (`@portal` now; Tasks 5 and 9 add more through `renderSpecial`).

- [ ] **Step 1: Declare the screen**

Append to `firmware/src/ui.h`:

```cpp
// Setup screen shown while the config portal runs: the cube's own WiFi name
// as text and as a join-QR, and the address of the form.
void uiPortal(Display &lcd, const char *apName);
```

- [ ] **Step 2: Implement it**

Append to `firmware/src/ui.cpp`:

```cpp
void uiPortal(Display &lcd, const char *apName) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  drawCaps(g, "WiFi setup", LCD_WIDTH / 2, 30, g_palette[ACC_ACCENT], middle_center);

  // A WiFi QR: phones offer to join the (open) network when it is scanned.
  // Version 3 is 29 modules, so 116px is a whole 4px per module. The white
  // plate gives the code its quiet zone against the black background.
  char join[64];
  snprintf(join, sizeof(join), "WIFI:T:nopass;S:%s;;", apName);
  constexpr int QR = 116;
  const int qx = (LCD_WIDTH - QR) / 2;
  const int qy = 50;
  g->fillRoundRect(qx - 6, qy - 6, QR + 12, QR + 12, 6, (uint16_t)0xFFFF);
  g->qrcode(join, qx, qy, QR, 3);

  g->setFont(&V_B18.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, BG);
  g->drawString(apName, LCD_WIDTH / 2, 190);

  drawCaps(g, "Join, then open", LCD_WIDTH / 2, 214, DIM, middle_center);
  g->setFont(&V_S12.font);
  g->setTextColor(DIM, BG);
  g->drawString("192.168.4.1", LCD_WIDTH / 2, 234);

  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}
```

- [ ] **Step 3: Replace `firmware/sim/shot.cpp`**

Same PNG writer as before, now with a shared `grab()` and a `renderSpecial()` for screens that are not payload cards. Replace the whole file with:

```cpp
// Headless frame grab. Renders cards through the real ui.cpp and writes PNGs,
// so the layout can be compared against the design without a window (and
// without a screen-recording permission prompt).
//
//   make shot                          # every card of the live bridge payload
//   ./build/cube-shot out card.json    # <out>-0.png, <out>-1.png, ...
//   ./build/cube-shot out @portal      # <out>-portal.png: a screen that is not
//                                      # a payload card (see renderSpecial)
//
// Reads the payload JSON from the file named on the command line, or stdin.

#include <zlib.h>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "../src/display.h"
#include "../src/payload.h"
#include "../src/ui.h"

Display *g_simDisplay = nullptr;  // normally lives in touch_sim.cpp

namespace {

const char *g_prefix = "shot";
const char *g_json = nullptr;
int g_rc = 0;

void chunk(FILE *f, const char *tag, const uint8_t *data, size_t len) {
  const uint32_t n = htonl((uint32_t)len);
  fwrite(&n, 4, 1, f);
  fwrite(tag, 4, 1, f);
  fwrite(data, 1, len, f);
  uLong c = crc32(crc32(0, nullptr, 0), (const Bytef *)tag, 4);
  if (len) c = crc32(c, data, (uInt)len);
  const uint32_t be = htonl((uint32_t)c);
  fwrite(&be, 4, 1, f);
}

// 8-bit RGB, no interlacing, one filter-0 byte per scanline.
bool writePng(const char *path, const uint8_t *rgb, int w, int h) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);

  uint8_t ihdr[13];
  const uint32_t bw = htonl((uint32_t)w), bh = htonl((uint32_t)h);
  memcpy(ihdr, &bw, 4);
  memcpy(ihdr + 4, &bh, 4);
  ihdr[8] = 8;                                   // bit depth
  ihdr[9] = 2;                                   // truecolour
  ihdr[10] = ihdr[11] = ihdr[12] = 0;
  chunk(f, "IHDR", ihdr, sizeof(ihdr));

  std::vector<uint8_t> raw;
  raw.reserve((size_t)h * (1 + (size_t)w * 3));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb + (size_t)y * w * 3, rgb + (size_t)(y + 1) * w * 3);
  }
  uLongf zlen = compressBound(raw.size());
  std::vector<uint8_t> z(zlen);
  if (compress2(z.data(), &zlen, raw.data(), raw.size(), 9) != Z_OK) {
    fclose(f);
    return false;
  }
  chunk(f, "IDAT", z.data(), zlen);
  chunk(f, "IEND", nullptr, 0);
  fclose(f);
  return true;
}

std::string slurp(const char *path) {
  FILE *f = path ? fopen(path, "rb") : stdin;
  if (!f) return "";
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
  if (path) fclose(f);
  return s;
}

std::vector<uint16_t> g_px((size_t)LCD_WIDTH * LCD_HEIGHT);
std::vector<uint8_t> g_rgb((size_t)LCD_WIDTH * LCD_HEIGHT * 3);

// Reads the panel back and writes it as a PNG.
bool grab(Display &lcd, const char *path) {
  lcd.readRect(0, 0, LCD_WIDTH, LCD_HEIGHT, g_px.data());
  for (size_t p = 0; p < g_px.size(); p++) {
    // readRect hands back the panel's own byte order, which is swapped
    // relative to the host's; unswap before unpacking the 565 fields.
    const uint16_t v = (uint16_t)((g_px[p] >> 8) | (g_px[p] << 8));
    g_rgb[p * 3 + 0] = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
    g_rgb[p * 3 + 1] = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
    g_rgb[p * 3 + 2] = (uint8_t)((v & 0x1F) * 255 / 31);
  }
  return writePng(path, g_rgb.data(), LCD_WIDTH, LCD_HEIGHT);
}

// Screens that are not payload cards. Selected with `@name` on the command
// line; the grab goes to <prefix>-<name>.png. Returns false for an unknown name.
bool renderSpecial(Display &lcd, const char *name) {
  if (!strcmp(name, "portal")) {
    uiPortal(lcd, "claude-cube-A1B2");
  } else {
    return false;
  }
  char path[512];
  snprintf(path, sizeof(path), "%s-%s.png", g_prefix, name);
  if (!grab(lcd, path)) {
    fprintf(stderr, "could not write %s\n", path);
    g_rc = 1;
  } else {
    printf("%s\n", path);
  }
  return true;
}

int render(bool *running) {
  static Display lcd;
  lcd.init();
  lcd.setRotation(0);
  uiBegin(lcd);

  if (g_json && g_json[0] == '@') {
    if (!renderSpecial(lcd, g_json + 1)) {
      fprintf(stderr, "unknown view: %s\n", g_json);
      g_rc = 1;
    }
    *running = false;
    fflush(stdout);
    _exit(g_rc);
  }

  JsonDocument doc;
  Payload payload{};
  const std::string text = slurp(g_json);
  char err[64] = "";
  if (deserializeJson(doc, text) != DeserializationError::Ok ||
      !payloadFromJson(doc, payload, err, sizeof(err))) {
    fprintf(stderr, "bad payload: %s\n", err[0] ? err : "not JSON");
    g_rc = 1;
    *running = false;
    return 1;
  }

  for (uint8_t i = 0; i < payload.nCards; i++) {
    // Draw until the entry animation has settled, so the grab shows the
    // resting layout rather than a frame part-way through the sweep.
    for (int frame = 0; frame < 400; frame++) {
      uiRender(lcd, payload, i, true, 4000);
      if (!uiAnimating()) break;
      delay(8);
    }
    char path[512];
    snprintf(path, sizeof(path), "%s-%u.png", g_prefix, (unsigned)i);
    if (!grab(lcd, path)) {
      fprintf(stderr, "could not write %s\n", path);
      g_rc = 1;
    } else {
      printf("%s  (%s)\n", path, payload.cards[i].title);
    }
  }

  // Panel_sdl::main keeps pumping its event loop after `running` clears, and
  // there is nothing left to pump for: leave straight from here.
  *running = false;
  fflush(stdout);
  _exit(g_rc);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc > 1) g_prefix = argv[1];
  if (argc > 2) g_json = argv[2];
  // SDL still backs the panel, but a dummy video driver keeps it off-screen.
  setenv("SDL_VIDEODRIVER", "dummy", 0);
  lgfx::Panel_sdl::main(render);
  return g_rc;
}
```

- [ ] **Step 4: Render it and look at it**

Run:
```bash
cd firmware/sim && make build/cube-shot && ./build/cube-shot build/shot @portal
```
Expected: prints `build/shot-portal.png`.

Open `firmware/sim/build/shot-portal.png` with the Read tool. Check: title "WIFI SETUP" at the top, a black-on-white QR code on a white plate with a clear border, `claude-cube-A1B2` below it, "JOIN, THEN OPEN", and `192.168.4.1` at the bottom; nothing clipped by the rounded corners; the QR is not blank or garbled. If the QR has no quiet zone or looks wrong, adjust the plate padding in `uiPortal` and re-render.

Also confirm an unknown view fails: `./build/cube-shot build/shot @nope; echo $?` prints `unknown view: @nope` and `1`.

- [ ] **Step 5: Commit (only if inside a git repo)**

```bash
git add firmware/src/ui.h firmware/src/ui.cpp firmware/sim/shot.cpp
git commit -m "feat(ui): portal setup screen and @view screenshots"
```

---

### Task 4: Portal server and boot flow

**Files:**
- Create: `firmware/src/portal.h`, `firmware/src/portal.cpp`, `firmware/sim/portal_sim.cpp`
- Modify: `firmware/src/main.cpp`, `firmware/sim/Makefile`

**Interfaces:**
- Consumes: `settings()`, `settingsSave`, `settingsHaveWifi`, `netBegin()` (Task 2); `uiPortal` (Task 3); `portal*` helpers (Task 1).
- Produces: `[[noreturn]] void portalRun(Display &lcd, bool autoRetry)`.

- [ ] **Step 1: Write `portal.h`**

```cpp
#pragma once
#include "display.h"

// Runs the setup portal: the cube starts its own open WiFi network
// (claude-cube-XXXX), serves a one-page form at 192.168.4.1 and answers every
// DNS query with its own address so phones pop the page up by themselves.
// Never returns: saving the form reboots the board.
//
// autoRetry: reboot after a minute with nobody joined, so a cube that merely
// lost its WiFi (power cut, router still booting) goes back to retrying the
// stored network by itself. Pass false when the user asked for the portal.
[[noreturn]] void portalRun(Display &lcd, bool autoRetry);
```

- [ ] **Step 2: Write `portal.cpp`**

```cpp
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
  const std::string ota = portalKeepIfBlank(cur.otaPass, server.arg("otapass").c_str());
  if (pass.size() > 63 || ota.size() > 63) return sendForm(400, "Passwords can be at most 63 characters.");

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
```

- [ ] **Step 3: Write the sim stand-in `firmware/sim/portal_sim.cpp`**

```cpp
// Desktop stand-in for portal.cpp: a laptop cannot host an access point, so
// show the setup screen and sit on it. The simulator's netBegin() always
// succeeds, so this is only reached by holding the mouse at startup.

#include <Arduino.h>

#include "../src/portal.h"
#include "../src/ui.h"

[[noreturn]] void portalRun(Display &lcd, bool) {
  uiPortal(lcd, "claude-cube-SIM0");
  for (;;) delay(100);
}
```

- [ ] **Step 4: Wire the boot flow into `main.cpp`**

Edits in `firmware/src/main.cpp`:

1. After `#include "payload.h"` (and the `settings.h` include from Task 2) add `#include "portal.h"`.

2. In the anonymous namespace, after the `TAP_MAX_MS` constant, add:

```cpp
constexpr uint32_t SETUP_HOLD_MS = 5000;

// Holding the screen while the cube powers up forces the setup portal, so a
// working cube can be pointed at a new network without a reflash. Only a
// finger that is already down at boot counts: that way a normal boot never
// waits for one.
bool setupHoldRequested() {
  int16_t x, y;
  if (!touch.read(x, y)) return false;
  uiMessage(lcd, "KEEP HOLDING", "FOR WIFI SETUP");
  const uint32_t t0 = millis();
  uint8_t misses = 0;
  while (millis() - t0 < SETUP_HOLD_MS) {
    misses = touch.read(x, y) ? 0 : misses + 1;
    if (misses > 5) return false;  // a few dropped reads are not a release
    delay(20);
  }
  return true;
}
```

3. In `setup()`, replace this block:

```cpp
  uiBegin(lcd);
  uiMessage(lcd, "CONNECTING", settings().ssid);

  touch.begin();
  netBegin();
```
with:
```cpp
  uiBegin(lcd);

  touch.begin();

  // The setup portal is asked for by holding the screen, needed when there is
  // nothing to join, and fallen back to when the stored network cannot be
  // reached. Only the last case retries by itself.
  if (setupHoldRequested() || !settingsHaveWifi()) portalRun(lcd, false);
  uiMessage(lcd, "CONNECTING", settings().ssid);
  if (!netBegin()) portalRun(lcd, true);
```

- [ ] **Step 5: Add the stand-in to the sim build**

In `firmware/sim/Makefile`:

```make
SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp \
        net_sim.cpp touch_sim.cpp settings_sim.cpp portal_sim.cpp sdl_main.cpp
```

- [ ] **Step 6: Build**

Run: `cd firmware && pio run` -> `[SUCCESS]`.
Run: `cd firmware/sim && make && make test` -> sim builds; tests pass.
Run: `cd firmware/sim && make run` and check the cube still shows the normal deck (close the window afterwards).

- [ ] **Step 7: Verify on hardware (manual; report results to the user)**

Flash with `cd firmware && pio run -t upload && pio device monitor`. Then:

1. **Normal boot**, config.h credentials, empty NVS: connects, shows the deck as before.
2. **Forced portal:** power-cycle while holding a finger on the screen. "KEEP HOLDING / FOR WIFI SETUP" appears; after 5 s the portal screen shows. Releasing before 5 s boots normally.
3. **Join and save:** on a phone join `claude-cube-XXXX`; the setup page should open by itself (else browse to `192.168.4.1`). Change nothing but press Save: the cube reboots and reconnects (blank password kept). Then change the bridge URL to a bad one such as `https://x`: the form refuses with an error and keeps what you typed.
4. **Wrong WiFi:** save a wrong password; after reboot it spends ~20 s on "CONNECTING", then shows the portal screen again.
5. **Auto-retry:** with the wrong password saved and nobody joining the AP, the cube reboots about a minute after the portal appears, and tries again. While a phone is joined to the AP it does not reboot.
6. **Restore** the correct settings through the portal.

- [ ] **Step 8: Commit (only if inside a git repo)**

```bash
git add firmware/src firmware/sim
git commit -m "feat(firmware): captive setup portal with boot-hold and wifi fallback"
```

---

### Task 5: OTA updates

**Files:**
- Create: `firmware/src/ota.h`, `firmware/src/ota.cpp`, `firmware/sim/ota_sim.cpp`
- Modify: `firmware/src/ui.h`, `firmware/src/ui.cpp`, `firmware/src/main.cpp`
- Modify: `firmware/sim/shot.cpp`, `firmware/sim/Makefile`, `firmware/platformio.ini`

**Interfaces:**
- Consumes: `settings().otaPass` (Task 2).
- Produces: `void otaBegin(Display &lcd)`, `void otaHandle()`, `void uiOta(Display &lcd, uint8_t percent)`.
- Note: `ArduinoOTA.handle()` runs the whole transfer inside the call, so the main loop is already paused while an image arrives. There is therefore no `otaBusy()`.

- [ ] **Step 1: Add the progress screen**

Append to `firmware/src/ui.h`:

```cpp
// Progress screen while an OTA image is arriving.
void uiOta(Display &lcd, uint8_t percent);
```

Append to `firmware/src/ui.cpp`:

```cpp
void uiOta(Display &lcd, uint8_t percent) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  constexpr int W = 160, H = 8, X = (LCD_WIDTH - W) / 2, Y = 150;
  drawCaps(g, "Updating", LCD_WIDTH / 2, 100, g_palette[ACC_ACCENT], middle_center);

  g->fillRoundRect(X, Y, W, H, H / 2, FAINT);
  const int pct = percent > 100 ? 100 : percent;
  const int fill = W * pct / 100;
  // Never narrower than the bar is tall, or the rounded ends degenerate.
  if (fill > 0) g->fillRoundRect(X, Y, fill < H ? H : fill, H, H / 2, g_palette[ACC_ACCENT]);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  g->setFont(&V_B24.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, BG);
  g->drawString(buf, LCD_WIDTH / 2, 190);

  drawCaps(g, "Do not unplug", LCD_WIDTH / 2, 230, DIM, middle_center);
  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}
```

In `firmware/sim/shot.cpp`, in `renderSpecial`, extend the chain:

```cpp
  if (!strcmp(name, "portal")) {
    uiPortal(lcd, "claude-cube-A1B2");
  } else if (!strcmp(name, "ota")) {
    uiOta(lcd, 62);
  } else {
    return false;
  }
```

- [ ] **Step 2: Look at it**

Run: `cd firmware/sim && make build/cube-shot && ./build/cube-shot build/shot @ota`, then open `build/shot-ota.png`. Check: "UPDATING", a rounded bar about 62% filled in the accent colour, "62%", and "DO NOT UNPLUG". Also render `uiOta(lcd, 0)` and `uiOta(lcd, 100)` once by temporarily editing the literal, to see the empty and full bar; restore it to 62 afterwards.

- [ ] **Step 3: Write `ota.h`**

```cpp
#pragma once
#include "display.h"

// Over-the-air flashing: after one USB flash, `pio run -t upload` with
// upload_protocol = espota reaches the cube as claude-cube.local.
//
// otaBegin once WiFi is up; otaHandle every loop. A transfer runs entirely
// inside otaHandle(), so the loop (polling, drawing) is paused while an image
// arrives without anyone having to arrange it.
void otaBegin(Display &lcd);
void otaHandle();
```

- [ ] **Step 4: Write `ota.cpp`**

```cpp
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
```

- [ ] **Step 5: Write the sim stand-in `firmware/sim/ota_sim.cpp`**

```cpp
// Desktop stand-in for ota.cpp: there is nothing to flash in a simulator.

#include "../src/ota.h"

void otaBegin(Display &) {}
void otaHandle() {}
```

- [ ] **Step 6: Hook into `main.cpp` and the sim build**

Edits in `firmware/src/main.cpp`:
1. After `#include "net.h"` add `#include "ota.h"`.
2. In `setup()`, right after `if (!netBegin()) portalRun(lcd, true);` add `otaBegin(lcd);`.
3. In `loop()`, make the first lines:

```cpp
void loop() {
  otaHandle();
  const uint32_t now = millis();
```

In `firmware/sim/Makefile`:

```make
SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp \
        net_sim.cpp touch_sim.cpp settings_sim.cpp portal_sim.cpp ota_sim.cpp sdl_main.cpp
```

- [ ] **Step 7: `platformio.ini`**

Edit the header comment: replace

```
;   ESP32-S3R2 (2 MB quad PSRAM, 16 MB flash)
```
with
```
;   ESP32-S3R8 (8 MB octal PSRAM, 16 MB flash)
;   (nothing here uses PSRAM yet; if that changes, note that the R8's PSRAM is
;   octal and the memory_type below is the quad-PSRAM setting -- see qio_opi)
```

After the `upload_speed = 921600` line add:

```
; Over the air, after the first USB flash: uncomment these, then plain
; `pio run -t upload` reaches the cube. --auth is the OTA password from the
; setup portal (drop the line if none is set).
; upload_protocol = espota
; upload_port = claude-cube.local
; upload_flags = --auth=YOUR_OTA_PASSWORD
```

- [ ] **Step 8: Build and test on hardware**

Run: `cd firmware && pio run` -> `[SUCCESS]`; `cd firmware/sim && make` -> builds.

On hardware (manual; report to the user):
1. Flash by USB. The serial log shows `[ota] ready as claude-cube.local` (and the no-password warning if none is set).
2. Set an OTA password in the portal (hold the screen at boot, save the form).
3. Uncomment the three OTA lines with the password, make a visible change (e.g. change a label), run `pio run -t upload`. The screen shows the progress bar, then "UPDATED", then reboots into the new build.
4. Run the upload again with a wrong `--auth`: it is rejected and the cube keeps running the old image.
5. Re-comment the OTA lines if you want USB uploads again.

- [ ] **Step 9: Commit (only if inside a git repo)**

```bash
git add firmware/src firmware/sim firmware/platformio.ini
git commit -m "feat(firmware): ArduinoOTA push updates with progress screen"
```

---

### Task 6: Ring style (notch-less rings), pixel-identical for existing cards

**Files:**
- Create: `firmware/sim/fixtures/ring.json`
- Modify: `firmware/src/ui.cpp`

**Interfaces:**
- Produces (used by Tasks 9, 10): in `ui.cpp`'s anonymous namespace, `struct RingStyle { uint32_t rgb; bool pct; bool notches; float flash; }`, `RingStyle ringStyle(uint32_t rgb)`, `drawGaugeCard(LovyanGFX *g, const Card &card, const RingStyle &style, ValueFont valueFont, GaugeAnim &a, uint32_t now, bool online, uint32_t ageMs)`, `drawRing(g, pct, rgb, filled, notches)`.
- Contract: with `notches = true`, `pct = true`, `flash = 0` the output is byte-identical to today.

- [ ] **Step 1: Create the fixture**

`firmware/sim/fixtures/ring.json` (eight cards, the deck maximum, covering both sides of each notch, empty, full, a blank ring and a text card):

```json
{"v":1,"ts":0,"src":"oauth","est":false,"cards":[
 {"t":"5H LIMIT","v":"4h 10m","s1":"resets in","s2":"of 5h limit","c":"green","g":0},
 {"t":"5H LIMIT","v":"3h 05m","s1":"resets in","s2":"of 5h limit","c":"green","g":14},
 {"t":"5H LIMIT","v":"2h 20m","s1":"resets in","s2":"of 5h limit","c":"green","g":59},
 {"t":"7D LIMIT","v":"3d 02h","s1":"resets in","s2":"of 7d limit","c":"amber","g":60},
 {"t":"7D LIMIT","v":"1d 08h","s1":"resets in","s2":"of 7d limit","c":"amber","g":84},
 {"t":"7D LIMIT","v":"0d 06h","s1":"resets in","s2":"of 7d limit","c":"red","g":86},
 {"t":"7D LIMIT","v":"--","s1":"no reading","s2":"","c":"ink","g":-1},
 {"t":"TODAY","v":"$12.40","s1":"34k tokens","s2":"est. spend","c":"accent"}
]}
```

- [ ] **Step 2: Take the baseline before touching `ui.cpp`**

Run:
```bash
cd firmware/sim && make build/cube-shot && ./build/cube-shot build/base fixtures/ring.json
```
Expected: eight lines `build/base-0.png` ... `build/base-7.png`. These are the reference images.

- [ ] **Step 3: Add `covNo` to the ring table**

In `firmware/src/ui.cpp`, in `struct RingPx` add a field after `int16_t d;`:

```cpp
  uint8_t covNo;   // the same coverage with the notches left out, for rings without thresholds
```

In `buildRingTable()`, replace

```cpp
        float cov = covR * clamp01(fminf(d, ARC_SWEEP - d) * px + 0.5f);
        // Threshold notches: a ~2px gap in the background colour.
        for (float f : NOTCHES) {
          cov *= clamp01(fabsf(d - ARC_SWEEP * f) * px - NOTCH_HALF_DEG * px + 0.5f);
        }
        if (cov <= 0.0f) continue;
```
with
```cpp
        const float covPlain = covR * clamp01(fminf(d, ARC_SWEEP - d) * px + 0.5f);
        float cov = covPlain;
        // Threshold notches: a ~2px gap in the background colour.
        for (float f : NOTCHES) {
          cov *= clamp01(fabsf(d - ARC_SWEEP * f) * px - NOTCH_HALF_DEG * px + 0.5f);
        }
        // A pixel that only a notch removes still goes in the table: a ring
        // drawn without notches needs it. With notches its coverage is 0 and
        // it paints background, exactly as leaving it out did.
        if (covPlain <= 0.0f) continue;
```
and in the `if (pass == 1)` block, after `p.base = pack565(RING_TRACK, cov);` add:

```cpp
          p.covNo = (uint8_t)(covPlain * 255.0f + 0.5f);
```

- [ ] **Step 4: Teach `drawRing` about notches**

Replace the whole `drawRing` function with:

```cpp
void drawRing(LovyanGFX *g, float pct, uint32_t rgb, bool filled, bool notches) {
  if (!g_ringPx) return;
  const float fillEnd = filled ? ARC_SWEEP * (pct / 100.0f) : -10.0f;
  // Past this the fill (and its AA edge) cannot reach: use the cached pixel.
  const int past = (int)((fillEnd + 1.5f) * 16.0f);
  uint16_t *buf = g_sprite ? (uint16_t *)g_canvas.getBuffer() : nullptr;

  for (int i = 0; i < g_ringN; i++) {
    const RingPx &p = g_ringPx[i];
    const uint8_t cov = notches ? p.cov : p.covNo;
    // `base` was cached with the notches cut out; without them it is rebuilt.
    uint16_t c = notches ? p.base : pack565(RING_TRACK, p.covNo * (1.0f / 255.0f));
    if (p.d <= past) {
      const float d = p.d * (1.0f / 16.0f);
      const float t = clamp01((fillEnd - d) * RING_PX_PER_DEG + 0.5f) * (p.fs * (1.0f / 255.0f));
      const uint32_t col = lerpRgb(RING_TRACK, rgb, t);
      c = pack565(col, cov * (1.0f / 255.0f));
    }
    if (buf) buf[p.idx] = c;  // sprite memory is big-endian 565
    else g->drawPixel(p.idx % LCD_WIDTH, p.idx / LCD_WIDTH, __builtin_bswap16(c));
  }
}
```

- [ ] **Step 5: Add `RingStyle` and use it in `drawGaugeCard`**

Replace the line

```cpp
// --- the two card layouts ------------------------------------------------
```
with:
```cpp
// How one ring card is dressed. Payload cards take their colour from the
// payload and show everything; the Pomodoro card picks its own colour, has no
// percentage (the ring is time left, not a limit) and no thresholds.
struct RingStyle {
  uint32_t rgb;   // arc colour
  bool pct;       // "NN%" and its label in the gap at the bottom; off = the label alone
  bool notches;   // cuts at the amber/red thresholds
  float flash;    // 0..1 blend of the arc towards white (the Pomodoro phase-end pulse)
};

RingStyle ringStyle(uint32_t rgb) {
  RingStyle s;
  s.rgb = rgb;
  s.pct = true;
  s.notches = true;
  s.flash = 0.0f;
  return s;
}

// --- the two card layouts ------------------------------------------------
```

Replace the head of `drawGaugeCard`:

```cpp
void drawGaugeCard(LovyanGFX *g, const Card &card, ValueFont valueFont,
                   GaugeAnim &a, uint32_t now,
                   bool online, uint32_t ageMs) {
  const bool hasReading = card.gauge >= 0;
  const uint32_t rgb = RGB888[card.color % 7];
```
with:
```cpp
void drawGaugeCard(LovyanGFX *g, const Card &card, const RingStyle &style,
                   ValueFont valueFont, GaugeAnim &a, uint32_t now,
                   bool online, uint32_t ageMs) {
  const bool hasReading = card.gauge >= 0;
  const uint32_t rgb = style.rgb;
```

Replace

```cpp
  drawTopBar(g, card.title, online, ageMs);
  drawRing(g, a.shown, shownRgb, hasReading);
```
with:
```cpp
  drawTopBar(g, card.title, online, ageMs);
  const uint32_t ringRgb = style.flash > 0.0f ? lerpRgb(shownRgb, 0xFFFFFF, style.flash) : shownRgb;
  drawRing(g, a.shown, ringRgb, hasReading, style.notches);
```

Replace

```cpp
  if (hasReading) {
    char pct[8];
```
with:
```cpp
  if (hasReading && style.pct) {
    char pct[8];
```

In `uiRender`, replace

```cpp
    drawGaugeCard(g, card, valueFont, g_anim[i], millis(), online, ageMs);
```
with:
```cpp
    drawGaugeCard(g, card, ringStyle(RGB888[card.color % 7]), valueFont, g_anim[i],
                  millis(), online, ageMs);
```

- [ ] **Step 6: Prove nothing changed**

Run:
```bash
cd firmware/sim && make build/cube-shot && ./build/cube-shot build/after fixtures/ring.json
for i in 0 1 2 3 4 5 6 7; do cmp build/base-$i.png build/after-$i.png && echo "card $i identical"; done
```
Expected: `card 0 identical` ... `card 7 identical`, no `differ` lines. If any card differs, the refactor changed output: find out why before continuing (do not "update the baseline").

Run: `cd firmware && pio run` -> `[SUCCESS]`.

Also confirm the new path works at all: it is exercised in Task 9. For now, `drawRing(..., false)` has no caller, which is expected.

- [ ] **Step 7: Commit (only if inside a git repo)**

```bash
git add firmware/src/ui.cpp firmware/sim/fixtures
git commit -m "refactor(ui): RingStyle and notch-less rings, output unchanged"
```

---

### Task 7: Pomodoro state machine

**Files:**
- Create: `firmware/src/pomodoro.h`, `firmware/src/pomodoro.cpp`
- Create: `firmware/sim/tests/pomodoro_test.cpp`
- Modify: `firmware/sim/Makefile`

**Interfaces:**
- Consumes: `check.h`, the `make test` target (Task 1).
- Produces (used by Tasks 9, 10):
  - `enum PomoState : uint8_t { POMO_IDLE, POMO_FOCUS, POMO_BREAK, POMO_PAUSED, POMO_DONE }`
  - `enum PomoPhase : uint8_t { PHASE_FOCUS, PHASE_SHORT, PHASE_LONG }`
  - `struct PomoConfig { uint32_t focusMs, shortMs, longMs; uint8_t sessions; }`
  - `struct PomoView { PomoState state; PomoPhase phase; PomoPhase next; uint32_t leftMs; uint8_t fraction; uint8_t completed; uint8_t sessions; uint32_t displaySec; }`
  - `class Pomodoro { explicit Pomodoro(const PomoConfig&); void tick(uint32_t now); void longPress(uint32_t now); void reset(); bool takeAlert(); PomoView view() const; }`
  - `void pomoFormatTime(uint32_t sec, char *buf, size_t cap)`: `MM:SS`, clamped to `99:59`
- Semantics: `phase` is IDLE: focus; FOCUS/BREAK/PAUSED: the phase in progress; DONE: the phase that just ended. `next` is what a long-press starts from DONE. `fraction` is time left in the phase, 0..100. `completed` counts finished focus sessions in the current set and resets when a long break ends. `displaySec` is `leftMs` rounded up to whole seconds.

- [ ] **Step 1: Write the failing test**

Create `firmware/sim/tests/pomodoro_test.cpp`:

```cpp
#include <cstdint>
#include <cstring>

#include "check.h"
#include "pomodoro.h"

namespace {

constexpr uint32_t MIN = 60000;
const PomoConfig CFG{25 * MIN, 5 * MIN, 15 * MIN, 4};

// A fake clock around one Pomodoro, the way main.cpp drives the real one.
struct Rig {
  Pomodoro p{CFG};
  uint32_t now;
  explicit Rig(uint32_t start = 1000) : now(start) {}
  void advance(uint32_t ms) {
    now += ms;
    p.tick(now);
  }
  void press() { p.longPress(now); }
};

void testIdle() {
  Rig r;
  const PomoView v = r.p.view();
  CHECK(v.state == POMO_IDLE);
  CHECK(v.phase == PHASE_FOCUS);
  CHECK(v.fraction == 100);
  CHECK(v.displaySec == 1500);
  CHECK(v.completed == 0);
  CHECK(v.sessions == 4);
  r.advance(180 * MIN);  // an idle timer does not run
  CHECK(r.p.view().state == POMO_IDLE);
  CHECK(!r.p.takeAlert());
}

void testCountdown() {
  Rig r;
  r.press();
  CHECK(r.p.view().state == POMO_FOCUS);
  r.advance(1);
  CHECK(r.p.view().displaySec == 1500);  // rounds up: 25:00 for the first second
  r.advance(999);
  CHECK(r.p.view().displaySec == 1499);
  r.advance(59 * 1000);  // 60 s in
  CHECK(r.p.view().displaySec == 1440);
  CHECK(r.p.view().fraction == 96);
}

void testFocusEnds() {
  Rig r;
  r.press();
  r.advance(25 * MIN - 1);
  CHECK(r.p.view().state == POMO_FOCUS);
  CHECK(!r.p.takeAlert());
  r.advance(1);
  const PomoView v = r.p.view();
  CHECK(v.state == POMO_DONE);
  CHECK(v.phase == PHASE_FOCUS);
  CHECK(v.next == PHASE_SHORT);
  CHECK(v.completed == 1);
  CHECK(v.fraction == 0);
  CHECK(v.displaySec == 0);
  CHECK(r.p.takeAlert());
  CHECK(!r.p.takeAlert());  // once per phase end
}

// Review Focus 2: tick can be starved for seconds by a blocking HTTP fetch.
void testHugeGap() {
  Rig r;
  r.press();
  r.advance(5 * 60 * MIN);
  CHECK(r.p.view().state == POMO_DONE);
  CHECK(r.p.view().completed == 1);
  CHECK(r.p.takeAlert());
  r.advance(60 * MIN);  // DONE does not run on or count again
  CHECK(r.p.view().completed == 1);
  CHECK(!r.p.takeAlert());
}

void testBreakCycle() {
  Rig r;
  r.press();
  r.advance(25 * MIN);
  CHECK(r.p.takeAlert());
  r.press();  // start the break
  PomoView v = r.p.view();
  CHECK(v.state == POMO_BREAK);
  CHECK(v.phase == PHASE_SHORT);
  CHECK(v.leftMs == 5 * MIN);
  CHECK(v.completed == 1);
  r.advance(5 * MIN);
  v = r.p.view();
  CHECK(v.state == POMO_DONE);
  CHECK(v.phase == PHASE_SHORT);
  CHECK(v.next == PHASE_FOCUS);
  CHECK(v.completed == 1);
  CHECK(r.p.takeAlert());
}

void testFullSetAndLongBreak() {
  Rig r;
  for (int i = 1; i <= 4; i++) {
    r.press();  // start focus (from IDLE, then from DONE)
    r.advance(25 * MIN);
    CHECK(r.p.view().state == POMO_DONE);
    CHECK(r.p.view().completed == i);
    if (i < 4) {
      CHECK(r.p.view().next == PHASE_SHORT);
      r.press();
      r.advance(5 * MIN);
      CHECK(r.p.view().next == PHASE_FOCUS);
    }
  }
  CHECK(r.p.view().next == PHASE_LONG);
  CHECK(r.p.view().completed == 4);

  r.press();
  PomoView v = r.p.view();
  CHECK(v.state == POMO_BREAK);
  CHECK(v.phase == PHASE_LONG);
  CHECK(v.leftMs == 15 * MIN);
  CHECK(v.completed == 4);  // the set counts as complete while the long break runs

  r.advance(15 * MIN);
  v = r.p.view();
  CHECK(v.state == POMO_DONE);
  CHECK(v.completed == 0);  // and starts over once it ends
  CHECK(v.next == PHASE_FOCUS);
}

void testPauseAndResume() {
  Rig r;
  r.press();
  r.advance(60000);
  r.press();
  CHECK(r.p.view().state == POMO_PAUSED);
  const uint32_t left = r.p.view().leftMs;
  r.advance(10 * MIN);  // paused time is not counted
  CHECK(r.p.view().leftMs == left);
  CHECK(!r.p.takeAlert());
  r.press();
  CHECK(r.p.view().state == POMO_FOCUS);
  r.advance(60000);
  CHECK(r.p.view().leftMs == 25 * MIN - 120000);
}

void testPauseDuringBreakResumesBreak() {
  Rig r;
  r.press();
  r.advance(25 * MIN);
  r.press();  // break
  r.advance(60000);
  r.press();  // pause
  CHECK(r.p.view().state == POMO_PAUSED);
  CHECK(r.p.view().phase == PHASE_SHORT);
  r.press();  // resume
  CHECK(r.p.view().state == POMO_BREAK);
}

// Review Focus 2: the pause press lands after the phase has actually ended.
void testPressAtTheEnd() {
  Rig r;
  r.press();
  r.advance(25 * MIN - 1);
  r.now += 1;
  r.p.longPress(r.now);  // elapsed == left: the phase is over, so this is not a pause
  CHECK(r.p.view().state == POMO_DONE);
  CHECK(r.p.takeAlert());
}

void testReset() {
  Rig r;
  r.press();
  r.advance(25 * MIN);  // DONE with an alert nobody has taken
  r.press();            // break, completed == 1
  r.p.reset();
  const PomoView v = r.p.view();
  CHECK(v.state == POMO_IDLE);
  CHECK(v.completed == 0);
  CHECK(v.leftMs == 25 * MIN);
  CHECK(v.phase == PHASE_FOCUS);
  CHECK(!r.p.takeAlert());
  r.press();  // and it starts again from the top
  CHECK(r.p.view().state == POMO_FOCUS);
}

void testResetClearsPendingAlert() {
  Rig r;
  r.press();
  r.advance(25 * MIN);  // alert pending
  r.p.reset();
  CHECK(!r.p.takeAlert());
}

// Review Focus 3: millis() wraps every ~49 days.
void testClockWrap() {
  Rig r(0xFFFFFF00u);
  r.press();
  r.advance(1000);  // crosses the wrap
  CHECK(r.p.view().state == POMO_FOCUS);
  CHECK(r.p.view().leftMs == 25 * MIN - 1000);
}

void testCustomConfig() {
  const PomoConfig cfg{1000, 500, 1500, 2};
  Pomodoro p(cfg);
  p.longPress(0);
  p.tick(1000);
  CHECK(p.view().state == POMO_DONE);
  CHECK(p.view().completed == 1);
  CHECK(p.view().next == PHASE_SHORT);
  p.longPress(1000);
  p.tick(1500);
  CHECK(p.view().next == PHASE_FOCUS);
  p.longPress(1500);
  p.tick(2500);
  CHECK(p.view().completed == 2);
  CHECK(p.view().next == PHASE_LONG);  // sessions is configurable
}

void testFormat() {
  char b[8];
  pomoFormatTime(1500, b, sizeof(b));
  CHECK(!strcmp(b, "25:00"));
  pomoFormatTime(0, b, sizeof(b));
  CHECK(!strcmp(b, "00:00"));
  pomoFormatTime(59, b, sizeof(b));
  CHECK(!strcmp(b, "00:59"));
  pomoFormatTime(3599, b, sizeof(b));
  CHECK(!strcmp(b, "59:59"));
  pomoFormatTime(5999, b, sizeof(b));
  CHECK(!strcmp(b, "99:59"));
  pomoFormatTime(6000, b, sizeof(b));
  CHECK(!strcmp(b, "99:59"));  // clamped, never wider than five characters
  pomoFormatTime(0xFFFFFFFFu, b, sizeof(b));
  CHECK(!strcmp(b, "99:59"));
}

}  // namespace

int main() {
  testIdle();
  testCountdown();
  testFocusEnds();
  testHugeGap();
  testBreakCycle();
  testFullSetAndLongBreak();
  testPauseAndResume();
  testPauseDuringBreakResumesBreak();
  testPressAtTheEnd();
  testReset();
  testResetClearsPendingAlert();
  testClockWrap();
  testCustomConfig();
  testFormat();
  return checksDone("pomodoro_test");
}
```

- [ ] **Step 2: Register the test and run it to see it fail**

In `firmware/sim/Makefile` change the test block to:

```make
TESTS := build/portal_util_test build/pomodoro_test

build/pomodoro_test: ../src/pomodoro.cpp
```
(the second line goes right after the `TESTS` line; it only adds a prerequisite, the recipe comes from the `build/%_test` rule).

Run: `cd firmware/sim && make test`
Expected: `portal_util_test` passes, then the build of `pomodoro_test` fails (`pomodoro.h` / `pomodoro.cpp` missing).

- [ ] **Step 3: Write `pomodoro.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

// Pomodoro timer state machine. Pure logic: time comes in as an argument
// (millis() on the board, a fake clock in sim/tests), so the whole cycle can
// be checked on the host. It never starts a phase by itself -- every phase
// begins with a long-press, so it cannot start a focus session while you are
// away from the desk.

enum PomoState : uint8_t { POMO_IDLE, POMO_FOCUS, POMO_BREAK, POMO_PAUSED, POMO_DONE };
enum PomoPhase : uint8_t { PHASE_FOCUS, PHASE_SHORT, PHASE_LONG };

struct PomoConfig {
  uint32_t focusMs, shortMs, longMs;
  uint8_t sessions;  // focus sessions before the long break
};

// What the UI needs to draw one frame.
struct PomoView {
  PomoState state;
  // IDLE: focus (what a start would begin). FOCUS/BREAK/PAUSED: the phase in
  // progress. DONE: the phase that just ended.
  PomoPhase phase;
  PomoPhase next;       // what a long-press starts from DONE
  uint32_t leftMs;
  uint8_t fraction;     // time left in the phase, 0..100
  uint8_t completed;    // focus sessions finished in this set; resets after the long break
  uint8_t sessions;
  uint32_t displaySec;  // leftMs rounded UP to whole seconds: 25:00 for the first second
};

class Pomodoro {
 public:
  explicit Pomodoro(const PomoConfig &cfg);

  void tick(uint32_t now);       // every loop; advances a running phase
  void longPress(uint32_t now);  // start / pause / resume / begin the next phase
  void reset();                  // back to IDLE, count cleared, alert dropped
  bool takeAlert();              // true exactly once after each phase end
  PomoView view() const;

 private:
  uint32_t lengthOf(PomoPhase p) const;
  void begin(PomoPhase p, uint32_t now);
  void finish();

  PomoConfig _cfg;
  PomoState _state;
  PomoState _resumeAs;  // FOCUS or BREAK: what PAUSED goes back to
  PomoPhase _phase;
  PomoPhase _next;
  uint32_t _left;       // ms left in the phase
  uint32_t _last;       // `now` at the last tick of a running phase
  uint8_t _completed;
  bool _alert;
};

// "MM:SS", clamped to 99:59 so it never needs more than five characters.
void pomoFormatTime(uint32_t sec, char *buf, size_t cap);
```

- [ ] **Step 4: Write `pomodoro.cpp`**

```cpp
#include "pomodoro.h"

#include <stdio.h>

Pomodoro::Pomodoro(const PomoConfig &cfg) : _cfg(cfg) { reset(); }

uint32_t Pomodoro::lengthOf(PomoPhase p) const {
  switch (p) {
    case PHASE_FOCUS: return _cfg.focusMs;
    case PHASE_SHORT: return _cfg.shortMs;
    default: return _cfg.longMs;
  }
}

void Pomodoro::reset() {
  _state = POMO_IDLE;
  _resumeAs = POMO_FOCUS;
  _phase = PHASE_FOCUS;
  _next = PHASE_FOCUS;
  _left = _cfg.focusMs;
  _last = 0;
  _completed = 0;
  _alert = false;
}

void Pomodoro::begin(PomoPhase p, uint32_t now) {
  _phase = p;
  _next = p;
  _left = lengthOf(p);
  _last = now;
  _state = p == PHASE_FOCUS ? POMO_FOCUS : POMO_BREAK;
}

void Pomodoro::finish() {
  _left = 0;
  if (_phase == PHASE_FOCUS) {
    if (_completed < _cfg.sessions) _completed++;
    _next = _completed >= _cfg.sessions ? PHASE_LONG : PHASE_SHORT;
  } else {
    if (_phase == PHASE_LONG) _completed = 0;  // the set is over
    _next = PHASE_FOCUS;
  }
  _state = POMO_DONE;
  _alert = true;
}

void Pomodoro::tick(uint32_t now) {
  if (_state != POMO_FOCUS && _state != POMO_BREAK) return;
  const uint32_t elapsed = now - _last;  // unsigned: correct across a millis() wrap
  _last = now;
  if (elapsed >= _left) finish();
  else _left -= elapsed;
}

void Pomodoro::longPress(uint32_t now) {
  switch (_state) {
    case POMO_IDLE:
      begin(PHASE_FOCUS, now);
      break;
    case POMO_FOCUS:
    case POMO_BREAK:
      tick(now);  // bring the clock up to date first: the phase may already be over
      if (_state == POMO_FOCUS || _state == POMO_BREAK) {
        _resumeAs = _state;
        _state = POMO_PAUSED;
      }
      break;
    case POMO_PAUSED:
      _state = _resumeAs;
      _last = now;
      break;
    case POMO_DONE:
      begin(_next, now);
      break;
  }
}

bool Pomodoro::takeAlert() {
  const bool a = _alert;
  _alert = false;
  return a;
}

PomoView Pomodoro::view() const {
  PomoView v;
  v.state = _state;
  v.phase = _phase;
  v.next = _next;
  v.leftMs = _left;
  const uint32_t len = lengthOf(_phase);
  v.fraction = len ? (uint8_t)(((uint64_t)_left * 100 + len / 2) / len) : 0;
  v.completed = _completed;
  v.sessions = _cfg.sessions;
  v.displaySec = (_left + 999) / 1000;
  return v;
}

void pomoFormatTime(uint32_t sec, char *buf, size_t cap) {
  if (sec > 99 * 60 + 59) sec = 99 * 60 + 59;
  snprintf(buf, cap, "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd firmware/sim && make test`
Expected: both lines end in `0 failed`. If `testCountdown`'s fraction check fails, recompute: 1440000 ms left of 1500000 is 96 %.

- [ ] **Step 6: Commit (only if inside a git repo)**

```bash
git add firmware/src/pomodoro.h firmware/src/pomodoro.cpp firmware/sim/tests firmware/sim/Makefile
git commit -m "feat(firmware): Pomodoro state machine with host tests"
```

---

### Task 8: Gesture tracker

**Files:**
- Create: `firmware/src/gesture.h`, `firmware/src/gesture.cpp`
- Create: `firmware/sim/tests/gesture_test.cpp`
- Modify: `firmware/src/main.cpp`, `firmware/sim/Makefile`

**Interfaces:**
- Produces (used by Task 10): `enum class Gesture : uint8_t { None, Tap, SwipeNext, SwipePrev, LongPress, ResetPress }`; `class GestureTracker { Gesture update(bool down, int16_t x, int16_t y, uint32_t now, bool holdEnabled); }`; the constants `SWIPE_MIN_PX`, `SWIPE_MAX_MS`, `TAP_MAX_PX`, `TAP_MAX_MS`, `LONG_PRESS_MS`, `RESET_PRESS_MS`.
- Behaviour contract: `LongPress` fires once, while the finger is still down, 600 ms after touch-down if the finger has not moved `TAP_MAX_PX` and `holdEnabled`; `ResetPress` fires once at 2000 ms after a `LongPress`; after either, the release yields `None`. With `holdEnabled == false`, holds do nothing and a slow touch yields `None` on release (as before). Tap and swipe thresholds are unchanged from today.

- [ ] **Step 1: Write the failing test**

Create `firmware/sim/tests/gesture_test.cpp`:

```cpp
#include <cstdint>

#include "check.h"
#include "gesture.h"

namespace {

struct Pad {
  GestureTracker t;
  Gesture at(uint32_t now, bool down, int x, int y, bool hold = false) {
    return t.update(down, (int16_t)x, (int16_t)y, now, hold);
  }
};

void testTap() {
  Pad p;
  CHECK(p.at(0, true, 100, 100) == Gesture::None);
  CHECK(p.at(50, true, 101, 101) == Gesture::None);
  CHECK(p.at(100, false, 0, 0) == Gesture::Tap);
  CHECK(p.at(150, false, 0, 0) == Gesture::None);  // nothing more once released
}

void testSwipes() {
  Pad p;
  p.at(0, true, 200, 100);
  p.at(100, true, 150, 102);
  p.at(150, true, 100, 100);
  CHECK(p.at(200, false, 0, 0) == Gesture::SwipeNext);  // moved left: advance

  p.at(1000, true, 60, 100);
  p.at(1100, true, 110, 98);
  p.at(1150, true, 160, 100);
  CHECK(p.at(1200, false, 0, 0) == Gesture::SwipePrev);
}

void testNonGestures() {
  Pad p;
  // A slow drag is neither a swipe nor a tap.
  p.at(0, true, 200, 100);
  p.at(400, true, 150, 100);
  p.at(800, true, 100, 100);
  CHECK(p.at(801, false, 0, 0) == Gesture::None);

  // A mostly-vertical drag is not a horizontal swipe.
  p.at(2000, true, 100, 200);
  p.at(2100, true, 102, 120);
  CHECK(p.at(2150, false, 0, 0) == Gesture::None);

  // A touch held too long to be a tap, with holds disabled: ignored on release.
  p.at(3000, true, 100, 100);
  CHECK(p.at(3600, true, 100, 100) == Gesture::None);
  CHECK(p.at(3700, false, 0, 0) == Gesture::None);
}

void testLongPressAndReset() {
  Pad p;
  CHECK(p.at(0, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(300, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(599, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(600, true, 100, 100, true) == Gesture::LongPress);
  CHECK(p.at(700, true, 100, 100, true) == Gesture::None);   // once
  CHECK(p.at(1900, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(2000, true, 100, 100, true) == Gesture::ResetPress);
  CHECK(p.at(2100, true, 100, 100, true) == Gesture::None);  // once
  CHECK(p.at(2200, false, 0, 0) == Gesture::None);           // release acts as nothing
}

// Review Focus 1: the finger lifting after a hold must not be read as a tap.
void testNoTrailingTap() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(600, true, 100, 100, true) == Gesture::LongPress);
  CHECK(p.at(650, false, 0, 0) == Gesture::None);  // not Tap
  // ...and the tracker is clean for the next touch.
  p.at(1000, true, 100, 100, true);
  CHECK(p.at(1050, false, 0, 0) == Gesture::Tap);
}

void testReleaseBetweenLongAndReset() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(600, true, 100, 100, true) == Gesture::LongPress);
  CHECK(p.at(1500, false, 0, 0) == Gesture::None);
  // No ResetPress was produced, and a fresh touch starts from zero.
  p.at(2000, true, 100, 100, true);
  CHECK(p.at(2400, true, 100, 100, true) == Gesture::None);
}

// Review Focus 1: holds on a card that does not use them stay ignored.
void testHoldDisabled() {
  Pad p;
  p.at(0, true, 100, 100, false);
  CHECK(p.at(600, true, 100, 100, false) == Gesture::None);
  CHECK(p.at(2000, true, 100, 100, false) == Gesture::None);
  CHECK(p.at(2100, false, 0, 0) == Gesture::None);
}

void testMovingCancelsHold() {
  Pad p;
  p.at(0, true, 100, 100, true);
  p.at(100, true, 130, 100, true);  // 30 px: no longer a stationary hold
  CHECK(p.at(700, true, 130, 100, true) == Gesture::None);
  CHECK(p.at(2100, true, 130, 100, true) == Gesture::None);
  CHECK(p.at(2200, false, 0, 0) == Gesture::None);  // and 30 px is not a swipe either
}

void testQuickTapOnHoldCard() {
  Pad p;
  p.at(0, true, 100, 100, true);
  CHECK(p.at(100, false, 0, 0) == Gesture::Tap);
}

// Review Focus 3: millis() wrap.
void testClockWrap() {
  const uint32_t base = 0xFFFFFF00u;
  Pad p;
  p.at(base, true, 100, 100, true);
  CHECK(p.at(base + 599u, true, 100, 100, true) == Gesture::None);
  CHECK(p.at(base + 600u, true, 100, 100, true) == Gesture::LongPress);
  CHECK(p.at(base + 650u, false, 0, 0) == Gesture::None);

  Pad q;  // a tap across the wrap
  q.at(base + 200u, true, 100, 100);
  CHECK(q.at(base + 300u, false, 0, 0) == Gesture::Tap);
}

}  // namespace

int main() {
  testTap();
  testSwipes();
  testNonGestures();
  testLongPressAndReset();
  testNoTrailingTap();
  testReleaseBetweenLongAndReset();
  testHoldDisabled();
  testMovingCancelsHold();
  testQuickTapOnHoldCard();
  testClockWrap();
  return checksDone("gesture_test");
}
```

- [ ] **Step 2: Register it and watch it fail**

In `firmware/sim/Makefile`:

```make
TESTS := build/portal_util_test build/pomodoro_test build/gesture_test

build/pomodoro_test: ../src/pomodoro.cpp
build/gesture_test: ../src/gesture.cpp
```

Run: `cd firmware/sim && make test`
Expected: the first two pass, `gesture_test` fails to build (`gesture.h` missing).

- [ ] **Step 3: Write `gesture.h`**

```cpp
#pragma once
#include <stdint.h>

// Turns polled touch samples into gestures. Pure logic -- no Arduino, no I2C --
// so the host tests in sim/tests can drive it with a fake clock.
//
// Swipes and taps are derived from the travel between touch-down and touch-up
// rather than from the CST816's gesture register, because that register's
// codes differ between the S/T/D chip variants.
enum class Gesture : uint8_t {
  None,
  Tap,
  SwipeNext,   // finger moved left: advance
  SwipePrev,
  LongPress,   // held in place for LONG_PRESS_MS; fires while the finger is still down
  ResetPress,  // still held at RESET_PRESS_MS, after a LongPress; fires once
};

constexpr int SWIPE_MIN_PX = 40;
constexpr uint32_t SWIPE_MAX_MS = 700;
constexpr int TAP_MAX_PX = 16;
constexpr uint32_t TAP_MAX_MS = 400;
constexpr uint32_t LONG_PRESS_MS = 600;
constexpr uint32_t RESET_PRESS_MS = 2000;

class GestureTracker {
 public:
  // Feed one sample per poll. At most one gesture comes back per call.
  // `holdEnabled` says whether holds mean anything right now (only the
  // Pomodoro card uses them). When false, a long touch is ignored on release,
  // exactly as a slow drag always was.
  Gesture update(bool down, int16_t x, int16_t y, uint32_t now, bool holdEnabled);

 private:
  bool _down = false;
  bool _moved = false;       // travelled past TAP_MAX_PX: no longer a stationary hold
  bool _longFired = false;
  bool _resetFired = false;
  int16_t _sx = 0, _sy = 0;  // where the touch began
  int16_t _lx = 0, _ly = 0;  // the latest sample
  uint32_t _t0 = 0;
};
```

- [ ] **Step 4: Write `gesture.cpp`**

```cpp
#include "gesture.h"

#include <stdlib.h>

Gesture GestureTracker::update(bool down, int16_t x, int16_t y, uint32_t now, bool holdEnabled) {
  if (down) {
    if (!_down) {
      _down = true;
      _moved = false;
      _longFired = false;
      _resetFired = false;
      _sx = _lx = x;
      _sy = _ly = y;
      _t0 = now;
      return Gesture::None;
    }
    _lx = x;
    _ly = y;
    if (abs(_lx - _sx) >= TAP_MAX_PX || abs(_ly - _sy) >= TAP_MAX_PX) _moved = true;
    if (!holdEnabled || _moved) return Gesture::None;

    const uint32_t held = now - _t0;  // unsigned: correct across a millis() wrap
    if (!_longFired && held >= LONG_PRESS_MS) {
      _longFired = true;
      return Gesture::LongPress;
    }
    if (_longFired && !_resetFired && held >= RESET_PRESS_MS) {
      _resetFired = true;
      return Gesture::ResetPress;
    }
    return Gesture::None;
  }

  if (!_down) return Gesture::None;
  _down = false;

  // A hold that already acted must not also count as a tap on release.
  if (_longFired) return Gesture::None;

  const int dx = _lx - _sx;
  const int dy = _ly - _sy;
  const uint32_t dt = now - _t0;

  if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy) && dt <= SWIPE_MAX_MS) {
    return dx < 0 ? Gesture::SwipeNext : Gesture::SwipePrev;
  }
  if (abs(dx) < TAP_MAX_PX && abs(dy) < TAP_MAX_PX && dt <= TAP_MAX_MS) {
    return Gesture::Tap;
  }
  return Gesture::None;
}
```

- [ ] **Step 5: Run the tests**

Run: `cd firmware/sim && make test`
Expected: all three lines `0 failed`.

- [ ] **Step 6: Replace the touch code in `main.cpp`**

In `firmware/src/main.cpp`:

1. After `#include "display.h"` add `#include "gesture.h"`.
2. Replace this block:

```cpp
// Swipe tracking. Deriving gestures from raw coordinates keeps this working
// across the CST816S/T/D variants, whose gesture registers disagree.
bool fingerDown = false;
int16_t startX = 0, startY = 0, lastX = 0, lastY = 0;
uint32_t touchStart = 0;

constexpr int SWIPE_MIN_PX = 40;
constexpr uint32_t SWIPE_MAX_MS = 700;
constexpr int TAP_MAX_PX = 16;
constexpr uint32_t TAP_MAX_MS = 400;
constexpr uint32_t SETUP_HOLD_MS = 5000;
```
with:
```cpp
// Gestures come from raw coordinates (see gesture.h), which keeps this working
// across the CST816S/T/D variants, whose gesture registers disagree.
GestureTracker gestures;

constexpr uint32_t SETUP_HOLD_MS = 5000;
```
3. Replace the whole `pollTouch()` function with:

```cpp
void pollTouch() {
  int16_t x = 0, y = 0;
  const bool down = touch.read(x, y);
  switch (gestures.update(down, x, y, millis(), false)) {
    case Gesture::SwipeNext:  // swipe left advances
    case Gesture::Tap:
      step(1);
      break;
    case Gesture::SwipePrev:
      step(-1);
      break;
    default:
      break;
  }
}
```

(`setupHoldRequested()` and its constants stay as they are.)

- [ ] **Step 7: Wire the sim and build**

In `firmware/sim/Makefile`:

```make
SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp ../src/gesture.cpp \
        net_sim.cpp touch_sim.cpp settings_sim.cpp portal_sim.cpp ota_sim.cpp sdl_main.cpp
```

Run: `cd firmware && pio run` -> `[SUCCESS]`; `cd firmware/sim && make && make test` -> sim builds, tests pass.

Check taps and swipes still work: `cd firmware/sim && make run`, click to advance a card, drag left/right to swipe; close the window.

- [ ] **Step 8: Commit (only if inside a git repo)**

```bash
git add firmware/src firmware/sim
git commit -m "refactor(firmware): host-tested gesture tracker with long-press"
```

---

### Task 9: Pomodoro card in the deck

**Files:**
- Modify: `firmware/src/ui.h`, `firmware/src/ui.cpp`, `firmware/src/main.cpp`, `firmware/src/config.h.example`, `firmware/src/config.h`
- Modify: `firmware/sim/shot.cpp`, `firmware/sim/Makefile`

**Interfaces:**
- Consumes: `Pomodoro`, `PomoView`, `pomoFormatTime` (Task 7); `RingStyle`, `ringStyle`, `drawGaugeCard`, `fitFont` (Task 6).
- Produces (used by Task 10):
  - `uint8_t uiDeckSize(const Payload &p)`: payload cards (if valid) plus one, so always >= 1
  - `uint8_t uiPomodoroIndex(const Payload &p)`: the last index
  - `void uiRender(Display&, const Payload&, uint8_t index, bool online, uint32_t ageMs, const PomoView &pomo)` (new last parameter)
  - `void uiReplayPomodoro()`
  - in `main.cpp`: global `Pomodoro pomo`, `POMO_TIME_DIV`, `lastPomoSec`.

- [ ] **Step 1: Render the states first (they will not compile yet; this is the target)**

Plan the screenshots now so each state has a name: `ready`, `focus`, `paused`, `break`, `done`, `long`. They are produced by Step 6.

- [ ] **Step 2: ui.h**

In `firmware/src/ui.h` add `#include "pomodoro.h"` after `#include "payload.h"`, change the `uiRender` declaration and add the new functions:

```cpp
// The deck is the bridge's cards plus one local card at the end: the Pomodoro
// timer. It exists even with no payload (bridge down), where it is the only card.
uint8_t uiDeckSize(const Payload &p);       // always >= 1
uint8_t uiPomodoroIndex(const Payload &p);  // the last index

// Draws one card plus the shared chrome. Everything is composed into an
// off-screen sprite and pushed in one go, so the panel never tears.
void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs,
              const PomoView &pomo);
```
(replacing the old `uiRender` declaration and its comment), and:

```cpp
// Makes the Pomodoro card sweep its ring in from empty again (as uiReplay does
// for payload cards).
void uiReplayPomodoro();
```

- [ ] **Step 3: ui.cpp, the Pomodoro card**

Insert the following into `firmware/src/ui.cpp` immediately before the line `}  // namespace` that closes the anonymous namespace (the one just before `void uiBegin`):

```cpp
// --- the Pomodoro card ---------------------------------------------------
// The timer lives on the device, so this is the one card whose text the
// firmware formats itself (MM:SS). It reuses the gauge layout: the ring is the
// time left in the phase, the middle is the clock, and the line in the gap at
// the bottom is "sessions done  +  what a hold does".
constexpr uint32_t POMO_MUTED = 0x7C8598;  // idle / paused: the ring waits, so it is grey

GaugeAnim g_pomoAnim;

const char *phaseName(PomoPhase p) {
  return p == PHASE_FOCUS ? "Focus" : (p == PHASE_SHORT ? "Break" : "Long break");
}

uint32_t phaseRgb(PomoPhase p) { return p == PHASE_FOCUS ? RGB888[ACC_AMBER] : RGB888[ACC_GREEN]; }

void pomodoroCard(const PomoView &v, Card &card, RingStyle &style) {
  char time[8];
  pomoFormatTime(v.displaySec, time, sizeof(time));
  char count[8];
  snprintf(count, sizeof(count), "%u/%u", (unsigned)v.completed, (unsigned)v.sessions);

  strlcpy(card.title, "Pomodoro", sizeof(card.title));
  strlcpy(card.value, time, sizeof(card.value));
  card.gauge = (int8_t)v.fraction;

  const char *hint = "";
  switch (v.state) {
    case POMO_IDLE:
      strlcpy(card.sub1, "Ready", sizeof(card.sub1));
      hint = "Hold to start";
      style.rgb = POMO_MUTED;
      break;
    case POMO_FOCUS:
      strlcpy(card.sub1, "Focus", sizeof(card.sub1));
      hint = "Hold to pause";
      style.rgb = phaseRgb(PHASE_FOCUS);
      break;
    case POMO_BREAK:
      strlcpy(card.sub1, phaseName(v.phase), sizeof(card.sub1));
      hint = "Hold to pause";
      style.rgb = phaseRgb(v.phase);
      break;
    case POMO_PAUSED:
      strlcpy(card.sub1, "Paused", sizeof(card.sub1));
      hint = "Hold to resume";
      style.rgb = POMO_MUTED;
      break;
    case POMO_DONE:
      strlcpy(card.value, "Done", sizeof(card.value));
      snprintf(card.sub1, sizeof(card.sub1), "%s done", phaseName(v.phase));
      hint = v.next == PHASE_FOCUS ? "Hold for focus"
                                   : (v.next == PHASE_SHORT ? "Hold for break" : "Hold for long break");
      style.rgb = phaseRgb(v.phase);
      break;
  }
  snprintf(card.sub2, sizeof(card.sub2), "%s  %s", count, hint);
  style.pct = false;      // the ring is time left, not a share of a limit
  style.notches = false;  // and has no thresholds
}

void drawPomodoroCard(LovyanGFX *g, const PomoView &v, float flash, bool online, uint32_t ageMs) {
  Card card{};
  RingStyle style = ringStyle(POMO_MUTED);
  pomodoroCard(v, card, style);
  style.flash = flash;
  // Sized on its own: it never nudges the font the payload cards agreed on.
  drawGaugeCard(g, card, style, fitFont(g, "88:88", 130, 42), g_pomoAnim, millis(), online, ageMs);
}

```

- [ ] **Step 4: ui.cpp, deck and render**

Replace the existing `uiReplay` and `uiRender` functions (everything from `void uiReplay(uint8_t index) {` through the end of `uiRender`; keep `uiAnimating` and `uiMessage` as they are) with:

```cpp
void uiReplay(uint8_t index) {
  if (index < MAX_CARDS) g_anim[index].seen = false;
}

void uiReplayPomodoro() { g_pomoAnim.seen = false; }

uint8_t uiDeckSize(const Payload &p) { return (p.valid ? p.nCards : 0) + 1; }

uint8_t uiPomodoroIndex(const Payload &p) { return p.valid ? p.nCards : 0; }

void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs,
              const PomoView &pomo) {
  g_animating = false;

  const uint8_t deck = uiDeckSize(p);
  const uint8_t i = index % deck;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  if (i == uiPomodoroIndex(p)) {
    drawPomodoroCard(g, pomo, 0.0f, online, ageMs);
  } else {
    const Card &card = p.cards[i];
    if (card.gauge >= GAUGE_BLANK) {
      // 130px is what fits between the ring's inner edges at the value's
      // height; the size is settled across the deck, not per card.
      const ValueFont valueFont = fitDeck(g, p, 130, 42);
      drawGaugeCard(g, card, ringStyle(RGB888[card.color % 7]), valueFont, g_anim[i],
                    millis(), online, ageMs);
    } else {
      drawTextCard(g, p, card, g_palette[card.color % 7], online, ageMs);
    }
  }

  drawDots(g, deck, i);

  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}
```

(The old "NO DATA / waiting for bridge" screen is gone: with no payload the deck is just the Pomodoro card, whose top bar says OFFLINE.)

- [ ] **Step 5: main.cpp**

Edits in `firmware/src/main.cpp`:

1. After `#include "payload.h"` add `#include "pomodoro.h"`.

2. Directly after the `#include` lines and before `namespace {`, add:

```cpp
// Defaults, so a config.h from before the Pomodoro still builds.
#ifndef POMO_FOCUS_MIN
#define POMO_FOCUS_MIN 25
#endif
#ifndef POMO_BREAK_MIN
#define POMO_BREAK_MIN 5
#endif
#ifndef POMO_LONG_MIN
#define POMO_LONG_MIN 15
#endif
#ifndef POMO_SESSIONS
#define POMO_SESSIONS 4
#endif
// Divides every Pomodoro duration. Leave at 1. `make run POMO_FAST=60` in
// sim/ turns minutes into seconds, so a whole cycle and the phase-end alert
// can be watched in about two minutes.
#ifndef POMO_TIME_DIV
#define POMO_TIME_DIV 1
#endif

static_assert(POMO_FOCUS_MIN >= 1 && POMO_FOCUS_MIN <= 99, "POMO_FOCUS_MIN must be 1..99");
static_assert(POMO_BREAK_MIN >= 1 && POMO_BREAK_MIN <= 99, "POMO_BREAK_MIN must be 1..99");
static_assert(POMO_LONG_MIN >= 1 && POMO_LONG_MIN <= 99, "POMO_LONG_MIN must be 1..99");
static_assert(POMO_SESSIONS >= 1 && POMO_SESSIONS <= 9, "POMO_SESSIONS must be 1..9");
```

3. In the anonymous namespace, after `Payload payload{};` add:

```cpp
Pomodoro pomo(PomoConfig{POMO_FOCUS_MIN * 60000UL / POMO_TIME_DIV,
                         POMO_BREAK_MIN * 60000UL / POMO_TIME_DIV,
                         POMO_LONG_MIN * 60000UL / POMO_TIME_DIV, POMO_SESSIONS});
uint32_t lastPomoSec = 0xFFFFFFFFu;  // the clock value last drawn, to redraw when it changes
```

4. Replace `step()` with:

```cpp
void step(int delta) {
  const uint8_t n = uiDeckSize(payload);
  if (n <= 1) return;
  cardIndex = (cardIndex + n + delta) % n;
  lastRotate = millis();
  if (cardIndex == uiPomodoroIndex(payload)) uiReplayPomodoro();
  else uiReplay(cardIndex);
  dirty = true;
}
```

5. In `loop()`, replace `if (cardIndex >= payload.nCards) cardIndex = 0;` with `if (cardIndex >= uiDeckSize(payload)) cardIndex = 0;`.

6. In `loop()`, right after `pollTouch();` add `pomo.tick(now);`. Then replace the render block:

```cpp
  // Redraw on change, every frame while a ring is still moving, and once a
  // second otherwise so the freshness counter ticks.
  if (dirty || uiAnimating() || now - lastDraw >= 1000) {
    const uint32_t age = lastGood ? now - lastGood : now;
    uiRender(lcd, payload, cardIndex, netOnline(), age);
    lastDraw = now;
    dirty = false;
  }
```
with:
```cpp
  // The Pomodoro clock counts whole seconds, so redraw the moment its number
  // changes rather than leaving it to the 1 s housekeeping tick below, which
  // would beat against it and skip or repeat digits.
  const PomoView pv = pomo.view();
  if (cardIndex == uiPomodoroIndex(payload) && pv.displaySec != lastPomoSec) {
    lastPomoSec = pv.displaySec;
    dirty = true;
  }

  // Redraw on change, every frame while a ring is still moving, and once a
  // second otherwise so the freshness counter ticks.
  if (dirty || uiAnimating() || now - lastDraw >= 1000) {
    const uint32_t age = lastGood ? now - lastGood : now;
    uiRender(lcd, payload, cardIndex, netOnline(), age, pv);
    lastDraw = now;
    dirty = false;
  }
```

- [ ] **Step 6: config files and the sim build**

Append to `firmware/src/config.h.example` **and** to the local `firmware/src/config.h` (append only; do not touch or print the existing lines):

```c

// Pomodoro card (the last card in the deck). Minutes, 1-99.
#define POMO_FOCUS_MIN     25
#define POMO_BREAK_MIN      5
#define POMO_LONG_MIN      15   // the long break, after POMO_SESSIONS focus sessions
#define POMO_SESSIONS       4   // 1-9
```

`firmware/sim/Makefile`:

```make
SCALE   ?= 2
POMO_FAST ?= 1
```
(next to the existing `SCALE ?= 2`), add `-DPOMO_TIME_DIV=$(POMO_FAST)` to `CXXFLAGS`:

```make
CXXFLAGS := -std=c++17 -O2 -DLGFX_SDL -DSIM_SCALE=$(SCALE) -DPOMO_TIME_DIV=$(POMO_FAST) \
            -I. -I$(LGFX) -I$(AJSON) $(shell pkg-config --cflags sdl2) \
            -Wno-deprecated-declarations
```
and add `pomodoro.cpp` to both `SRCS` and `SHOT_OBJ`:

```make
SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp ../src/gesture.cpp ../src/pomodoro.cpp \
        net_sim.cpp touch_sim.cpp settings_sim.cpp portal_sim.cpp ota_sim.cpp sdl_main.cpp
...
SHOT_OBJ := build/shot.o build/ui.o build/payload.o build/pomodoro.o
```

Because `POMO_TIME_DIV` is part of the flags, a changed `POMO_FAST` only takes effect after `make clean` (or touching `main.cpp`); say so in Step 9.

- [ ] **Step 7: shot.cpp, Pomodoro views and the new `uiRender` signature**

In `firmware/sim/shot.cpp`:

1. Add `#include "../src/pomodoro.h"` after `#include "../src/payload.h"`.

2. Add this function in the anonymous namespace, before `renderSpecial`:

```cpp
// Drives the real state machine to a named state and draws it with an empty
// payload (the bridge-down case: the Pomodoro is the only card), so the shot
// shows what the board would draw rather than a hand-built view.
bool pomodoroShot(Display &lcd, const char *state) {
  const uint32_t MIN = 60000;
  Pomodoro p(PomoConfig{25 * MIN, 5 * MIN, 15 * MIN, 4});
  uint32_t now = 1000;
  auto run = [&](uint32_t ms) {
    now += ms;
    p.tick(now);
  };

  if (!strcmp(state, "ready")) {
  } else if (!strcmp(state, "focus")) {
    p.longPress(now);
    run(7 * MIN + 3000);
  } else if (!strcmp(state, "paused")) {
    p.longPress(now);
    run(7 * MIN + 3000);
    p.longPress(now);
  } else if (!strcmp(state, "break")) {
    p.longPress(now);
    run(25 * MIN);
    p.longPress(now);
    run(90000);
  } else if (!strcmp(state, "done")) {
    p.longPress(now);
    run(25 * MIN);
  } else if (!strcmp(state, "long")) {  // three full cycles, a fourth focus, then the long break
    for (int i = 0; i < 3; i++) {
      p.longPress(now);
      run(25 * MIN);
      p.longPress(now);
      run(5 * MIN);
    }
    p.longPress(now);
    run(25 * MIN);
    p.longPress(now);
    run(60000);
  } else {
    return false;
  }

  const Payload none{};
  const PomoView v = p.view();
  for (int frame = 0; frame < 400; frame++) {
    uiRender(lcd, none, 0, true, 4000, v);
    if (!uiAnimating()) break;
    delay(8);
  }
  return true;
}
```

3. In `renderSpecial`, extend the chain:

```cpp
  } else if (!strncmp(name, "pomo-", 5)) {
    if (!pomodoroShot(lcd, name + 5)) return false;
  } else {
```
(after the `ota` branch and before the final `else`).

4. In `render()`, the payload loop's call becomes:

```cpp
  const Payload none{};
  const PomoView idle = Pomodoro(PomoConfig{25 * 60000UL, 5 * 60000UL, 15 * 60000UL, 4}).view();
  for (uint8_t i = 0; i < payload.nCards; i++) {
    for (int frame = 0; frame < 400; frame++) {
      uiRender(lcd, payload, i, true, 4000, idle);
```
(remove the unused `none` there if the compiler warns; the `none` object is only needed in `pomodoroShot`). Update the header comment's view list to: `@portal, @ota, @pomo-ready|focus|paused|break|done|long`.

- [ ] **Step 8: Build and look**

Run:
```bash
cd firmware/sim && make build/cube-shot
for s in ready focus paused break done long; do ./build/cube-shot build/shot @pomo-$s; done
```
Expected: six lines `build/shot-pomo-<state>.png`. Open each with the Read tool and check:

- `ready`: grey ring completely full, `25:00`, caption `READY`, bottom line `0/4  HOLD TO START`, top bar `POMODORO`, a single page dot, red `OFFLINE` in the top-right (no payload).
- `focus`: amber ring about 71 % full (`17:57` left), caption `FOCUS`, `0/4  HOLD TO PAUSE`.
- `paused`: grey ring, `PAUSED`, `0/4  HOLD TO RESUME`.
- `break`: green ring about 70 % full, `03:30`, `BREAK`, `1/4  HOLD TO PAUSE`.
- `done`: empty track (no arc), value `DONE`, caption `FOCUS DONE`, `1/4  HOLD FOR BREAK`.
- `long`: green, `14:00`, `LONG BREAK`, `4/4  HOLD TO PAUSE`.
- In every one: **no notch gaps** in the ring, **no percentage** text, text inside the ring not clipped, bottom line fits the width. Review Focus 5: these shots use an empty payload, so they also prove the Pomodoro card exists when the bridge is down.

Re-run the Task 6 regression: `./build/cube-shot build/after2 fixtures/ring.json` and confirm the payload cards still match the baseline **except the page-dot row** at the bottom (there is now one extra dot). Verify by opening `build/after2-0.png` next to `build/base-0.png`: ring, text and colours identical; nine dots instead of eight is expected for a full deck of eight cards plus the Pomodoro.

Run: `cd firmware && pio run` -> `[SUCCESS]`; `cd firmware/sim && make && make test`.

- [ ] **Step 9: Try it in the simulator**

Run: `cd firmware/sim && make clean >/dev/null && make run POMO_FAST=60`. Swipe or click to the last card: the Pomodoro card appears with its ring sweeping in. (Controls are wired in Task 10; for now only the display is checked.) Close the window. Run `make clean` afterwards so a later normal build does not keep `POMO_FAST=60`.

- [ ] **Step 10: Commit (only if inside a git repo)**

```bash
git add firmware/src firmware/sim
git commit -m "feat(ui): Pomodoro card appended to the deck"
```

---

### Task 10: Pomodoro controls and phase-end alert

**Files:**
- Modify: `firmware/src/ui.h`, `firmware/src/ui.cpp`, `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `GestureTracker`, `Gesture::LongPress/ResetPress` (Task 8); `Pomodoro::longPress/reset/takeAlert`, `uiPomodoroIndex`, `uiReplayPomodoro` (Tasks 7, 9).
- Produces: `void uiAlertStart()`.

- [ ] **Step 1: Alert state in `ui.cpp`**

Insert before the `}  // namespace` that closes the anonymous namespace (after the Pomodoro card block from Task 9):

```cpp
// --- phase-end alert -----------------------------------------------------
// The board has no speaker, so the end of a phase is a few pulses of the ring
// colour towards white and of the backlight, then back to normal.
constexpr uint32_t ALERT_MS = 2000;
constexpr int ALERT_PULSES = 3;
constexpr float ALERT_RING_MIX = 0.85f;  // how far the ring goes towards white at a peak
uint32_t g_alertStart = 0;
bool g_alertOn = false;

// 0 at the start and end, 1 at each of ALERT_PULSES peaks in between.
float alertPulse(uint32_t now) {
  const float t = (float)(now - g_alertStart) / (float)ALERT_MS;
  return 0.5f - 0.5f * cosf(6.2831853f * ALERT_PULSES * t);
}

```

Add to `ui.h`:

```cpp
// Starts the Pomodoro phase-end alert: the ring and the backlight pulse for
// about two seconds. uiAnimating() stays true while it runs.
void uiAlertStart();
```

Add to `ui.cpp` (next to the other public functions, e.g. after `uiReplayPomodoro`):

```cpp
void uiAlertStart() {
  g_alertStart = millis();
  g_alertOn = true;
}
```

Add `#include "config.h"` to `ui.cpp` (after `#include "fonts_gen.h"`) so `BACKLIGHT` is known.

- [ ] **Step 2: Apply it in `uiRender`**

In `uiRender`, replace

```cpp
  g_animating = false;

  const uint8_t deck = uiDeckSize(p);
```
with:
```cpp
  g_animating = false;

  float flash = 0.0f;
  if (g_alertOn) {
    const uint32_t now = millis();
    if (now - g_alertStart >= ALERT_MS) {
      g_alertOn = false;
      lcd.setBrightness(BACKLIGHT);
    } else {
      flash = alertPulse(now);
      lcd.setBrightness((uint8_t)(BACKLIGHT + (255 - BACKLIGHT) * flash));
      g_animating = true;
    }
  }

  const uint8_t deck = uiDeckSize(p);
```
and change `drawPomodoroCard(g, pomo, 0.0f, online, ageMs);` to `drawPomodoroCard(g, pomo, flash * ALERT_RING_MIX, online, ageMs);`.

- [ ] **Step 3: Controls and alert in `main.cpp`**

1. Replace `pollTouch()` with:

```cpp
void pollTouch() {
  int16_t x = 0, y = 0;
  const bool down = touch.read(x, y);
  const uint32_t now = millis();
  // Holds only mean something on the Pomodoro card. Everywhere else a long
  // touch is ignored when the finger lifts, as a slow press always was.
  const bool onPomodoro = cardIndex == uiPomodoroIndex(payload);
  switch (gestures.update(down, x, y, now, onPomodoro)) {
    case Gesture::SwipeNext:  // swipe left advances
    case Gesture::Tap:
      step(1);
      break;
    case Gesture::SwipePrev:
      step(-1);
      break;
    case Gesture::LongPress:  // start / pause / resume / next phase
      pomo.longPress(now);
      dirty = true;
      break;
    case Gesture::ResetPress:  // still held after the long-press: start over
      pomo.reset();
      dirty = true;
      break;
    default:
      break;
  }
}
```

2. In `loop()`, replace

```cpp
  pollTouch();
  pomo.tick(now);
```
with:
```cpp
  pomo.tick(now);
  if (pomo.takeAlert()) {
    // Phase over: pull the deck to the Pomodoro card wherever you were. A tap
    // or swipe leaves it again; the DONE state waits for a long-press.
    cardIndex = uiPomodoroIndex(payload);
    lastRotate = now;
    uiReplayPomodoro();
    uiAlertStart();
    dirty = true;
  }
  pollTouch();
```

- [ ] **Step 4: Build**

Run: `cd firmware && pio run` -> `[SUCCESS]`; `cd firmware/sim && make && make test`.

- [ ] **Step 5: Try the full cycle in the simulator**

Run: `cd firmware/sim && make clean >/dev/null && make run POMO_FAST=60` (minutes become seconds: focus 25 s, break 5 s, long break 15 s). Walk through, using the mouse as the finger:

1. Click or drag to the last card (Pomodoro, `READY`).
2. Press and hold the mouse for about a second, then release: it starts `FOCUS`, the ring begins draining and the clock counts down once per second. Releasing did not also advance the card (Review Focus 1).
3. Press and hold again: `PAUSED`, grey. Hold again: resumes.
4. Press and hold, keep holding past two seconds: it resets to `READY 25:00`.
5. Start a focus, then swipe to another card. When the 25 s end, the deck jumps back to the Pomodoro card, the ring flashes white three times and `FOCUS DONE / 1/4 HOLD FOR BREAK` stays. The backlight cannot be seen in the sim.
6. Tap once: the card advances away; tap round to the Pomodoro card: it still says DONE (the alert does not repeat). Hold: the break starts.
7. Run it through four focus sessions: after the fourth the done screen says `HOLD FOR LONG BREAK` and `4/4`; after the long break it shows `0/4`.

Then `make clean` so later builds go back to real minutes.

- [ ] **Step 6: Verify on hardware (manual; report to the user)**

For a quick check build with short durations: temporarily add `-DPOMO_TIME_DIV=60` to `build_flags` in `platformio.ini`, flash, and remove it afterwards. Check:

1. Long-press (about 0.6 s) on the Pomodoro card starts, pauses and resumes; a very long press resets. A tap or swipe still changes card and the backlight does not change on any other card.
2. The alert pulses the ring and the backlight and jumps to the card even when you were on a payload card.
3. **Bridge unreachable (Review Focus 5):** set `BRIDGE_URL` through the portal to a dead address (e.g. `http://192.168.1.250:8787/api/status`), reboot, go to the Pomodoro card (it is the only card), run a short cycle. The timer stays correct against a stopwatch and the alert fires within a few seconds of the true end (a blocked fetch can delay it by up to the 3 s connect timeout). Restore the real bridge URL afterwards.
4. Remove the `POMO_TIME_DIV` build flag and reflash (or OTA).

- [ ] **Step 7: Commit (only if inside a git repo)**

```bash
git add firmware/src
git commit -m "feat(firmware): Pomodoro long-press controls and phase-end alert"
```

---

### Task 11: Browser preview and documentation

**Files:**
- Modify: `bridge/preview.html`, `CLAUDE.md`, `README.md`

- [ ] **Step 1: Preview CSS**

In `bridge/preview.html`, inside `<style>`, after the `.device.text .read { top: 230px; }` rule add:

```css
  /* The Pomodoro card: a ring that is time left, so no percentage and no
     threshold notches (see drawPomodoroCard in firmware/src/ui.cpp). */
  .device.nopct .read .pct { display: none; }
  .device.nonotch .notch { display: none; }
```

- [ ] **Step 2: Preview markup**

Give the two notch lines the class `notch`: change `<line x1="150.87" ...` and `<line x1="187.79" ...` to start `<line class="notch" x1=...`.

- [ ] **Step 3: Preview script**

Replace the script section from `const COLORS = ...` through the end of `paint()` with:

```js
const COLORS = { accent:'--accent', blue:'--blue', green:'--green', amber:'--amber', violet:'--violet', red:'--red', muted:'--dim' };
let data = null, idx = 0, lastFetch = 0;

// The Pomodoro card is local to the device (firmware/src/pomodoro.cpp), so it
// is not in /api/status. The mock shows fixed fake timer data; pick the state
// with ?pomo=ready|focus|paused|break|done.
const POMO = {
  ready:  { v:'25:00', s1:'Ready',      s2:'0/4  Hold to start',  c:'muted', g:100 },
  focus:  { v:'17:57', s1:'Focus',      s2:'0/4  Hold to pause',  c:'amber', g:72 },
  paused: { v:'17:57', s1:'Paused',     s2:'0/4  Hold to resume', c:'muted', g:72 },
  break:  { v:'03:30', s1:'Break',      s2:'1/4  Hold to pause',  c:'green', g:70 },
  done:   { v:'Done',  s1:'Focus done', s2:'1/4  Hold for break', c:'amber', g:0 },
};
const pomoState = new URLSearchParams(location.search).get('pomo');
const pomoCard = { t:'Pomodoro', local:true, ...(POMO[pomoState] || POMO.ready) };
const cards = () => data.cards.concat([pomoCard]);

function paint() {
  if (!data) return;
  const deck = cards();
  const card = deck[idx % deck.length];
  const color = `var(${COLORS[card.c] || '--ink'})`;
  // A card with `g` is a ring; anything else keeps the big-number layout.
  const gauge = typeof card.g === 'number';
  device.classList.toggle('text', !gauge);
  device.classList.toggle('nopct', !!card.local);
  device.classList.toggle('nonotch', !!card.local);

  t.textContent = card.t;
  v.textContent = card.v;
  v.style.color = gauge ? 'var(--ink)' : color;
  s1.textContent = card.s1 || '';
  s2.textContent = card.s2 || '';

  if (gauge) {
    const pctVal = Math.max(0, Math.min(100, card.g));
    // 75 of the circle's 100 path units are the arc, so the offset eats into
    // it from the far end: 0 is a full ring, 75 an empty one.
    arc.style.strokeDashoffset = card.g < 0 ? 75 : (75 * (1 - pctVal / 100)).toFixed(2);
    arc.style.stroke = color;
    pct.textContent = card.g < 0 ? '' : `${pctVal}%`;
    pct.style.color = color;
  }

  dots.innerHTML = deck.map((_, i) => `<b class="${i === idx ? 'on' : ''}"></b>`).join('');
  tick();
}
```

Then replace every remaining use of `data.cards.length` in the file with `cards().length`: the `device.onclick` handler, the `keydown` handler (`const n = data.cards.length;`) and the 6 s `setInterval`.

- [ ] **Step 4: Check the preview**

Run the bridge (`cd bridge && node server.mjs`) and open `http://localhost:8787/` plus `http://localhost:8787/?pomo=focus`, `?pomo=paused`, `?pomo=done`. Use the right arrow key to reach the last card. Check: the last card shows the ring, clock and bottom line like the `make shot` images; no percentage and no notch gaps on it; the other cards are unchanged and still show notches and percentages; the dots row has one more dot than before. If the bridge cannot run here, say so instead of claiming this was checked.

- [ ] **Step 5: CLAUDE.md**

Apply these edits to `CLAUDE.md`:

(a) In the "Desktop simulator" command block, after the `./build/cube-shot build/state states.json ...` line, add:

```sh
./build/cube-shot build/shot @portal       # a screen that is not a payload card: @portal, @ota, @pomo-ready|focus|paused|break|done|long
make test                                  # host-side unit tests (pomodoro, gesture, portal helpers); no SDL or board needed
make run POMO_FAST=60                      # Pomodoro minutes become seconds: watch a full cycle and the phase-end alert (make clean after)
```

(b) After the bullet that begins "Changing the contract means updating", add:

```
- The deck is the payload cards **plus one local Pomodoro card, always last** (`uiDeckSize` / `uiPomodoroIndex` in `ui.cpp`). It is not in the contract, not counted against `MAX_CARDS`, and exists even with no payload. It is the one place the firmware formats text itself (`MM:SS`, `pomoFormatTime`); everything else stays formatted by `cards.mjs`. Its state machine (`pomodoro.cpp`) and the touch gesture classifier (`gesture.cpp`) are pure and host-tested (`make test` in `firmware/sim/`). Long-press (600 ms) on that card starts/pauses/resumes, a hold to 2 s resets; tap and swipe never change.
```

(c) In the "Hardware specifics" paragraph, append a new paragraph:

```
**Settings, portal and OTA.** WiFi SSID/password, bridge URL and OTA password live in NVS (`settings.cpp`, namespace `cube`), falling back to `config.h` for any key never stored, so an empty NVS behaves exactly like before. The setup portal (`portal.cpp`: SoftAP `claude-cube-XXXX` + DNS catch-all + one form at 192.168.4.1) starts when there is no SSID, when WiFi fails to join for ~20 s (it reboots after 60 s with nobody joined, to retry), or when the screen is held through boot for 5 s. OTA is ArduinoOTA as `claude-cube.local` (see the commented `upload_protocol = espota` lines in `platformio.ini`). `settings.cpp`, `portal.cpp` and `ota.cpp` are hardware-only and have stand-ins in `firmware/sim/`.
```

- [ ] **Step 6: README.md**

Read `README.md`, then add three short sections in the firmware part (after the section that describes `config.h` and flashing; keep the README's existing voice and heading depth):

```markdown
### Changing WiFi without reflashing (setup portal)

The cube shows a setup screen when it has no network to join, when it cannot join the stored one within ~20 s, or when you hold your finger on the screen while plugging it in (keep holding for 5 s). Join the open `claude-cube-XXXX` network (scan the QR on the screen); the setup page opens by itself, or browse to `192.168.4.1`. Enter the WiFi name and password, the bridge URL and, optionally, an OTA password. A blank password keeps the saved one unless you change the network. If nobody joins within a minute the cube reboots and retries the stored network, so a router that was slow to come back after a power cut does not strand it.

### Updating over WiFi (OTA)

After the first USB flash the cube is reachable as `claude-cube.local`. In `firmware/platformio.ini` uncomment `upload_protocol = espota`, `upload_port = claude-cube.local` and (if you set an OTA password) `upload_flags = --auth=...`, then run `pio run -t upload` as usual. The screen shows a progress bar. Without an OTA password anyone on your network can flash the cube; set one in the portal.

### Pomodoro card

The last card in the deck is a Pomodoro timer that runs on the cube itself (so it works with the bridge down). **Hold** the screen for about half a second to start, pause or resume; keep holding for two seconds to reset. It runs 25 min focus, 5 min break, and a 15 min break after four focus sessions, each phase started by you. When a phase ends the cube jumps to this card and pulses the ring and backlight until you hold to start the next phase. Durations are `POMO_FOCUS_MIN`, `POMO_BREAK_MIN`, `POMO_LONG_MIN` and `POMO_SESSIONS` in `config.h`.
```

- [ ] **Step 7: Final verification sweep**

Run, and report the actual output of each:

```bash
cd firmware/sim && make clean >/dev/null && make test
cd ../ && pio run
cd sim && make && make build/cube-shot && ./build/cube-shot build/final fixtures/ring.json && ./build/cube-shot build/final @portal && ./build/cube-shot build/final @ota && ./build/cube-shot build/final @pomo-focus
```
Expected: all three test programs `0 failed`; `[SUCCESS]`; the sim builds; the shots write without error. Then re-check the hardware items listed in Tasks 4, 5 and 10 that have not yet been confirmed on the board, and tell the user plainly which of them were not run.

- [ ] **Step 8: Commit (only if inside a git repo)**

```bash
git add bridge/preview.html CLAUDE.md README.md
git commit -m "docs: Pomodoro, portal and OTA in the preview, CLAUDE.md and README"
```
