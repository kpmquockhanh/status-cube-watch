// Performance and energy bench for the firmware's main loop: `make bench`.
//
// Runs the real setup() and loop() (main.cpp, ui.cpp) against scripted stand-ins
// for touch, BLE, WiFi, the IMU and the battery, on a virtual clock (Arduino.h,
// SIM_VIRTUAL_CLOCK). Each pass is charged what it would cost on the board:
//   - compute: host time for the pass x HOST_SLOWDOWN, at the CPU clock main.cpp
//     set (half the clock = twice the time);
//   - panel: pixels x 16 bit / 80 MHz SPI, plus a few us per address window.
//     The CPU spins until the transfer ends (LovyanGFX waits for the bus at
//     endWrite), so that time counts as busy;
//   - I2C and ADC reads: fixed costs per read;
//   - delay(): idle (WAITI) at the current clock.
// The average current comes from the ESP32-S3 datasheet v2.2, Table 5-9,
// "Typ2" column (all peripheral clocks on). WAITI is idle; "single core running
// 32-bit data access instructions" is busy. The radio, panel logic and
// backlight LEDs are not in that figure; the backlight is reported on its own
// as a duty cycle.
//
//   ./build/cube-bench <desk|linked|browse|editor|sleep|volume|nav|ride|agents|alert|handover|
//                       agents-usb|agents-battery|agents-battery-still|render> [slowdown]
//
// `agents-battery` first runs `agents-battery-still` in a child process (so the
// firmware's globals start fresh) and fails if the critters' frames cost more
// than ANIM_BUDGET x the same card drawn still.

#include <ArduinoJson.h>

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../src/battery.h"
#include "../src/ble.h"
#include "../src/buzzer.h"
#include "../src/detail_layout.h"
#include "../src/detail_frame.h"
#include "../src/detail_view.h"
#include "../src/hat_picker_layout.h"
#include "../src/display.h"
#include "../src/imu.h"
#include "../src/net.h"
#include "../src/payload.h"
#include "../src/editor_layout.h"
#include "../src/settings.h"
#include "../src/touch.h"
#include "../src/ui.h"
#include "battery_env.h"
#include "nav_samples.h"

void setup();
void loop();

uint64_t g_simUs = 100000;  // the board has been up a moment when setup() runs
Display *g_simDisplay = nullptr;

uint8_t benchCardIndex();  // main.cpp
bool benchNavActive();     // main.cpp
bool benchPromptOpen();    // main.cpp
bool benchPayloadViaBle(); // main.cpp
bool benchDetailOpen();    // main.cpp: the detail screen or its hat picker
extern bool g_benchAnimOff;  // main.cpp: the still run never animates the critters

namespace {

using Clock = std::chrono::steady_clock;
FILE *g_out = nullptr;  // the report; stdout itself carries the firmware's log

// --- cost model --------------------------------------------------------------
double g_slowdown = 40.0;  // ESP32-S3 @ 240 MHz vs this host, for the same code (assumed)
constexpr double SPI_US_PER_PX = 16.0 / 80.0;  // 16 bit at 80 MHz
constexpr double WINDOW_US = 5.0;              // CASET + RASET + RAMWR, polled
constexpr double I2C_BUSY_US = 50.0;           // driver work per read, at 240 MHz
constexpr double I2C_WAIT_US = 230.0;          // bus time at 400 kHz; the task blocks
constexpr double HOST_PASS_CAP_NS = 3e6;       // a pass longer than this was preempted

struct Mhz {
  uint32_t mhz;
  double idleMa, busyMa;  // datasheet Table 5-9, Typ2
};
constexpr Mhz CLOCKS[] = {{80, 36.1, 42.6}, {160, 42.3, 54.6}, {240, 47.6, 65.9}};
constexpr int NCLK = 3;

int clockIndex(uint32_t mhz) { return mhz <= 80 ? 0 : (mhz <= 160 ? 1 : 2); }

// --- accounting --------------------------------------------------------------
uint32_t g_mhz = 240;  // board_build.f_cpu
double g_usF = 0;      // fractional part carried between advances
double g_busyUs[NCLK] = {}, g_idleUs[NCLK] = {};
uint8_t g_backlight = 0;
double g_backlightUs = 0;  // full-brightness-equivalent time
uint64_t g_px = 0, g_windows = 0, g_frames = 0, g_passes = 0, g_switches = 0, g_clamped = 0;
uint64_t g_passPx = 0;
int64_t g_panelNs = 0;
Clock::time_point g_panelT0;
double g_frameComputeUs = 0;  // compute charged to passes that sent pixels

void advance(double us) {
  g_usF += us;
  const uint64_t whole = (uint64_t)g_usF;
  g_usF -= (double)whole;
  g_simUs += whole;
  g_backlightUs += us * g_backlight / 255.0;
}

void busyFixed(double us) {  // takes this long whatever the clock (SPI spin, delayMicroseconds)
  g_busyUs[clockIndex(g_mhz)] += us;
  advance(us);
}

void busyWork(double us240) {  // CPU work measured at 240 MHz: slower at a lower clock
  busyFixed(us240 * 240.0 / g_mhz);
}

void idle(double us) {
  g_idleUs[clockIndex(g_mhz)] += us;
  advance(us);
}

void resetCounters() {
  for (int i = 0; i < NCLK; i++) g_busyUs[i] = g_idleUs[i] = 0;
  g_backlightUs = 0;
  g_px = g_windows = g_frames = g_passes = g_switches = g_clamped = 0;
  g_frameComputeUs = 0;
}

// --- the scenario script ---------------------------------------------------
struct Stroke {
  uint32_t t0, dur;  // ms from the scenario's start
  int16_t x0, y0, x1, y1;
};

struct Scenario {
  const char *name;
  const char *what;
  uint32_t seconds;
  bool bonded, bleLive;
  // The iPhone: `nav` names a nav_samples.h scene it sends every 200 ms while
  // the scenario runs (nullptr = no phone).
  const char *nav;
  uint32_t navFromMs;  // the phone starts sending this long into the run
  // Screen sleep, set by the scenario so the numbers do not depend on whose
  // config.h the bench is built with. `mustSleep`: the run fails unless the
  // screen is off at the end.
  uint8_t sleepMin;
  bool mustSleep;
  uint16_t rotateSec;  // auto-advance, 0 = off
  // The Agents card after the usage card: AGENTS_RUNNING is four working
  // agents; AGENTS_ALERT the same until ALERT_AT_MS, then one waits on a prompt;
  // AGENTS_HANDOVER has it waiting from the start. AGENTS_DECK is a payload that
  // never changes, the Agents card first (the card boot lands on): two at work,
  // a hungry one, an idle one, nobody waiting (so no alert).
  // AGENTS_DETAIL is AGENTS_DECK whose checkout-api (l00002) waits, then goes,
  // in the windows the `detail` scenario names.
  enum : uint8_t { AGENTS_NONE, AGENTS_RUNNING, AGENTS_ALERT, AGENTS_HANDOVER, AGENTS_DECK, AGENTS_DETAIL } agents;
  // CUBE_POWER=battery for the run (batteryOnExternal() false; usb otherwise),
  // and the still variant: the critter pacer forced off.
  bool battery;
  bool animOff;
  // BLE payloads start this long into the run (0 = at once); until then WiFi
  // delivers, and for `handover` its fetches answer with the LAN variant.
  uint32_t bleFromMs;
  // A Mac pairs during [pairFromMs, pairToMs): the passkey screen covers
  // everything (0, 0 = never).
  uint32_t pairFromMs, pairToMs;
  std::vector<Stroke> strokes;
};

Scenario S;
uint8_t g_endCard = 0;
uint32_t g_start = 0;  // millis() when the scenario's clock starts (after setup)
bool g_running = false;

// `detail`: when the tile is tapped, back is tapped, and the tile again; then
// the hat picker (critter, a shape, a colour, back to the detail); the tile
// waits during [DETAIL_WAIT_MS, DETAIL_WAIT_END_MS) (the payload closes the
// detail); open again, the tile is gone during [DETAIL_GONE_MS,
// DETAIL_GONE_END_MS) (closed again); open again with the picker up and
// left: the screen's sleep closes both.
constexpr uint32_t DETAIL_OPEN_MS = 2000, DETAIL_CLOSE_MS = 20000, DETAIL_REOPEN_MS = 22000;
constexpr uint32_t DETAIL_PICKER_MS = 26000, DETAIL_SHAPE_MS = 28000, DETAIL_COLOR_MS = 30000,
                   DETAIL_PICK_BACK_MS = 32000;
constexpr uint32_t DETAIL_WAIT_MS = 38000, DETAIL_WAIT_END_MS = 48000, DETAIL_OPEN2_MS = 52000;
// `detail-ride`: the phone's first frame.
constexpr uint32_t DETAIL_RIDE_MS = 10000;
constexpr uint32_t DETAIL_GONE_MS = 58000, DETAIL_GONE_END_MS = 68000, DETAIL_OPEN3_MS = 72000,
                   DETAIL_PICKER2_MS = 74000;

Stroke tapAt(uint32_t t, int x, int y) { return {t, 80, (int16_t)x, (int16_t)y, (int16_t)x, (int16_t)y}; }
Stroke swipe(uint32_t t, int x0, int y0, int x1, int y1) {
  return {t, 220, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
}
Stroke tapZone(uint32_t t, const EditRect &r) { return tapAt(t, r.x + r.w / 2, r.y + r.h / 2); }

Scenario makeScenario(const std::string &name) {
  Scenario s{};
  s.bonded = true;
  s.bleLive = true;
  s.sleepMin = 15;  // the repo default: longer than every scenario that should stay awake
  if (name == "desk") {
    s.what = "usage card, Mac pushing every 5 s, nobody touching it";
    s.seconds = 600;
  } else if (name == "linked") {
    s.what = "as desk with a 2 min sleep: the screen goes off though the Mac keeps pushing";
    s.seconds = 600;
    s.sleepMin = 2;
    s.mustSleep = true;
  } else if (name == "browse") {
    s.what = "a swipe every 4 s (ring sweep on each card)";
    s.seconds = 120;
    for (uint32_t t = 1000; t < 120000; t += 4000) s.strokes.push_back(swipe(t, 190, 150, 50, 150));
  } else if (name == "editor") {
    s.what = "display panel: open, 8 steps, a toggle, close; open again: a step, DONE";
    s.seconds = 15;
    s.strokes.push_back(swipe(1000, 120, 60, 120, 220));  // swipe down: open
    uint32_t t = 2000;
    for (int i = 0; i < 4; i++, t += 600) s.strokes.push_back(tapZone(t, editorPlus(0)));
    for (int i = 0; i < 4; i++, t += 600) s.strokes.push_back(tapZone(t, editorMinus(0)));
    s.strokes.push_back(tapZone(t, editorLead(1)));
    t += 600;
    s.strokes.push_back(tapZone(t, editorLead(1)));
    t += 1000;
    s.strokes.push_back(swipe(t, 120, 220, 120, 60));  // swipe up: close
    t += 1000;
    s.strokes.push_back(swipe(t, 120, 60, 120, 220));  // swipe down: open again
    t += 1000;
    s.strokes.push_back(tapZone(t, editorPlus(0)));
    t += 600;
    s.strokes.push_back(tapZone(t, editorDoneBtn()));
  } else if (name == "volume") {
    s.what = "Volume card: a drag up the card, then a tap on the number (the Mac echoes each)";
    s.seconds = 6;
    s.strokes.push_back(swipe(1000, 190, 150, 50, 150));       // swipe left: usage -> Volume
    s.strokes.push_back(Stroke{2000, 600, 120, 180, 120, 60});  // drag up, above the button row
    s.strokes.push_back(tapAt(3500, VOL_MUTE_CX, VOL_MUTE_CY));  // the number: mute
  } else if (name == "nav") {
    s.what = "navigation: Mac and iPhone bonded, a busy frame every 200 ms, rider moving, full brightness";
    s.seconds = 600;
    s.nav = "busy";
    s.rotateSec = 5;  // auto-advance is on: it must not move the deck during the ride
  } else if (name == "ride") {
    s.what = "navigation, Mac bonded but away: busy frames every 200 ms, WiFi configured must stay off";
    s.seconds = 600;
    s.nav = "busy";
    s.bleLive = false;
  } else if (name == "agents") {
    s.what = "Agents grid, 4 agents at work (lines, poses, context move every 5 s): the screen still sleeps (2 min)";
    s.seconds = 600;
    s.sleepMin = 2;
    s.mustSleep = true;
    s.agents = Scenario::AGENTS_RUNNING;
    s.strokes.push_back(swipe(6000, 190, 150, 50, 150));  // swipe left: usage -> Agents, once a payload is in
  } else if (name == "alert") {
    s.what = "asleep on the usage card; at 300 s an agent waits on a prompt: wake, jump to it, one beep; covered by a passkey, a tap right after it is refused, a later one answers";
    s.seconds = 330;
    s.sleepMin = 2;
    s.agents = Scenario::AGENTS_ALERT;
    s.pairFromMs = 305000;  // the passkey covers the prompt for 10 s
    s.pairToMs = 315000;
  } else if (name == "handover") {
    s.what = "an agent waits; WiFi (LAN variant, no prompt) until BLE starts at 40 s; BLE wins while live, one beep";
    s.seconds = 120;
    s.agents = Scenario::AGENTS_HANDOVER;
    s.bleFromMs = 40000;
  } else if (name == "agents-usb") {
    s.what = "Agents card on screen, external power: critters at 10 frames/s";
    s.seconds = 120;
    s.agents = Scenario::AGENTS_DECK;
  } else if (name == "agents-battery" || name == "agents-battery-still") {
    s.animOff = name == "agents-battery-still";
    s.what = s.animOff ? "as agents-battery, the critters never move (the budget's baseline)"
                       : "Agents card on screen, on battery: critters at 2 frames/s, dark after 2 min";
    s.seconds = 130;  // the first payload lands at once: ~120 s on screen, then the screen sleeps
    s.sleepMin = 2;
    s.mustSleep = true;
    s.battery = true;
    s.agents = Scenario::AGENTS_DECK;
  } else if (name == "detail") {
    s.what = "Agents card: tap a working tile (0A, again every 5 s), back (0A 00 fails, goes 1 s later); open, pick a hat (0B x2); the tile "
             "waits, then goes (0A 00 each); picker left open: sleep at 2 min closes it (0A 00)";
    s.seconds = 210;
    s.sleepMin = 2;
    s.mustSleep = true;
    s.agents = Scenario::AGENTS_DETAIL;
    const LayoutRect wizard = hatPickCell(4), yellow = hatPickSwatch(2);
    const int critX = DETAIL_CRITTER.x + DETAIL_CRITTER.w / 2, critY = DETAIL_CRITTER.y + DETAIL_CRITTER.h / 2;
    s.strokes.push_back(tapAt(DETAIL_OPEN_MS, 177, 100));   // tile 1, checkout-api, running
    s.strokes.push_back(tapAt(DETAIL_CLOSE_MS, 40, 44));    // back
    s.strokes.push_back(tapAt(DETAIL_REOPEN_MS, 177, 100));  // the same tile
    s.strokes.push_back(tapAt(DETAIL_PICKER_MS, critX, critY));  // its critter: the picker
    s.strokes.push_back(tapAt(DETAIL_SHAPE_MS, wizard.x + wizard.w / 2, wizard.y + wizard.h / 2));
    s.strokes.push_back(tapAt(DETAIL_COLOR_MS, yellow.x + yellow.w / 2, yellow.y + yellow.h / 2));
    s.strokes.push_back(tapAt(DETAIL_PICK_BACK_MS, 40, 44));  // back to the detail, which stays open
    s.strokes.push_back(tapAt(DETAIL_OPEN2_MS, 177, 100));    // after the wait: open again
    s.strokes.push_back(tapAt(DETAIL_OPEN3_MS, 177, 100));    // after it was gone: open again
    s.strokes.push_back(tapAt(DETAIL_PICKER2_MS, critX, critY));  // the picker, left up
  } else if (name == "detail-ride") {
    s.what = "Agents card: a working tile's detail open (0A), then a ride starts: one 0A 00, nothing left open";
    s.seconds = 30;
    s.agents = Scenario::AGENTS_DECK;
    s.nav = "turn";
    s.navFromMs = DETAIL_RIDE_MS;
    s.strokes.push_back(tapAt(DETAIL_OPEN_MS, 177, 100));  // tile 1, checkout-api, running
  } else if (name == "sleep") {
    s.what = "Mac gone: WiFi fetches fail, screen sleeps after the timeout (2 min)";
    s.seconds = 600;
    s.bleLive = false;
    s.sleepMin = 2;
    s.mustSleep = true;
  } else {
    s.name = nullptr;
    return s;
  }
  s.name = strdup(name.c_str());
  return s;
}

constexpr uint32_t ALERT_AT_MS = 300000;
constexpr double ANIM_BUDGET = 1.2;  // agents-battery's MCU mA over agents-battery-still's, at most

// The scenario's Agents card (none for most): four agents at work, whose lines,
// poses and context move every 5 s as the bridge's would; for `alert`, from
// ALERT_AT_MS the first one waits on a prompt (for `handover`, from the start).
// `lan`: the variant WiFi gets, without the prompt.
std::string agentsJson(uint32_t sinceStartMs, bool lan) {
  if (S.agents == Scenario::AGENTS_NONE) return "";
  if (S.agents == Scenario::AGENTS_DECK || S.agents == Scenario::AGENTS_DETAIL) {
    // no comma in front: payloadJson puts it first
    const bool detail = S.agents == Scenario::AGENTS_DETAIL;
    const bool waits = detail && sinceStartMs >= DETAIL_WAIT_MS && sinceStartMs < DETAIL_WAIT_END_MS;
    const bool gone = detail && sinceStartMs >= DETAIL_GONE_MS && sinceStartMs < DETAIL_GONE_END_MS;
    const char *second =
        gone    ? ""
        : waits ? "{\"i\":\"l00002\",\"n\":\"checkout-api\",\"l\":\"allow bash?\",\"k\":\"wait\",\"p\":\"wait\",\"x\":41,\"h\":17},"
                : "{\"i\":\"l00002\",\"n\":\"checkout-api\",\"l\":\"edit . 41% ctx\",\"k\":\"run\",\"p\":\"write\",\"x\":41,\"h\":17},";
    return std::string("{\"t\":\"AGENTS\",\"v\":\"4\",\"s1\":\"2 WORKING\",\"s2\":\"\",\"c\":\"ink\",\"a\":["
                       "{\"i\":\"l00001\",\"n\":\"infra-network\",\"l\":\"bash . 12% ctx\",\"k\":\"run\",\"p\":\"bash\",\"x\":12,\"h\":10},") +
           second +
           "{\"i\":\"l00003\",\"n\":\"docs-site\",\"l\":\"hungry . feed me\",\"k\":\"idle\",\"p\":\"hungry\",\"x\":3,\"u\":1,\"h\":63},"
           "{\"i\":\"l00004\",\"n\":\"claude-status...\",\"l\":\"idle\",\"k\":\"idle\",\"p\":\"idle\",\"x\":8,\"h\":30}]}";
  }
  static const char *const names[4] = {"infra-network", "checkout-api", "docs-site", "claude-status..."};
  static const char *const poses[5] = {"bash", "read", "write", "think", "web"};
  const bool waiting = S.agents == Scenario::AGENTS_HANDOVER ||
                       (S.agents == Scenario::AGENTS_ALERT && sinceStartMs >= ALERT_AT_MS);
  const uint32_t tick = S.agents == Scenario::AGENTS_RUNNING ? sinceStartMs / 5000 : 0;
  std::string a;
  for (int i = 0; i < 4; i++) {
    char t[160];
    if (i == 0 && waiting) {
      snprintf(t, sizeof(t), "{\"i\":\"l00001\",\"n\":\"%s\",\"l\":\"needs you\",\"k\":\"wait\",\"p\":\"wait\",\"x\":12}",
               names[i]);
    } else {
      snprintf(t, sizeof(t), "{\"i\":\"l0000%d\",\"n\":\"%s\",\"l\":\"Bash %u\",\"k\":\"run\",\"p\":\"%s\",\"x\":%u}",
               i + 1, names[i], (unsigned)(tick % 100), poses[(tick + i) % 5], (unsigned)((tick * 7 + i * 13) % 100));
    }
    if (i) a += ",";
    a += t;
  }
  std::string card = std::string(",{\"t\":\"AGENTS\",\"v\":\"4\",\"s1\":\"") + (waiting ? "1 NEEDS YOU" : "4 WORKING") +
                     "\",\"s2\":\"\",\"c\":\"" + (waiting ? "amber" : "green") + "\",\"a\":[" + a + "]";
  if (waiting && !lan) {
    card += ",\"q\":{\"i\":\"l00001\",\"p\":\"p1abc-1\",\"k\":\"permission\",\"t\":\"run this command?\","
            "\"d\":\"terraform apply -target=module.staging_net\",\"c\":\"~/code/infra-network\","
            "\"o\":[{\"k\":\"1\",\"l\":\"yes\"},{\"k\":\"2\",\"l\":\"yes, and don't ask again\"},{\"k\":\"3\",\"l\":\"no\"}]}";
  }
  return card + "}";
}

// The usage card from fixtures/usage.json, with the countdown running down a
// minute at a time and the 5h percentage creeping up, as the bridge sends it,
// then the scenario's Agents card.
// For AGENTS_DECK (and AGENTS_DETAIL) the Agents card comes first, so the deck starts on it.
std::string payloadJson(uint32_t nowMs, bool lan = false) {
  const std::string agents = agentsJson(g_running ? nowMs - g_start : 0, lan);
  static std::string usage;
  static int cachedMin = -1;
  // `detail` keeps it still: its sleep must come from the script alone, not a gauge step.
  const int min = S.agents == Scenario::AGENTS_DETAIL ? 0 : (int)(nowMs / 60000);
  if (min != cachedMin) {
    cachedMin = min;
    const int left = 249 - min > 0 ? 249 - min : 0;
    const int g = 14 + min / 3;
    char reset[16];
    snprintf(reset, sizeof(reset), "%dh %02dm", left / 60, left % 60);
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"CLAUDE\",\"v\":\"%s\","
             "\"s1\":\"to reset\",\"s2\":\"\",\"c\":\"ink\",\"g\":%d,\"c2\":\"ink\",\"g2\":53,"
             "\"rows\":[{\"k\":\"5H\",\"p\":\"%d%%\",\"r\":\"%s\"},{\"k\":\"7D\",\"p\":\"53%%\",\"r\":\"1d 1h\"}],"
             "\"m\":\"12\",\"mc\":\"amber\"}",
             reset, g, g, reset);
    usage = buf;
  }
  const std::string head = "{\"v\":1,\"ts\":0,\"src\":\"oauth\",\"cards\":[";
  if (S.agents == Scenario::AGENTS_DECK || S.agents == Scenario::AGENTS_DETAIL) return head + agents + "," + usage + "]}";
  return head + usage + agents + "]}";
}

bool parsePayload(const std::string &json, Payload &out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  static Payload scratch;
  char err[64];
  if (!payloadFromJson(doc, scratch, err, sizeof(err))) return false;
  out = scratch;
  return true;
}

uint32_t g_bleLastPush = 0, g_bleLastGood = 0;
// The Mac's side of the Volume card: its state once the link is live, then an
// echo of each request. Writes are write-with-response and the Mac keeps one in
// flight, the newest request waiting (LatestWinsGate): the first lands 300 ms
// after the request (about how slow the real link is), each queued one 90 ms
// after the one before.
bool g_volInFlight = false;
uint32_t g_volDoneAt = 0;
MacVolume g_volWriting{};  // the state of the write in flight
bool g_volHasPending = false;
MacVolume g_volPending{};
uint32_t g_volLastReqAt = 0;
uint8_t g_volLastReqLevel = 0;
uint8_t g_volLastEchoLevel = 0;
bool g_volEchoed = false;
uint32_t g_volStaleLate = 0;  // echoes that landed 600 ms+ after the last request with another level
bool g_volSent = false;
uint32_t g_volRequests = 0;
uint32_t g_volPlain = 0;  // requests with muted == false
bool g_volLastMuted = false;
MacVolume g_volEcho{};
// What a Settings read over BLE would return: net_ble.cpp publishes it at
// bleBegin() and again whenever it is told the stored settings changed.
DeviceSettings g_pubDevice{};
uint64_t g_unpublished = 0;  // loop passes that ended with a read returning stale settings
void publishSettings() {
  g_pubDevice = deviceSettings();
}
uint32_t g_lastImuCost = 0;

double hostNs(Clock::time_point a, Clock::time_point b) {
  return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

}  // namespace

// --- hooks the bench's panel and Arduino shim call -----------------------------
void simDelayUs(uint64_t us) { idle((double)us); }

void simSetCpuMhz(uint32_t mhz) {
  if (mhz != g_mhz) g_switches++;
  g_mhz = mhz;
}

void benchPanelEnter() { g_panelT0 = Clock::now(); }

void benchPanelLeave(uint32_t pixels, uint32_t windows) {
  g_panelNs += (int64_t)hostNs(g_panelT0, Clock::now());
  g_px += pixels;
  g_passPx += pixels;
  g_windows += windows;
  busyFixed(pixels * SPI_US_PER_PX + windows * WINDOW_US);
}

void benchBacklight(uint8_t level) { g_backlight = level; }
void benchPanelSleep(bool) {}

// Every frame the partial push sends must leave the glass equal to the sprite.
// Timed like a panel write that sends nothing, so the check costs the board 0.
uint64_t g_checked = 0, g_stale = 0;
void benchFrameSent(const uint16_t *frame) {
  if (g_simDisplay->getRotation() != 0) return;  // the panel keeps rows in its own orientation
  benchPanelEnter();
  const BenchPanel *panel = static_cast<const BenchPanel *>(g_simDisplay->getPanel());
  g_checked++;
  for (int y = 0; y < LCD_HEIGHT; y++) {
    if (memcmp(panel->line(y), frame + y * LCD_WIDTH, LCD_WIDTH * 2) != 0) {
      g_stale++;
      break;
    }
  }
  benchPanelLeave(0, 0);
}

// --- stand-ins: touch ---------------------------------------------------------
bool Touch::begin() {
  _present = true;
  return true;
}

bool Touch::read(int16_t &x, int16_t &y) {
  busyWork(I2C_BUSY_US);
  idle(I2C_WAIT_US);
  if (!g_running) return false;  // nobody holds the screen at boot
  const uint32_t t = millis() - g_start;
  for (const Stroke &s : S.strokes) {
    if (t < s.t0 || t >= s.t0 + s.dur) continue;
    const float f = (float)(t - s.t0) / (float)s.dur;
    x = (int16_t)(s.x0 + (s.x1 - s.x0) * f);
    y = (int16_t)(s.y0 + (s.y1 - s.y0) * f);
    return true;
  }
  return false;
}

// --- stand-ins: BLE -------------------------------------------------------------
void bleBegin() { publishSettings(); }
BleState bleState() {
  if (g_running && S.pairToMs) {
    const uint32_t t = millis() - g_start;
    if (t >= S.pairFromMs && t < S.pairToMs) return BleState::Pairing;
  }
  return S.bleLive ? BleState::Connected : BleState::Advertising;
}
uint32_t blePasskey() { return 0; }
bool bleMacBonded() { return S.bonded; }
bool blePhoneBonded() { return S.nav != nullptr; }
bool bleUnknownBonded() { return false; }
bool bleBonded() { return bleMacBonded() || blePhoneBonded(); }
void bleForget(BondKind) {}
void bleRequestPairingAtBoot() {}
bool blePairingOpen() { return false; }

// The phone's feed: the scenario's sample, one frame every 200 ms.
bool bleTakeNav(NavScene &out) {
  static uint32_t last = 0;
  static bool sent = false;
  if (!S.nav || !g_running) return false;
  const uint32_t now = millis();
  if (now - g_start < S.navFromMs) return false;
  if (sent && now - last < 200) return false;
  if (!nav_samples::navSample(S.nav, out)) return false;
  // The rider moves: the whole map slides 2 px down per frame and wraps, so
  // every frame differs from the last one.
  static int shift = 0;
  shift += 2;
  const auto slide = [](NavPoint &p) {
    int y = p.y + shift % 400;
    if (y > 340) y -= 400;
    p.y = (int16_t)y;
  };
  for (uint16_t i = 0; i < out.nRoute; i++) slide(out.route[i]);
  for (uint16_t i = 0; i < out.nStreetPts; i++) slide(out.streetPts[i]);
  sent = true;
  last = now;
  return true;
}

// The Mac's PushPolicy sends every 5 s whether or not anything changed.
bool bleTake(Payload &out) {
  if (!S.bleLive || !g_running) return false;
  const uint32_t now = millis();
  if (now - g_start < S.bleFromMs) return false;
  if (g_bleLastPush && now - g_bleLastPush < 5000) return false;
  g_bleLastPush = now ? now : 1;
  if (!parsePayload(payloadJson(now), out)) return false;
  g_bleLastGood = g_bleLastPush;
  return true;
}

uint32_t bleLastGood() { return g_bleLastGood; }
bool bleTakeSettings(char *, size_t) { return false; }
void bleSettingsReply(uint8_t) { publishSettings(); }
void bleSettingsChanged() { publishSettings(); }

bool bleTakeVolume(MacVolume &out) {
  if (!S.bleLive || !g_running) return false;
  if (!g_volSent) {
    g_volSent = true;
    g_volEcho = MacVolume{};
    g_volEcho.known = true;
    g_volEcho.level = 40;
    g_volEcho.canSet = g_volEcho.canMute = true;
    snprintf(g_volEcho.name, sizeof(g_volEcho.name), "MacBook Pro Speakers");
    out = g_volEcho;
    return true;
  }
  if (g_volInFlight && millis() >= g_volDoneAt) {
    out = g_volWriting;
    g_volEchoed = true;
    g_volLastEchoLevel = out.level;
    if (g_volLastReqLevel != out.level && millis() - g_volLastReqAt >= 600) g_volStaleLate++;
    if (g_volHasPending) {
      g_volWriting = g_volPending;
      g_volHasPending = false;
      g_volDoneAt = millis() + 90;
    } else {
      g_volInFlight = false;
    }
    return true;
  }
  return false;
}

void bleSendMedia(MediaKey) {}
void bleSendUnlock() {}
uint32_t g_answers = 0;  // Control 09 sends
uint32_t g_answerAt = 0;  // ms into the run of the last one
bool bleSendAnswer(const char *, const char *, char) {
  g_answers++;
  g_answerAt = millis() - g_start;
  return true;
}
void bleSendNavPan(int16_t, int16_t) {}
uint32_t g_details = 0;  // Control 0A sends
// Control 0A with an agent: the first few, in ms into the run; and 0A 00 (closed).
constexpr int DETAIL_LOG = 64;
uint32_t g_detailAt[DETAIL_LOG], g_detailCloseAt[DETAIL_LOG];
uint32_t g_detailOpens = 0, g_detailCloses = 0;
// `detail`: the first 0A 00 cannot go out (as a failed notify), so it is tried again.
uint32_t g_detailCloseFails = 0, g_detailCloseFailAt = 0;
bool bleSendDetail(const char *agent) {
  g_details++;
  if (!g_running) return true;
  const uint32_t t = millis() - g_start;
  if (!(agent && agent[0]) && !strcmp(S.name, "detail") && g_detailCloseFails == 0) {
    g_detailCloseFails++;
    g_detailCloseFailAt = t;
    return false;
  }
  if (agent && agent[0]) {
    if (g_detailOpens < DETAIL_LOG) g_detailAt[g_detailOpens] = t;
    g_detailOpens++;
  } else {
    if (g_detailCloses < DETAIL_LOG) g_detailCloseAt[g_detailCloses] = t;
    g_detailCloses++;
  }
  return true;
}
uint32_t g_hats = 0;  // Control 0B sends, and the first two: agent and hat
uint8_t g_hatSent[2];
char g_hatAgent[2][16];
bool bleSendHat(const char *agent, uint8_t hat) {
  if (g_hats < 2) {
    g_hatSent[g_hats] = hat;
    snprintf(g_hatAgent[g_hats], sizeof(g_hatAgent[0]), "%s", agent ? agent : "");
  }
  g_hats++;
  return true;
}

void bleSendVolume(uint8_t level, bool muted) {
  g_volRequests++;
  if (!muted) g_volPlain++;
  g_volLastMuted = muted;
  g_volEcho.level = level;
  g_volEcho.muted = muted;
  g_volLastReqAt = millis();
  g_volLastReqLevel = level;
  if (g_volInFlight) {
    g_volPending = g_volEcho;
    g_volHasPending = true;
  } else {
    g_volWriting = g_volEcho;
    g_volInFlight = true;
    g_volDoneAt = millis() + 300;
  }
}

// --- stand-ins: WiFi -----------------------------------------------------------
// Joined, but the bridge does not answer: each fetch is refused after a short
// wait. The board waits on its fetch task, so the loop is not charged for it.
constexpr uint32_t FETCH_REFUSED_MS = 20;
bool g_wifi = false;
uint32_t g_fetchAt = 0;
bool g_fetching = false;
uint32_t g_wifiRideStarts = 0;  // WiFi brought up while a ride ran
uint64_t g_wifiRidePasses = 0;  // loop passes that ended with a ride running and WiFi up
void wifiUp() {
  if (g_running && benchNavActive()) g_wifiRideStarts++;
  g_wifi = true;
}
bool netBegin() {
  wifiUp();
  return true;
}
bool netOnline() { return g_wifi; }
void netStart() { wifiUp(); }
void netJoinSaved() { wifiUp(); }
void netStop() {
  g_wifi = false;
  g_fetching = false;
}
void netRequest() {
  if (g_fetching) return;
  g_fetching = true;
  g_fetchAt = millis();
}
uint32_t g_wifiOkWhileLive = 0;  // `handover`: WiFi payloads delivered while BLE was live
NetFetch netTake(Payload &out) {
  if (!g_fetching || millis() - g_fetchAt < FETCH_REFUSED_MS) return NetFetch::None;
  g_fetching = false;
  if (S.agents == Scenario::AGENTS_HANDOVER && parsePayload(payloadJson(millis(), true), out)) {
    if (g_bleLastGood && millis() - g_bleLastGood < 15000) g_wifiOkWhileLive++;
    return NetFetch::Ok;
  }
  return NetFetch::Failed;
}
const char *netLastError() { return "refused (bench)"; }

// --- stand-ins: IMU and battery --------------------------------------------------
bool imuBegin() { return true; }
bool imuReadAccel(float &ax, float &ay, float &az) {
  busyWork(I2C_BUSY_US);
  idle(I2C_WAIT_US);
  ax = (float)IMU_UP_SIGN;  // upright: main.cpp multiplies by IMU_UP_SIGN again
  ay = az = 0.0f;
  return true;
}

void batteryBegin() {}
void batteryHoldPower() {}
void batteryUpdate(uint32_t now) {
  static uint32_t last = 0;
  if (last && now - last < 5000) return;
  last = now ? now : 1;
  busyFixed(16 * (25.0 + 200.0));  // 16 ADC reads, delayMicroseconds(200) between them
}
BatteryView batteryView() { return batteryViewFromEnv(); }
bool batteryOnExternal() { return powerExternalFromEnv(); }

// --- stand-in: buzzer --------------------------------------------------------------
// Silent. A tone is LEDC hardware time, not CPU, so the model charges nothing for it.
bool buzzerBegin() { return true; }
void buzzerSetLevel(uint8_t) {}
uint32_t g_attention = 0;  // Sound::Attention plays: the agent alert
void buzzerPlay(Sound s) {
  if (s == Sound::Attention) g_attention++;
}
void buzzerUpdate(uint32_t) {}

// --- running ---------------------------------------------------------------------
namespace {

uint64_t g_wifiWonPasses = 0;  // passes that ended with BLE live but WiFi's payload applied

// Returns the modelled MCU current, mA.
double runScenario() {
  // Every run names its power source, whatever the caller's environment says.
  setenv("CUBE_POWER", S.battery ? "battery" : "usb", 1);
  g_benchAnimOff = S.animOff;
  setup();  // loads the config.h defaults, so the scenario's timers go in after it
  DeviceSettings d = deviceSettings();
  d.sleepMin = S.sleepMin;
  d.rotateSec = S.rotateSec;
  if (S.agents == Scenario::AGENTS_HANDOVER) d.pollSec = 5;  // WiFi fetches overlap BLE several times
  deviceSettingsSave(d);
  publishSettings();  // as if stored before boot
  if (!settingsHaveWifi()) {  // the scenarios assume a network is configured, whoever's config.h this is
    Settings w = settings();
    snprintf(w.ssid, sizeof(w.ssid), "bench");
    settingsSave(w);
  }
  // The alert's prompt is answered with a tap on its first button, well after
  // the settle time; where that button sits is what uiAgentPrompt draws.
  if (S.agents == Scenario::AGENTS_ALERT) {
    static Payload p;
    g_running = true;  // agentsJson counts from the start: build the payload of ALERT_AT_MS
    g_start = millis() - ALERT_AT_MS;
    parsePayload(payloadJson(millis()), p);
    g_running = false;
    const PromptLayout L = uiPromptLayout(p.agents.prompt, ButtonsOff::No);
    const int16_t y = PROMPT_VIEW_Y + L.buttonY(0) + PROMPT_BTN_H / 2;
    // Just after the passkey screen goes, the prompt is settling again: no answer.
    S.strokes.push_back(tapAt(S.pairToMs + 300, 120, y));
    // Once it has been readable for PROMPT_SETTLE_MS: one answer.
    S.strokes.push_back(tapAt(S.pairToMs + 2000, 120, y));
  }
  resetCounters();
  g_start = millis();
  g_running = true;
  const uint64_t endUs = g_simUs + (uint64_t)S.seconds * 1000000ull;
  while (g_simUs < endUs) {
    g_panelNs = 0;
    g_passPx = 0;
    const auto t0 = Clock::now();
    loop();
    // The display panel saves on the cube itself; by the end of that pass the
    // Mac's next Settings read must see what it saved.
    if (g_pubDevice != deviceSettings()) g_unpublished++;
    if (benchNavActive() && g_wifi) g_wifiRidePasses++;
    // BLE is live (a push in the last 15 s, the policy's BLE_LIVE_MS), yet the payload is WiFi's.
    if (g_bleLastGood && millis() - g_bleLastGood < 15000 && !benchPayloadViaBle()) g_wifiWonPasses++;
    double ns = hostNs(t0, Clock::now()) - (double)g_panelNs;
    if (ns > HOST_PASS_CAP_NS) {
      ns = HOST_PASS_CAP_NS;
      g_clamped++;
    }
    const double us240 = ns / 1000.0 * g_slowdown;
    busyWork(us240);
    g_passes++;
    if (g_passPx) {
      g_frames++;
      g_frameComputeUs += us240;
    }
  }

  g_endCard = benchCardIndex();
  const double total = (double)S.seconds * 1e6;
  double charge = 0, busy = 0;
  for (int i = 0; i < NCLK; i++) {
    charge += g_busyUs[i] * CLOCKS[i].busyMa + g_idleUs[i] * CLOCKS[i].idleMa;
    busy += g_busyUs[i];
  }
  const double mins = S.seconds / 60.0;
  fprintf(g_out, "%-9s %7.0f %9.2f %7.1f %6.1f%% %5.0f%% %5.0f%% %7.2f %7.1f %5.0f%%  %s\n", S.name, g_frames / mins,
         g_px / mins / 1e6, g_frames ? g_frameComputeUs / g_frames / 1000.0 : 0.0, busy / total * 100.0,
         (g_busyUs[2] + g_idleUs[2]) / total * 100.0, (g_busyUs[0] + g_idleUs[0]) / total * 100.0,
         g_passes / (double)S.seconds, charge / total, g_backlightUs / total * 100.0, S.what);
  if (g_clamped) fprintf(g_out, "          (%llu passes clamped at %.0f ms host time)\n", (unsigned long long)g_clamped,
                        HOST_PASS_CAP_NS / 1e6);
  return charge / total;
}

// Runs `name` in a child process (fresh firmware globals), which prints its own
// row, and returns its modelled mA (< 0 if it failed or, when it must, did not sleep).
double runChild(const std::string &name) {
  int fd[2];
  if (pipe(fd) != 0) return -1;
  fflush(g_out);
  const pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    close(fd[0]);
    S = makeScenario(name);
    double ma = runScenario();
    if (S.mustSleep && g_backlight != 0) ma = -1;
    fflush(g_out);
    _exit(write(fd[1], &ma, sizeof(ma)) == (ssize_t)sizeof(ma) ? 0 : 1);
  }
  close(fd[1]);
  double ma = -1;
  if (read(fd[0], &ma, sizeof(ma)) != (ssize_t)sizeof(ma)) ma = -1;
  close(fd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? ma : -1;
}

// Host time of one call of `fn`, less the time spent copying into the panel:
// the median of `n` runs, so a preempted run does not count.
template <typename F>
double medianUs(int n, F fn, uint64_t *px) {
  std::vector<double> t;
  for (int i = 0; i < n; i++) {
    g_panelNs = 0;
    g_passPx = 0;
    const auto t0 = Clock::now();
    fn();
    t.push_back((hostNs(t0, Clock::now()) - (double)g_panelNs) / 1000.0);
    g_simUs += 1000000;  // a second later: animations settle, the age label moves
  }
  if (px) *px = g_passPx;
  std::sort(t.begin(), t.end());
  return t[t.size() / 2];
}

void runRender() {
  settingsLoad();
  Display lcd;
  lcd.init();
  uiBegin(lcd);
  static Payload p;
  parsePayload(payloadJson(0), p);
  const BatteryView bv = batteryView();
  VolumeSlider volume;
  MacVolume mv{};
  mv.known = true;
  mv.level = 56;
  mv.canSet = mv.canMute = true;
  snprintf(mv.name, sizeof(mv.name), "MacBook Pro Speakers");
  volume.fromMac(mv, 0);
  const VolumeView vv = volume.view();

  static NavScene navTurn, navBusy;
  nav_samples::navSample("turn", navTurn);
  nav_samples::navSample("busy", navBusy);

  struct Case {
    const char *name;
    std::function<void()> draw;
  };
  const Case cases[] = {
      {"usage card", [&] { uiRender(lcd, p, 0, true, 2000, bv, UiLink::Ble); }},
      {"volume card", [&] { uiRender(lcd, p, uiVolumeIndex(p), true, 2000, bv, UiLink::Ble, &vv); }},
      {"display panel", [&] { uiDeviceEditor(lcd, deviceSettings()); }},
      {"nav turn", [&] { uiNav(lcd, navTurn, false, bv); }},
      {"nav busy", [&] { uiNav(lcd, navBusy, false, bv); }},
  };
  // "redraw" is the same screen a second later (only what changed is sent);
  // "whole" is the frame after uiInvalidate(), as after a wake.
  fprintf(g_out, "%-14s %9s %8s %8s %9s %9s %9s %9s\n", "screen", "host us", "@240 ms", "@80 ms", "px redraw",
          "SPI ms", "px whole", "SPI ms");
  for (const Case &c : cases) {
    medianUs(30, c.draw, nullptr);  // settle: first sweep, font caches
    uint64_t redraw = 0, whole = 0;
    const double us = medianUs(301, c.draw, &redraw);
    medianUs(1, [&] { uiInvalidate(); c.draw(); }, &whole);
    fprintf(g_out, "%-14s %9.1f %8.2f %8.2f %9llu %9.2f %9llu %9.2f\n", c.name, us, us * g_slowdown / 1000.0,
            us * g_slowdown * 3.0 / 1000.0, (unsigned long long)redraw, redraw * SPI_US_PER_PX / 1000.0,
            (unsigned long long)whole, whole * SPI_US_PER_PX / 1000.0);
  }
}

}  // namespace

int main(int argc, char **argv) {
  const std::string name = argc > 1 ? argv[1] : "desk";
  if (argc > 2) g_slowdown = atof(argv[2]);
  // setup() and loop() print their [tag] lines through Serial, which writes to
  // stdout; the table goes to a copy of the original stdout instead.
  g_out = fdopen(dup(1), "w");
  if (!g_out || !freopen("/dev/null", "w", stdout)) return 1;
  setvbuf(g_out, nullptr, _IOLBF, 0);
  if (name == "header") {
    fprintf(g_out, "%-9s %7s %9s %7s %7s %6s %6s %7s %7s %6s\n", "scenario", "fr/min", "Mpx/min", "ms/fr", "busy",
           "@240", "@80", "loop/s", "mA", "bl");
    return 0;
  }
  if (name == "render") {
    runRender();
    return 0;
  }
  S = makeScenario(name);
  if (!S.name) {
    fprintf(stderr, "unknown scenario %s\n", name.c_str());
    return 2;
  }
  // The battery budget: the critters' frames against the same card drawn still.
  double stillMa = 0;
  if (name == "agents-battery") {
    stillMa = runChild("agents-battery-still");
    if (stillMa <= 0) {
      fprintf(stderr, "%s: the still baseline failed\n", name.c_str());
      return 1;
    }
  }
  const double ma = runScenario();
  if (stillMa > 0) {
    fprintf(g_out, "          (animating / still = %.2f / %.2f mA = %.3fx, budget %.1fx)\n", ma, stillMa, ma / stillMa,
            ANIM_BUDGET);
    if (ma > ANIM_BUDGET * stillMa) {
      fprintf(stderr, "%s: %.2f mA animating is over %.1fx the %.2f mA of the card drawn still\n", S.name, ma,
              ANIM_BUDGET, stillMa);
      return 1;
    }
  }
  if (g_stale || !g_checked) {
    fprintf(stderr, "%s: %llu of %llu frames left the panel different from the sprite\n", S.name,
            (unsigned long long)g_stale, (unsigned long long)g_checked);
    return 1;
  }
  if (g_unpublished) {
    fprintf(stderr, "%s: a Settings read over BLE returned stale settings after %llu passes\n", S.name,
            (unsigned long long)g_unpublished);
    return 1;
  }
  if (S.nav && g_endCard != 0) {
    fprintf(stderr, "%s: the ride moved the deck to card %u (auto-advance must freeze during navigation)\n", S.name,
            (unsigned)g_endCard);
    return 1;
  }
  // A ride: frames keep coming (a busy frame every 200 ms is 300/min), the
  // screen stays on, and WiFi stays off (one radio, shared with the phone's link).
  const double framesPerMin = g_frames / (S.seconds / 60.0);
  if (S.nav && framesPerMin < 240) {
    fprintf(stderr, "%s: %.0f frames/min during the ride, expected at least 240\n", S.name, framesPerMin);
    return 1;
  }
  if (S.nav && g_backlight == 0) {
    fprintf(stderr, "%s: the screen was off at the end of the ride\n", S.name);
    return 1;
  }
  if (S.nav && (g_wifiRideStarts || g_wifiRidePasses)) {
    fprintf(stderr, "%s: WiFi came up %u times during the ride (%llu passes with it on)\n", S.name,
            (unsigned)g_wifiRideStarts, (unsigned long long)g_wifiRidePasses);
    return 1;
  }
  // The alert: the screen woke for the prompt, it beeped once (the Mac resends
  // the same prompt every 5 s), the prompt is what is drawn, and of the two taps
  // on its first button only the one made after it settled again (once the
  // passkey screen stopped covering it) sent an answer.
  if (S.agents == Scenario::AGENTS_ALERT &&
      (g_backlight == 0 || g_attention != 1 || !benchPromptOpen() || g_answers != 1 ||
       g_answerAt < S.pairToMs + PROMPT_SETTLE_MS)) {
    fprintf(stderr, "%s: at the end the screen was %s, %u attention beeps (expected 1), prompt drawn=%d, %u answers (expected 1)\n",
            S.name, g_backlight ? "on" : "off", (unsigned)g_attention, (int)benchPromptOpen(), (unsigned)g_answers);
    fprintf(stderr, "%s: the answer went out %u ms into the run; the passkey screen left at %u ms\n", S.name,
            (unsigned)g_answerAt, (unsigned)S.pairToMs);
    return 1;
  }
  // The handover: once BLE is live its payload stays (WiFi results are drained),
  // the prompt it carries is drawn, and the tile's wait (seen first over WiFi)
  // and its prompt (then over BLE) beep once between them.
  if (S.agents == Scenario::AGENTS_HANDOVER &&
      (!g_wifiOkWhileLive || g_wifiWonPasses || g_attention != 1 || !benchPromptOpen())) {
    fprintf(stderr, "%s: %u WiFi payloads while BLE was live, %llu passes showed WiFi's payload, %u beeps (expected 1), prompt drawn=%d\n",
            S.name, (unsigned)g_wifiOkWhileLive, (unsigned long long)g_wifiWonPasses, (unsigned)g_attention,
            (int)benchPromptOpen());
    return 1;
  }
  if (S.agents == Scenario::AGENTS_RUNNING && g_attention != 0) {
    fprintf(stderr, "%s: %u attention beeps with no agent waiting\n", S.name, (unsigned)g_attention);
    return 1;
  }
  // The detail: one 0A as the tap lifts, again 5 s later while no `d` came,
  // so four before back; back's 0A 00 fails (the stand-in) and goes out on
  // its retry a second later; the reopen sends 0A at once.
  // The picker: a shape keeps the tile's colour (h 17 = shape 2, colour 1),
  // a colour keeps the picked shape, both for the open agent; its back leaves
  // the detail open. Then each way out sends one 0A 00: the tile waiting (and
  // the alert's prompt then opens as before: one beep), the tile gone, and the
  // screen's sleep with the picker up, after which nothing is open.
  if (!strcmp(S.name, "detail")) {
    uint32_t before = 0, reopenAt = 0;
    const uint32_t n = g_detailOpens < DETAIL_LOG ? g_detailOpens : DETAIL_LOG;
    for (uint32_t i = 0; i < n; i++) {
      if (g_detailAt[i] < DETAIL_CLOSE_MS) before++;
      else if (!reopenAt) reopenAt = g_detailAt[i];
    }
    // The first 0A at or after `from`, or 0.
    auto openAfter = [&](uint32_t from) -> uint32_t {
      for (uint32_t i = 0; i < n; i++)
        if (g_detailAt[i] >= from) return g_detailAt[i];
      return 0;
    };
    const uint32_t *c = g_detailCloseAt;
    const uint32_t sleepAt = DETAIL_PICKER2_MS + 2u * 60000u;
    const bool firstOk = n >= 2 && g_detailAt[0] >= DETAIL_OPEN_MS && g_detailAt[0] < DETAIL_OPEN_MS + 300;
    const bool resendOk = firstOk && g_detailAt[1] - g_detailAt[0] >= DETAIL_RESEND_MS &&
                          g_detailAt[1] - g_detailAt[0] < DETAIL_RESEND_MS + 100;
    const bool reopenOk = reopenAt >= DETAIL_REOPEN_MS && reopenAt < DETAIL_REOPEN_MS + 300;
    const bool failOk = g_detailCloseFails == 1 && g_detailCloseFailAt >= DETAIL_CLOSE_MS &&
                        g_detailCloseFailAt < DETAIL_CLOSE_MS + 300;
    // (c[0] + 50: the stand-in stamps mid-pass, the retry is timed from the pass's start.)
    const bool closesOk = g_detailCloses == 4 && failOk && c[0] + 50 >= g_detailCloseFailAt + DETAIL_CLOSE_RETRY_MS &&
                          c[0] < g_detailCloseFailAt + DETAIL_CLOSE_RETRY_MS + 100 &&
                          c[1] >= DETAIL_WAIT_MS && c[1] < DETAIL_WAIT_END_MS &&  // the payload that waits
                          c[2] >= DETAIL_GONE_MS && c[2] < DETAIL_GONE_END_MS &&  // the payload without it
                          c[3] >= sleepAt && c[3] < sleepAt + 2000;               // the screen's sleep
    // Opened again only by the taps, each at once.
    const uint32_t o2 = openAfter(c[1]), o3 = openAfter(c[2]);
    const bool opensOk = o2 >= DETAIL_OPEN2_MS && o2 < DETAIL_OPEN2_MS + 300 && o3 >= DETAIL_OPEN3_MS &&
                         o3 < DETAIL_OPEN3_MS + 300 && openAfter(c[3]) == 0;
    const bool hatsOk = g_hats == 2 && g_hatSent[0] == 4 * 8 + 1 && g_hatSent[1] == 4 * 8 + 2 &&
                        !strcmp(g_hatAgent[0], "l00002") && !strcmp(g_hatAgent[1], "l00002");
    if (!firstOk || !resendOk || before != 4 || !reopenOk || !closesOk || !opensOk || !hatsOk ||
        g_attention != 1 || benchDetailOpen()) {
      fprintf(stderr, "%s: 0A sends at", S.name);
      for (uint32_t i = 0; i < n; i++) fprintf(stderr, " %u", (unsigned)g_detailAt[i]);
      fprintf(stderr, " ms (%u before back, expected 4); 0A 00 sends at", (unsigned)before);
      for (uint32_t i = 0; i < g_detailCloses && i < (uint32_t)DETAIL_LOG; i++) fprintf(stderr, " %u", (unsigned)c[i]);
      fprintf(stderr, " ms (expected 4: back's retry, wait, gone, sleep at %u); %u failed, at %u ms (expected 1, at back)\n",
              (unsigned)sleepAt, (unsigned)g_detailCloseFails, (unsigned)g_detailCloseFailAt);
      fprintf(stderr, "%s: %u 0B sends (expected 2: 33 then 34)", S.name, (unsigned)g_hats);
      for (uint32_t i = 0; i < g_hats && i < 2; i++) fprintf(stderr, " %s:%u", g_hatAgent[i], (unsigned)g_hatSent[i]);
      fprintf(stderr, "; %u beeps (expected 1); open at the end=%d\n", (unsigned)g_attention, (int)benchDetailOpen());
      return 1;
    }
  }
  // A ride starting over the detail closes it: one 0A 00 as the first frame
  // lands, nothing open afterwards, and no 0A once the ride owns the screen.
  if (!strcmp(S.name, "detail-ride")) {
    const uint32_t n = g_detailOpens < DETAIL_LOG ? g_detailOpens : DETAIL_LOG;
    uint32_t after = 0;
    for (uint32_t i = 0; i < n; i++)
      if (g_detailAt[i] >= DETAIL_RIDE_MS) after++;
    const bool ok = n >= 1 && g_detailAt[0] >= DETAIL_OPEN_MS && g_detailAt[0] < DETAIL_OPEN_MS + 300 &&
                    g_detailCloses == 1 && g_detailCloseAt[0] >= DETAIL_RIDE_MS &&
                    g_detailCloseAt[0] < DETAIL_RIDE_MS + 300 && after == 0 && !benchDetailOpen() && benchNavActive();
    if (!ok) {
      fprintf(stderr, "%s: %u 0A sends (%u after the ride started, expected 0), %u 0A 00 (expected 1, at %u ms: %u), "
              "open at the end=%d, ride=%d\n", S.name, (unsigned)g_detailOpens, (unsigned)after,
              (unsigned)g_detailCloses, (unsigned)DETAIL_RIDE_MS, (unsigned)(g_detailCloses ? g_detailCloseAt[0] : 0),
              (int)benchDetailOpen(), (int)benchNavActive());
      return 1;
    }
  }
  if (S.mustSleep && g_backlight != 0) {
    fprintf(stderr, "%s: the screen was still on after %u s with nobody touching it\n", S.name, S.seconds);
    return 1;
  }
  if (!strcmp(S.name, "volume") && (g_volRequests < 2 || g_volPlain < 1 || !g_volLastMuted)) {
    fprintf(stderr, "%s: %u volume requests reached the Mac, expected a drag level then a final mute (%u unmuted, last muted=%d)\n", S.name,
            (unsigned)g_volRequests, (unsigned)g_volPlain, (int)g_volLastMuted);
    return 1;
  }
  if (!strcmp(S.name, "volume") && (g_volStaleLate || !g_volEchoed || g_volLastEchoLevel != g_volLastReqLevel)) {
    fprintf(stderr, "%s: the Mac's echoes ended at level %u (%u stale ones landed late), the last request was %u\n", S.name,
            (unsigned)g_volLastEchoLevel, (unsigned)g_volStaleLate, (unsigned)g_volLastReqLevel);
    return 1;
  }
  return 0;
}
