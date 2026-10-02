// Claude status cube -- Waveshare ESP32-S3-Touch-LCD-1.69
//
// Polls the host bridge for a small pre-formatted JSON payload and renders it
// as a swipeable deck of cards. Swipe or tap to change card; the deck can
// also auto-advance (CARD_ROTATE_MS).

#include <Arduino.h>
#include "battery.h"
#include "config.h"
#include "deck_util.h"
#include "display.h"
#include "gesture.h"
#include "net.h"
#include "ota.h"
#include "payload.h"
#include "pomo_editor.h"
#include "pomo_settings.h"
#include "pomodoro.h"
#include "portal.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"

// Divides every Pomodoro duration. Leave at 1. `make run POMO_FAST=60` in
// sim/ turns minutes into seconds, so a whole cycle and the phase-end alert
// can be watched in about two minutes.
#ifndef POMO_TIME_DIV
#define POMO_TIME_DIV 1
#endif

namespace {

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

// Gestures come from raw coordinates (see gesture.h), which keeps this working
// across the CST816S/T/D variants, whose gesture registers disagree.
GestureTracker gestures;

// The Pomodoro settings editor (swipe up on the idle Pomodoro card). While it
// is open nothing else moves the deck, and holds are off so a long press
// cannot start a session behind it. `editSettings` is the working copy.
bool editing = false;
PomoSettings editSettings{};

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
  uiMessage(lcd, "STARTING", "HOLD SCREEN FOR WIFI SETUP");
  while (millis() - w0 < SETUP_WINDOW_MS && !touched) {
    touched = touch.read(x, y);
    if (!touched) delay(20);
  }
  Serial.printf("[boot] setup-touch window: touched=%d\n", (int)touched);
  if (!touched) return false;
  Serial.println("[boot] touch at boot: keep holding for WiFi setup");
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

void step(int delta) {
  const uint8_t n = uiDeckSize(payload);
  if (n <= 1) return;
  cardIndex = (cardIndex + n + delta) % n;
  lastRotate = millis();
  if (cardIndex == uiPomodoroIndex(payload)) uiReplayPomodoro();
  else uiReplay(cardIndex);
  dirty = true;
}

void openEditor() {
  editSettings = pomoSettings();
  editing = true;
  uiEditorSlide(true, editSettings);
  dirty = true;
}

// DONE and swipe-down both land here: what is on screen is what is saved.
void closeEditor() {
  pomoSettingsSave(editSettings);
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
  const uint32_t now = millis();
  // Holds only mean something on the Pomodoro card. Everywhere else a long
  // touch is ignored when the finger lifts, as a slow press always was.
  const bool onPomodoro = cardIndex == uiPomodoroIndex(payload);
  const Gesture gesture = gestures.update(down, x, y, now, onPomodoro && !editing);
  if (editing) {
    editorGesture(gesture);
    return;
  }
  switch (gesture) {
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
    case Gesture::SwipeUp:  // open the Pomodoro editor (idle timer only)
      if (editorMayOpen(onPomodoro, pomo.view().state)) openEditor();
      break;
    default:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] claude-status-cube");
  settingsLoad();
  pomo.setConfig(pomoConfigFrom(pomoSettings(), POMO_TIME_DIV));

  lcd.init();
  lcd.setRotation(0);
  lcd.setBrightness(BACKLIGHT);
  uiBegin(lcd);

  touch.begin();
  batteryBegin();

  // The setup portal is asked for by holding the screen, needed when there is
  // nothing to join, and fallen back to when the stored network cannot be
  // reached. Only the last case retries by itself.
  if (setupHoldRequested() || !settingsHaveWifi()) portalRun(lcd, false);
  uiMessage(lcd, "CONNECTING", settings().ssid);
  if (!netBegin()) portalRun(lcd, true);
  otaBegin(lcd);

  if (netFetch(payload)) {
    lastGood = millis();
  } else {
    uiMessage(lcd, "NO BRIDGE", netLastError());
  }
  lastPoll = lastRotate = millis();
  dirty = true;
}

void loop() {
  otaHandle();
  const uint32_t now = millis();

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
  batteryUpdate(now);

  if (now - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = now;
    const bool wasOnPomodoro = cardIndex == uiPomodoroIndex(payload);
    if (netFetch(payload)) {
      lastGood = now;
      // The deck may have changed size: keep the Pomodoro card under the user.
      cardIndex = deckKeepIndex(wasOnPomodoro, cardIndex, uiDeckSize(payload));
    } else {
      Serial.printf("[net] fetch failed: %s\n", netLastError());
    }
    dirty = true;
  }

  if (CARD_ROTATE_MS > 0 && !editing && now - lastRotate >= CARD_ROTATE_MS) {
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

  // Redraw on change, every frame while a ring is still moving, and once a
  // second otherwise so the freshness counter ticks.
  if (dirty || uiAnimating() || now - lastDraw >= 1000) {
    const uint32_t age = lastGood ? now - lastGood : now;
    if (editing && !uiEditorSliding()) uiPomodoroEditor(lcd, editSettings);
    else uiRender(lcd, payload, cardIndex, netOnline(), age, pv, bv);
    lastDraw = now;
    dirty = false;
  }

  // Touch sampling cadence; keeps the loop off a busy spin. Skipped while a
  // ring is moving, where the frame time already paces the loop.
  if (!uiAnimating()) delay(15);
}
