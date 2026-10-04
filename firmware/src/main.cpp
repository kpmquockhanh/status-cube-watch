// Claude status cube -- Waveshare ESP32-S3-Touch-LCD-1.69
//
// Polls the host bridge for a small pre-formatted JSON payload and renders it
// as a swipeable deck of cards. Swipe left/right to change card; the deck can
// also auto-advance (the rotate setting).

#include <Arduino.h>
#include "battery.h"
#include "ble.h"
#include "config.h"
#include "deck_util.h"
#include "display.h"
#include "gesture.h"
#include "idle_sleep.h"
#include "imu.h"
#include "net.h"
#include "orientation.h"
#include "ota.h"
#include "payload.h"
#include "dev_editor.h"
#include "pomo_editor.h"
#include "pomo_settings.h"
#include "pomodoro.h"
#include "portal.h"
#include "settings.h"
#include "settings_json.h"
#include "touch.h"
#include "touch_map.h"
#include "transport_policy.h"
#include "ui.h"

// Divides every Pomodoro duration. Leave at 1. `make run POMO_FAST=60` in
// sim/ turns minutes into seconds, so a whole cycle and the phase-end alert
// can be watched in about two minutes.
#ifndef POMO_TIME_DIV
#define POMO_TIME_DIV 1
#endif

// Keep WiFi up while BLE is live (OTA needs it). A config.h from before this
// existed still builds.
#ifndef WIFI_ALWAYS_ON
#define WIFI_ALWAYS_ON 0
#endif

namespace {

constexpr uint32_t CPU_MHZ_AWAKE = 240;  // matches board_build.f_cpu
constexpr uint32_t CPU_MHZ_ASLEEP = 80;  // lowest clock WiFi/BLE still run at

Display lcd;
Touch touch;
Payload payload{};
// Built from the compile-time defaults only so the object exists; setup()
// applies the stored settings once NVS is read.
Pomodoro pomo(pomoConfigFrom(pomoDefaults(), POMO_TIME_DIV));
uint32_t lastPomoSec = 0xFFFFFFFFu;  // the clock value last drawn, to redraw when it changes
uint8_t lastBatPct = 255;  // battery percent last drawn, to redraw when it changes

uint8_t cardIndex = 0;
uint32_t lastPoll = 0;
uint32_t lastRotate = 0;
uint32_t lastGood = 0;  // millis() of the last successful fetch
uint32_t lastDraw = 0;
bool dirty = true;

TransportPolicy policy;
bool wifiUp = false;           // netStart() called and not yet netStop()
bool otaUp = false;            // otaBegin() called and not yet otaEnd()
IdleSleep idle;
bool screenOn = true;
bool swallowTouch = false;  // the touch that woke the screen is ignored until the finger lifts
bool pairWaitDismissed = false;  // a touch dismisses the first-boot "waiting for a Mac" screen

// Auto-rotate (orientation.h). `orient` is what the IMU says; `appliedRot` is what
// is on the panel: they differ only while the screen sleeps, so the panel is
// never written to while it is off. Touch is remapped by `appliedRot`.
Orientation orient;
uint8_t appliedRot = 0;
bool imuUp = false;
bool touchDown = false;  // a finger was on the glass at the last poll: do not flip under it
uint32_t lastImu = 0;
constexpr uint32_t IMU_POLL_MS = 200;

// First boot with neither a Mac nor a network and nothing received yet.
bool waitingToPair() { return pairWaitAtBoot(bleBonded(), settingsHaveWifi()) && !payload.valid; }

// Gestures come from raw coordinates (see gesture.h), which keeps this working
// across the CST816S/T/D variants, whose gesture registers disagree.
GestureTracker gestures;

// The Pomodoro settings editor (swipe up on the idle Pomodoro card). While it
// is open nothing else moves the deck, and multi-tap is off so a double tap
// cannot start a session behind it. `editSettings` is the working copy.
bool editing = false;
bool editingDevice = false;  // the display panel, not the Pomodoro editor, is the open one
DeviceSettings editDevice{};
PomoSettings editSettings{};
// The Mac changed the Pomodoro lengths while a phase ran: they reach the timer
// at the next point where setConfig takes them (pomodoro.h).
bool pomoConfigPending = false;

constexpr uint32_t SETUP_HOLD_MS = 5000;
constexpr uint32_t SETUP_WINDOW_MS = 3000;

// Touching the screen just after the cube powers up forces the setup portal,
// so a working cube can be pointed at a new network without a reflash. The
// touch must then be held for SETUP_HOLD_MS.
bool setupHoldRequested() {
  int16_t x, y;
  // touch.begin() resets the controller, which then ignores a finger that was
  // already down, so a touch is looked for for a short window after the reset
  // rather than only at the instant of boot. It costs a normal boot 3 s, which the hint on screen explains.
  const uint32_t w0 = millis();
  bool touched = false;
  uiMessage(lcd, "STARTING", "HOLD SCREEN FOR SETUP");
  while (millis() - w0 < SETUP_WINDOW_MS && !touched) {
    touched = touch.read(x, y);
    if (!touched) delay(20);
  }
  Serial.printf("[boot] setup-touch window: touched=%d\n", (int)touched);
  if (!touched) return false;
  Serial.println("[boot] touch at boot: keep holding for WiFi setup");
  uiMessage(lcd, "KEEP HOLDING", "FOR SETUP");
  const uint32_t t0 = millis();
  uint8_t misses = 0;
  while (millis() - t0 < SETUP_HOLD_MS) {
    misses = touch.read(x, y) ? 0 : misses + 1;
    if (misses > 5) return false;  // a few dropped reads are not a release
    delay(20);
  }
  return true;
}

void step(int delta) {
  // Also when there is nowhere to go: otherwise the timer stays expired and the
  // first payload to arrive is swiped away from the Pomodoro card at once.
  lastRotate = millis();
  const uint8_t n = uiDeckSize(payload);
  if (n <= 1) return;
  cardIndex = (cardIndex + n + delta) % n;
  if (cardIndex == uiPomodoroIndex(payload)) uiReplayPomodoro();
  else uiReplay(cardIndex);
  dirty = true;
}

// The Mac edited settings over BLE: apply, tell it how it went, and reboot when
// the network settings changed (they are only read at boot).
void handleBleSettings() {
  char json[SETTINGS_JSON_MAX + 1];
  if (!bleTakeSettings(json, sizeof(json))) return;
  const SettingsResult r = settingsApplyJson(json);
  Serial.printf("[ble] settings write -> %d\n", (int)r);
  bleSettingsReply((uint8_t)r);
  if (r == SettingsResult::Invalid) return;
  pomoConfigPending = !pomo.setConfig(pomoConfigFrom(pomoSettings(), POMO_TIME_DIV));
  // The Mac's write wins over an open editor: it shows what is now stored,
  // rather than saving its older copy over it on DONE.
  if (editing) {
    editDevice = deviceSettings();
    editSettings = pomoSettings();
  }
  if (screenOn) lcd.setBrightness(deviceSettings().backlight);
  lastRotate = millis();
  dirty = true;
  if (r == SettingsResult::OkReboot) {
    delay(500);  // let the notification leave
    ESP.restart();
  }
}

void openEditor() {
  editSettings = pomoSettings();
  editing = true;
  uiEditorSlide(true, editSettings);
  dirty = true;
}

void openDeviceEditor() {
  editDevice = deviceSettings();
  editing = true;
  editingDevice = true;
  uiDeviceSlide(true, editDevice);
  dirty = true;
}

// DONE and swipe-up both land here. Brightness was previewed live while
// editing; the rest takes effect now.
void closeDeviceEditor() {
  if (editDevice != deviceSettings()) deviceSettingsSave(editDevice);  // spare the flash
  lcd.setBrightness(deviceSettings().backlight);
  editing = false;
  editingDevice = false;
  uiDeviceSlide(false, editDevice);
  lastRotate = millis();
  idle.data(millis());  // the sleep timer starts over from the edit
  dirty = true;
}

void deviceEditorGesture(Gesture g) {
  if (g == Gesture::SwipeUp) {
    closeDeviceEditor();
  } else if (g == Gesture::Tap) {
    const EditHit hit = devEditorHit(gestures.startX(), gestures.startY());
    if (hit.action == EditAction::Done) {
      closeDeviceEditor();
    } else if (hit.action != EditAction::None) {
      devEditorApply(editDevice, hit, deviceDefaults(), deviceSettings());
      lcd.setBrightness(editDevice.backlight);
      dirty = true;
    }
  }
}

// DONE and swipe-down both land here: what is on screen is what is saved.
void closeEditor() {
  if (editSettings != pomoSettings()) pomoSettingsSave(editSettings);  // spare the flash
  pomo.setConfig(pomoConfigFrom(pomoSettings(), POMO_TIME_DIV));
  editing = false;
  uiEditorSlide(false, editSettings);
  uiReplayPomodoro();
  lastRotate = millis();
  dirty = true;
}

void editorGesture(Gesture g) {
  if (g == Gesture::SwipeDown) {
    closeEditor();
  } else if (g == Gesture::Tap) {
    // Hit-test where the finger went down, not where it came up.
    const EditHit hit = pomoEditorHit(gestures.startX(), gestures.startY());
    if (hit.action == EditAction::Done) {
      closeEditor();
    } else if (hit.action != EditAction::None) {
      pomoEditorApply(editSettings, hit, pomoDefaults());
      dirty = true;
    }
  }
}

void pollTouch() {
  int16_t x = 0, y = 0;
  const bool down = touch.read(x, y);
  touchDown = down;
  if (down) touchToScreen(appliedRot, x, y);
  if (down && !pairWaitDismissed && waitingToPair()) {
    pairWaitDismissed = true;
    dirty = true;
  }
  const uint32_t now = millis();
  if (down && idle.touch(now)) swallowTouch = true;
  if (swallowTouch) {
    if (!down) swallowTouch = false;
    return;
  }
  // Multi-taps only mean something on the Pomodoro card. Everywhere else a tap
  // is a plain Tap, delivered at once (the editors hit-test it).
  const bool onPomodoro = cardIndex == uiPomodoroIndex(payload);
  const Gesture gesture = gestures.update(down, x, y, now, onPomodoro && !editing);
  // While the panel slides in its buttons are not where the hit-test puts
  // them, so nothing is pressed until it has landed.
  if (editing && uiEditorSliding()) return;
  if (editing) {
    if (editingDevice) deviceEditorGesture(gesture);
    else editorGesture(gesture);
    return;
  }
  switch (gesture) {
    case Gesture::SwipeNext:  // swipe left advances (a tap does nothing)
      step(1);
      break;
    case Gesture::SwipePrev:
      step(-1);
      break;
    case Gesture::DoubleTap:  // start / pause / resume / next phase
      pomo.longPress(now);
      dirty = true;
      break;
    case Gesture::TripleTap:  // start over
      pomo.reset();
      dirty = true;
      break;
    case Gesture::SwipeUp:  // open the Pomodoro editor (idle timer only)
      if (editorMayOpen(onPomodoro, pomo.view().state)) openEditor();
      break;
    case Gesture::SwipeDown:  // open the display settings panel, from any card
      if (devEditorMayOpen(editing)) openDeviceEditor();
      break;
    default:
      break;
  }
}

void pollOrientation(uint32_t now) {
  if (!imuUp || now - lastImu < IMU_POLL_MS) return;
  lastImu = now;
  float ax, ay, az;
  if (!imuReadAccel(ax, ay, az)) return;  // a missed read keeps the current orientation
  orient.update(now, (float)IMU_UP_SIGN * ax, az, touchDown);
}

// Puts the classifier's answer on the panel. Not while the screen is asleep; the
// wake path calls this on the same loop pass, before the first frame is drawn.
void applyRotation() {
  if (!screenOn || appliedRot == orient.rotation()) return;
  appliedRot = orient.rotation();
  lcd.setRotation(appliedRot);
  dirty = true;
  Serial.printf("[imu] rotation -> %d\n", (int)appliedRot);
}

uint32_t pollMs() { return (uint32_t)deviceSettings().pollSec * 1000u; }
uint32_t rotateMs() { return (uint32_t)deviceSettings().rotateSec * 1000u; }
uint32_t sleepMs() { return (uint32_t)deviceSettings().sleepMin * 60000u; }

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] claude-status-cube");
  settingsLoad();
  pomo.setConfig(pomoConfigFrom(pomoSettings(), POMO_TIME_DIV));

  lcd.init();
  lcd.setRotation(0);
  lcd.setBrightness(deviceSettings().backlight);
  uiBegin(lcd);

  touch.begin();
  imuUp = imuBegin();  // after touch.begin(), which starts the shared I2C bus
  batteryBegin();

  // BLE first: the stack must be up before bleBonded() means anything. The
  // setup portal is asked for by holding the screen; WiFi is joined at boot
  // only when there is no Mac to wait for (the old behaviour, and the old
  // fallback to the portal). With a bond, WiFi comes up later and only if BLE
  // goes quiet (see transport_policy.h).
  bleBegin();
  if (setupHoldRequested()) portalRun(lcd, false);

  if (!bleBonded()) {
    if (settingsHaveWifi()) {
      uiMessage(lcd, "CONNECTING", settings().ssid);
      const bool joined = netBegin();
      wifiUp = true;
      if (!joined && portalOnJoinFail(false)) portalRun(lcd, true);
      if (joined) {
        otaBegin(lcd);
        otaUp = true;
      }
    }
  } else if (!settingsHaveWifi()) {
    uiMessage(lcd, "NO MAC", "WAITING FOR LINK");
  }

  idle.begin(millis());
  policy.begin(millis());
  // First poll on the first loop pass rather than a full interval from now.
  lastPoll = millis() - pollMs();
  lastRotate = millis();
  dirty = true;
}

void loop() {
  if (otaUp) otaHandle();
  const uint32_t now = millis();

  pomo.tick(now);
  if (pomo.takeAlert()) {
    const PomoView ended = pomo.view();  // DONE: phase = the one that just ended
    bleNotifyPomodoro(ended.phase, ended.next);
    // Phase over: pull the deck to the Pomodoro card wherever you were. A swipe
    // leaves it again; the DONE state waits for a double tap.
    cardIndex = uiPomodoroIndex(payload);
    lastRotate = now;
    uiReplayPomodoro();
    uiAlertStart();
    dirty = true;
  }
  if (pomoConfigPending && pomo.setConfig(pomoConfigFrom(pomoSettings(), POMO_TIME_DIV))) {
    pomoConfigPending = false;
    dirty = true;
  }
  pollTouch();
  pollOrientation(now);
  batteryUpdate(now);

  // Which transport feeds the cube (transport_policy.h): BLE while the Mac is
  // delivering, WiFi when it is not.
  const TransportDecision td =
      policy.update(now, bleLastGood(), bleBonded(), settingsHaveWifi(), WIFI_ALWAYS_ON != 0);
  if (td.wifiOn && !wifiUp) {
    netStart();
    wifiUp = true;
  } else if (!td.wifiOn && wifiUp) {
    if (otaUp) {
      otaEnd();
      otaUp = false;
    }
    netStop();
    wifiUp = false;
  }
  if (wifiUp && !otaUp && netOnline()) {
    otaBegin(lcd);
    otaUp = true;
  }

  bool gotData = false;
  const bool wasOnPomodoro = cardIndex == uiPomodoroIndex(payload);
  if (bleTake(payload)) gotData = true;
  handleBleSettings();
  if (wifiUp && now - lastPoll >= pollMs()) {
    lastPoll = now;
    if (netFetch(payload)) {
      gotData = true;
    } else {
      Serial.printf("[net] fetch failed: %s\n", netLastError());
    }
    dirty = true;
  }
  if (gotData) {
    lastGood = now;
    idle.data(now);
    // The deck may have changed size: keep the Pomodoro card under the user.
    cardIndex = deckKeepIndex(wasOnPomodoro, cardIndex, uiDeckSize(payload));
    dirty = true;
  }

  if (rotateMs() > 0 && !editing && now - lastRotate >= rotateMs()) {
    step(1);
  }

  // The Pomodoro clock counts whole seconds, so redraw the moment its number
  // changes rather than leaving it to the 1 s housekeeping tick below, which
  // would beat against it and skip or repeat digits.
  const PomoView pv = pomo.view();
  if (cardIndex == uiPomodoroIndex(payload) && pv.displaySec != lastPomoSec) {
    lastPomoSec = pv.displaySec;
    dirty = true;
  }

  const BatteryView bv = batteryView();
  if (bv.pct != lastBatPct) {
    lastBatPct = bv.pct;
    dirty = true;
  }

  // Screen sleep: never while a session is running, the editor or a pairing
  // code is up (see idle_sleep.h).
  const PomoState ps = pv.state;
  const bool keepOn = ps == POMO_FOCUS || ps == POMO_BREAK || ps == POMO_PAUSED || editing ||
                      bleState() == BleState::Pairing || (!pairWaitDismissed && waitingToPair());
  const bool wantOn = idle.update(now, keepOn, sleepMs());
  if (wantOn != screenOn) {
    screenOn = wantOn;
    if (screenOn) {
      setCpuFrequencyMhz(CPU_MHZ_AWAKE);
      lcd.wakeup();
      lcd.setBrightness(deviceSettings().backlight);
      dirty = true;
    } else {
      lcd.setBrightness(0);
      lcd.sleep();
      setCpuFrequencyMhz(CPU_MHZ_ASLEEP);
    }
  }
  if (!screenOn) {
    delay(50);  // touch is still polled, so a tap wakes it at once
    return;
  }
  applyRotation();

  // Redraw on change, every frame while a ring is still moving, and once a
  // second otherwise so the freshness counter ticks.
  if (dirty || uiAnimating() || now - lastDraw >= 1000) {
    const uint32_t age = lastGood ? now - lastGood : now;
    const bool pairing = bleState() == BleState::Pairing;
    const bool waiting = !pairWaitDismissed && waitingToPair();
    const bool editorUp = editing && !uiEditorSliding();
    // Only the deck pulses the backlight for an alert; another screen ends it
    // and restores the level (the display panel's preview while it is open).
    if ((pairing || waiting || editorUp) && uiAlertCancel())
      lcd.setBrightness(editingDevice ? editDevice.backlight : deviceSettings().backlight);
    if (pairing) uiBlePair(lcd, blePasskey());  // the code must be seen
    else if (waiting) uiBlePair(lcd, 0);
    else if (editorUp) {
      if (editingDevice) uiDeviceEditor(lcd, editDevice);
      else uiPomodoroEditor(lcd, editSettings);
    }
    else uiRender(lcd, payload, cardIndex, td.bleLive || netOnline(), age, pv, bv,
                    td.bleLive ? UiLink::Ble : (wifiUp && netOnline() ? UiLink::Wifi : UiLink::None));
    lastDraw = now;
    dirty = false;
  }

  // Touch sampling cadence; keeps the loop off a busy spin. Skipped while a
  // ring is moving, where the frame time already paces the loop.
  if (!uiAnimating()) delay(15);
}
