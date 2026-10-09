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
//   ./build/cube-bench <desk|linked|pomodoro|browse|editor|sleep|volume|render> [slowdown]

#include <ArduinoJson.h>

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
#include "../src/display.h"
#include "../src/imu.h"
#include "../src/net.h"
#include "../src/payload.h"
#include "../src/pomo_editor.h"
#include "../src/pomo_settings.h"
#include "../src/pomodoro.h"
#include "../src/settings.h"
#include "../src/touch.h"
#include "../src/ui.h"
#include "battery_env.h"

void setup();
void loop();

uint64_t g_simUs = 100000;  // the board has been up a moment when setup() runs
Display *g_simDisplay = nullptr;

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
  // Screen sleep, set by the scenario so the numbers do not depend on whose
  // config.h the bench is built with. `mustSleep`: the run fails unless the
  // screen is off at the end.
  uint8_t sleepMin;
  bool mustSleep;
  std::vector<Stroke> strokes;
};

Scenario S;
uint32_t g_start = 0;  // millis() when the scenario's clock starts (after setup)
bool g_running = false;

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
  } else if (name == "pomodoro") {
    s.what = "Pomodoro focus running, Mac pushing every 5 s";
    s.seconds = 600;
    s.strokes.push_back(swipe(1000, 190, 150, 50, 150));  // to the Pomodoro card
    s.strokes.push_back(tapAt(3000, 120, 140));           // double tap: start
    s.strokes.push_back(tapAt(3200, 120, 140));
  } else if (name == "browse") {
    s.what = "a swipe every 4 s (ring sweep on each card)";
    s.seconds = 120;
    for (uint32_t t = 1000; t < 120000; t += 4000) s.strokes.push_back(swipe(t, 190, 150, 50, 150));
  } else if (name == "editor") {
    s.what = "display panel: open, 8 steps, a toggle, close; then the Pomodoro editor: a step, DONE";
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
    s.strokes.push_back(swipe(t, 120, 220, 120, 60));  // still on the Pomodoro card: its editor
    t += 1000;
    s.strokes.push_back(tapZone(t, editorPlus(0)));
    t += 600;
    s.strokes.push_back(tapZone(t, editorDoneBtn()));
  } else if (name == "volume") {
    s.what = "Volume card: a drag up the card, then a tap on the number (the Mac echoes each)";
    s.seconds = 6;
    s.strokes.push_back(swipe(1000, 50, 150, 190, 150));       // swipe right: Pomodoro -> Volume
    s.strokes.push_back(Stroke{2000, 600, 120, 180, 120, 60});  // drag up, above the button row
    s.strokes.push_back(tapAt(3500, VOL_MUTE_CX, VOL_MUTE_CY));  // the number: mute
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

// The usage card from fixtures/usage.json, with the countdown running down a
// minute at a time and the 5h percentage creeping up, as the bridge sends it.
std::string payloadJson(uint32_t nowMs) {
  static std::string cache;
  static int cachedMin = -1;
  const int min = (int)(nowMs / 60000);
  if (min == cachedMin) return cache;
  cachedMin = min;
  const int left = 249 - min > 0 ? 249 - min : 0;
  const int g = 14 + min / 3;
  char reset[16];
  snprintf(reset, sizeof(reset), "%dh %02dm", left / 60, left % 60);
  char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"v\":1,\"ts\":0,\"src\":\"oauth\",\"cards\":[{\"t\":\"CLAUDE\",\"v\":\"%s\","
           "\"s1\":\"to reset\",\"s2\":\"\",\"c\":\"green\",\"g\":%d,\"c2\":\"green\",\"g2\":53,"
           "\"rows\":[{\"k\":\"5H\",\"p\":\"%d%%\",\"r\":\"%s\"},{\"k\":\"7D\",\"p\":\"53%%\",\"r\":\"1d 1h\"}],"
           "\"m\":\"12\",\"mc\":\"amber\"}]}",
           reset, g, g, reset);
  cache = buf;
  return cache;
}

bool parsePayload(const std::string &json, Payload &out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  Payload p{};
  char err[64];
  if (!payloadFromJson(doc, p, err, sizeof(err))) return false;
  out = p;
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
PomoSettings g_pubPomo{};
uint64_t g_unpublished = 0;  // loop passes that ended with a read returning stale settings
void publishSettings() {
  g_pubDevice = deviceSettings();
  g_pubPomo = pomoSettings();
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
BleState bleState() { return S.bleLive ? BleState::Connected : BleState::Advertising; }
uint32_t blePasskey() { return 0; }
bool bleBonded() { return S.bonded; }

// The Mac's PushPolicy sends every 5 s whether or not anything changed.
bool bleTake(Payload &out) {
  if (!S.bleLive || !g_running) return false;
  const uint32_t now = millis();
  if (g_bleLastPush && now - g_bleLastPush < 5000) return false;
  g_bleLastPush = now ? now : 1;
  if (!parsePayload(payloadJson(now), out)) return false;
  g_bleLastGood = g_bleLastPush;
  return true;
}

uint32_t bleLastGood() { return g_bleLastGood; }
void bleForgetBonds() {}
bool bleTakeSettings(char *, size_t) { return false; }
void bleSettingsReply(uint8_t) { publishSettings(); }
void bleSettingsChanged() { publishSettings(); }
void bleNotifyPomodoro(uint8_t, uint8_t) {}

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
bool netBegin() { return g_wifi = true; }
bool netOnline() { return g_wifi; }
void netStart() { g_wifi = true; }
void netJoinSaved() { g_wifi = true; }
void netStop() {
  g_wifi = false;
  g_fetching = false;
}
void netRequest() {
  if (g_fetching) return;
  g_fetching = true;
  g_fetchAt = millis();
}
NetFetch netTake(Payload &) {
  if (!g_fetching || millis() - g_fetchAt < FETCH_REFUSED_MS) return NetFetch::None;
  g_fetching = false;
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
void batteryUpdate(uint32_t now) {
  static uint32_t last = 0;
  if (last && now - last < 5000) return;
  last = now ? now : 1;
  busyFixed(16 * (25.0 + 200.0));  // 16 ADC reads, delayMicroseconds(200) between them
}
BatteryView batteryView() { return batteryViewFromEnv(); }

// --- stand-in: buzzer --------------------------------------------------------------
// Silent. A tone is LEDC hardware time, not CPU, so the model charges nothing for it.
bool buzzerBegin() { return true; }
void buzzerSetLevel(uint8_t) {}
void buzzerPlay(Sound) {}
void buzzerUpdate(uint32_t) {}

// --- running ---------------------------------------------------------------------
namespace {

void runScenario() {
  setup();  // loads the config.h defaults, so the scenario's timers go in after it
  DeviceSettings d = deviceSettings();
  d.sleepMin = S.sleepMin;
  d.rotateSec = 0;
  deviceSettingsSave(d);
  publishSettings();  // as if stored before boot
  resetCounters();
  g_start = millis();
  g_running = true;
  const uint64_t endUs = g_simUs + (uint64_t)S.seconds * 1000000ull;
  while (g_simUs < endUs) {
    g_panelNs = 0;
    g_passPx = 0;
    const auto t0 = Clock::now();
    loop();
    // Both editors save on the cube itself; by the end of that pass the Mac's
    // next Settings read must see what they saved.
    if (g_pubDevice != deviceSettings() || g_pubPomo != pomoSettings()) g_unpublished++;
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
  Payload p{};
  parsePayload(payloadJson(0), p);
  Pomodoro pomo(pomoConfigFrom(pomoSettings(), 1));
  pomo.longPress(millis());
  const BatteryView bv = batteryView();
  VolumeSlider volume;
  MacVolume mv{};
  mv.known = true;
  mv.level = 56;
  mv.canSet = mv.canMute = true;
  snprintf(mv.name, sizeof(mv.name), "MacBook Pro Speakers");
  volume.fromMac(mv, 0);
  const VolumeView vv = volume.view();

  struct Case {
    const char *name;
    std::function<void()> draw;
  };
  const Case cases[] = {
      {"usage card", [&] { uiRender(lcd, p, 0, true, 2000, pomo.view(), bv, UiLink::Ble); }},
      {"pomodoro card", [&] {
         pomo.tick(millis());
         uiRender(lcd, p, uiPomodoroIndex(p), true, 2000, pomo.view(), bv, UiLink::Ble);
       }},
      {"volume card", [&] { uiRender(lcd, p, uiVolumeIndex(p), true, 2000, pomo.view(), bv, UiLink::Ble, &vv); }},
      {"display panel", [&] { uiDeviceEditor(lcd, deviceSettings()); }},
      {"pomo editor", [&] { uiPomodoroEditor(lcd, pomoSettings()); }},
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
  runScenario();
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
