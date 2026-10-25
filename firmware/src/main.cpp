// Claude status cube -- Waveshare ESP32-S3-Touch-LCD-1.69
//
// Polls the host bridge for a small pre-formatted JSON payload and renders it
// as a swipeable deck of cards. Swipe left/right to change card; the deck can
// also auto-advance (the rotate setting). On the Agents card a tap on a waiting
// agent opens its prompt, whose buttons answer it over BLE.

#include <Arduino.h>
#include <string.h>
#include "agent_alert.h"
#include "agent_view.h"
#include "agents_layout.h"
#include "battery.h"
#ifdef BATTERY_LOG
#include "battery_log.h"
#endif
#include "ble.h"
#include "buzzer.h"
#include "config.h"
#include "cpu_policy.h"
#include "deck_util.h"
#include "display.h"
#include "gesture.h"
#include "idle_sleep.h"
#include "nav_pan.h"
#include "nav_session.h"
#include "imu.h"
#include "net.h"
#include "orientation.h"
#include "ota.h"
#include "payload.h"
#include "dev_editor.h"
#include "portal.h"
#include "readings_key.h"
#include "settings.h"
#include "settings_json.h"
#include "touch.h"
#include "touch_map.h"
#include "transport_policy.h"
#include "ui.h"
#include "unlock_prompt.h"
#include "volume_slider.h"

// Keep WiFi up while BLE is live (OTA needs it). A config.h from before this
// existed still builds.
#ifndef WIFI_ALWAYS_ON
#define WIFI_ALWAYS_ON 0
#endif

namespace {

// While a ring moves a frame is drawn on every pass, and the pass is held to
// this so the loop does not spin: ~60 fps, which is what a whole-frame push
// over SPI allowed before only changed rows were sent. The animations are
// timed, so this sets how smooth they look, not how long they take.
constexpr uint32_t FRAME_MS = 16;

Display lcd;
Touch touch;
Payload payload{};
uint8_t lastBatPct = 255;  // battery percent last drawn, to redraw when it changes

uint8_t cardIndex = 0;
uint32_t lastPoll = 0;
uint32_t lastRotate = 0;
uint32_t lastGood = 0;  // millis() of the last successful fetch
uint32_t lastReadings = 0;  // readingsKey() of the last payload: only a change keeps the screen awake
uint32_t lastDraw = 0;
bool dirty = true;
// The Volume card: in the deck while a Mac is bonded, after the payload cards.
// `deckVolume` is what the deck was last built with, so a bond coming or going
// can keep the viewer on their card.
VolumeSlider volSlider;
// The media button last tapped on the Volume card (a MediaKey, -1 none), lit for VOLUME_PRESS_MS.
int8_t mediaPressed = -1;
uint32_t mediaPressedAt = 0;
bool deckVolume = false;
// The Mac is locked and unlocks for a tap (unlock_prompt.h): a screen over the deck.
UnlockPrompt unlock;
bool volumeShown() { return bleMacBonded(); }
uint8_t deckSize() { return uiDeckSize(payload, deckVolume); }
bool onVolumeCard() { return deckVolume && cardIndex == uiVolumeIndex(payload); }
DeckSpot deckSpot() { return onVolumeCard() ? DeckSpot::Volume : DeckSpot::Payload; }

// The Agents card (agents_layout.h, agent_view.h, agent_alert.h). Its grid
// scrolls under a vertical drag; a tap on a waiting tile opens that agent's
// prompt screen, drawn instead of the deck until BACK or a swipe right.
ScrollY gridScroll;
AgentView agentView;
ScrollY promptScroll;
// What uiAgentPrompt draws now, for hit-testing and the column's scroll range.
// Building it measures the text, so it is rebuilt only when what it depends on
// changed (a payload, the view's state).
PromptLayout promptLayout;
bool promptLayoutStale = true;
AgentAlert agentAlert;
bool payloadViaBle = false;  // the payload came over BLE: only such a prompt is shown or answered
bool macLockedNow = false;   // the Mac's screen is locked (Volume bits 5/6): the buttons are off
bool lastBleLive = false;    // the transport the view last saw, to update it when the link changes
bool touchOnGrid = false;    // the finger now down landed in a grid that scrolls (latched at landing)
// A tap on the prompt screen waits for this pass's payload and Volume intake,
// so it is checked against the prompt the view knows about now, not the one a
// moment ago (a new prompt id restarts the settle time first). -1 = none.
int16_t promptTapY = -1;
bool promptDrawn = false;  // the last frame drawn was a prompt with its buttons (bench)
bool onAgentsCard() {
  return payload.agents.present && !onVolumeCard() && cardIndex < payload.nCards && cardIndex == payload.agents.card;
}
bool gridScrolls() { return agentsContentH(payload.agents.nTiles) > AGENTS_VIEW_H; }

TransportPolicy policy;
bool wifiUp = false;           // netStart() called and not yet netStop()
bool otaUp = false;            // otaBegin() called and not yet otaEnd()
IdleSleep idle;
// Navigation (nav_session.h): while a ride runs its map owns the screen. The scene is ~4 KB, so it
// lives here and not on the loop's stack.
NavSession navSession;
NavScene navScene;
NavPan navPan;  // the dragged map (nav_pan.h)
bool navPhoneLost = false;  // what the map last showed: a change redraws at once

// While riding, the time one map frame takes to compose and send (ms), logged
// every NAV_LOG_MS: the last frame's and the slowest since the previous line.
constexpr uint32_t NAV_LOG_MS = 10000;
uint32_t navLogAt = 0, navComposeMax = 0;
void logNavCompose(uint32_t now, uint32_t ms) {
  if (ms > navComposeMax) navComposeMax = ms;
  if (navLogAt == 0) navLogAt = now;
  if (now - navLogAt < NAV_LOG_MS) return;
  Serial.printf("[nav] compose %u ms (max %u)\n", (unsigned)ms, (unsigned)navComposeMax);
  navLogAt = now;
  navComposeMax = 0;
}
bool screenOn = true;
CpuPolicy cpuPolicy;  // fast clock for touch and animation, slow otherwise (cpu_policy.h)
uint32_t cpuMhz = CPU_MHZ_FAST;  // the clock last set; the board boots at board_build.f_cpu
bool swallowTouch = false;  // the touch that woke the screen is ignored until the finger lifts
bool pairWaitDismissed = false;  // a touch dismisses the first-boot "waiting for a Mac" screen

// Auto-rotate (orientation.h). `orient` is what the IMU says; `appliedRot` is what
// is on the panel: they differ only while the screen sleeps, so the panel is
// never written to while it is off. Touch is remapped by `appliedRot`.
Orientation orient;
uint8_t appliedRot = 0;
bool imuUp = false;
bool touchOnVolumeRow = false;  // ... and landed in its button row: a tap there, never a drag
bool touchOnVolume = false;  // the finger now down landed on the Volume card (latched at landing)
bool touchDown = false;  // a finger was on the glass at the last poll: do not flip under it
uint32_t lastImu = 0;
constexpr uint32_t IMU_POLL_MS = 200;

// The "PAIRING OPEN" message covers the deck for the pairing window; a tap dismisses it for the rest
// of that window (it is cleared when the window ends). Until then gestures do not reach the deck.
bool pairMsgDismissed = false;
bool pairingMsgUp() { return blePairingOpen() && !pairMsgDismissed; }

// What the backlight is set to now: full while navigation runs (a map in daylight), else the setting.
uint8_t shownBacklight() { return navSession.active() ? 255 : deviceSettings().backlight; }

// First boot with neither a Mac nor a network and nothing received yet.
bool waitingToPair() { return pairWaitAtBoot(bleBonded(), settingsHaveWifi()) && !payload.valid; }

// Gestures come from raw coordinates (see gesture.h), which keeps this working
// across the CST816S/T/D variants, whose gesture registers disagree.
GestureTracker gestures;

// The display panel (swipe down on any card but Volume). While it is open
// nothing else moves the deck. `editDevice` is the working copy.
bool editing = false;
DeviceSettings editDevice{};

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
  // first payload to arrive is swiped away from its first card at once.
  lastRotate = millis();
  const uint8_t n = deckSize();
  if (n <= 1) return;
  cardIndex = (cardIndex + n + delta) % n;
  if (onVolumeCard()) uiReplayVolume();
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
  // The Mac's write wins over an open editor: it shows what is now stored,
  // rather than saving its older copy over it on DONE.
  if (editing) editDevice = deviceSettings();
  if (screenOn) lcd.setBrightness(shownBacklight());
  buzzerSetLevel(deviceSettings().sound);
  lastRotate = millis();
  dirty = true;
  if (r == SettingsResult::OkReboot) {
    delay(500);  // let the notification leave
    ESP.restart();
  }
}

void openDeviceEditor() {
  editDevice = deviceSettings();
  editing = true;
  uiDeviceSlide(true, editDevice);
  dirty = true;
}

// DONE and swipe-up both land here. Brightness and sound were previewed live
// while editing; the rest takes effect now.
void closeDeviceEditor() {
  if (editDevice != deviceSettings()) {  // spare the flash
    deviceSettingsSave(editDevice);
    bleSettingsChanged();
  }
  lcd.setBrightness(deviceSettings().backlight);
  buzzerSetLevel(deviceSettings().sound);
  editing = false;
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
      const uint8_t soundBefore = editDevice.sound;
      devEditorApply(editDevice, hit, deviceDefaults(), deviceSettings());
      lcd.setBrightness(editDevice.backlight);
      if (editDevice.sound != soundBefore) {  // previewed live, like brightness
        buzzerSetLevel(editDevice.sound);
        buzzerPlay(Sound::Preview);  // silent when it was just switched off
      }
      dirty = true;
    }
  }
}

// After every payload, link change and Volume update (and an open): the view
// closes itself when its agent stopped waiting, so this may return to the grid.
void updateAgentView(uint32_t now) {
  if (!agentView.isOpen()) return;
  if (agentView.update(payload.agents, payloadViaBle, macLockedNow, now)) {
    promptLayoutStale = true;
    dirty = true;
  }
  if (!agentView.isOpen()) {  // back on the grid: as closeAgentView
    promptScroll.reset();
    promptTapY = -1;
    lastRotate = now;
  }
}

const PromptLayout &currentPromptLayout(uint32_t now) {
  if (promptLayoutStale) {
    promptLayout = PromptLayout{};
    if (agentView.screen() == PromptScreen::Prompt && payload.agents.hasPrompt) {
      promptLayout = uiPromptLayout(payload.agents.prompt, agentView.buttonsOff(now));
    }
    promptScroll.setRange(promptLayout.contentH(), PROMPT_VIEW_H);
    promptLayoutStale = false;
  }
  return promptLayout;
}

void openAgentView(const char *agent, uint32_t now) {
  agentView.open(agent, now);
  promptScroll.reset();
  promptLayoutStale = true;
  updateAgentView(now);
  dirty = true;
}

void closeAgentView(uint32_t now) {
  agentView.close();
  promptScroll.reset();
  promptTapY = -1;
  lastRotate = now;  // auto-advance, held while it was open, starts over
  dirty = true;
}

// The prompt screen: a vertical drag scrolls its column, a swipe right closes
// it, a tap is routed later in the pass (routePromptTap). The other swipes do
// nothing, so the display panel does not open over it.
void promptGesture(Gesture g, uint32_t now) {
  switch (g) {
    case Gesture::DragStart:
      currentPromptLayout(now);  // the scroll range
      promptScroll.grab(gestures.startY());
      if (promptScroll.drag(gestures.lastY())) dirty = true;
      break;
    case Gesture::Drag:
      if (promptScroll.drag(gestures.lastY())) dirty = true;
      break;
    case Gesture::DragEnd:
      promptScroll.release();
      break;
    case Gesture::Tap:
      promptTapY = gestures.startY();
      break;
    case Gesture::SwipePrev:
      closeAgentView(now);
      break;
    default:
      break;
  }
}

// A tap on an answer button sends Control 09 when the view allows it (settled,
// not locked, not already answered); BACK closes the screen.
void routePromptTap(uint32_t now) {
  if (promptTapY < 0) return;
  const int16_t y = promptTapY;
  promptTapY = -1;
  if (!agentView.isOpen() || agentView.screen() != PromptScreen::Prompt || !payload.agents.hasPrompt) return;
  const int hit = currentPromptLayout(now).buttonAt(y, promptScroll.offset());
  if (hit == PROMPT_HIT_BACK) {
    closeAgentView(now);
    return;
  }
  if (hit < 0 || !agentView.tap((uint8_t)hit, now)) return;
  const AgentPrompt &q = payload.agents.prompt;
  // No Mac to send to: say so now rather than after the answer wait.
  if (!bleSendAnswer(q.agent, q.id, q.opts[hit].key)) agentView.answerFailed();
  promptLayoutStale = true;  // the reason line appears
  dirty = true;
}

void pollTouch() {
  int16_t x = 0, y = 0;
  const bool down = touch.read(x, y);
  const bool landing = down && !touchDown;
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
  if (down) unlock.touch(now);
  // The unlock prompt covers the deck: a tap there is for it alone.
  const bool onUnlock = !editing && !navSession.active() && !pairingMsgUp() && unlock.screen(now) != UnlockScreen::Hidden;
  // An open prompt screen covers the deck, below the unlock prompt.
  const bool onPrompt = agentView.isOpen() && !editing && !onUnlock && !navSession.active() && !pairingMsgUp();
  // On the Volume card a vertical move is the volume, not a swipe (gesture.h).
  // Whether it is the slider's touch is decided where the finger landed, so a touch that began on
  // another card never reaches it; the latch clears on the lift pass, after the gesture is routed.
  // On the Agents card a vertical move scrolls the grid, but only for a touch
  // that lands in the grid and only when the grid is taller than its view: a
  // swipe down from the caption, or on a grid that fits, still opens the
  // display panel.
  if (landing) {
    const bool deckUp = !editing && !onUnlock && !onPrompt && !navSession.active() && !pairingMsgUp();
    touchOnVolume = onVolumeCard() && deckUp;
    touchOnVolumeRow = touchOnVolume && volumeInRow(y);
    touchOnGrid = onAgentsCard() && deckUp && gridScrolls() && y >= AGENTS_VIEW_Y && y < AGENTS_VIEW_Y + AGENTS_VIEW_H;
  }
  const bool onVolume = touchOnVolume && onVolumeCard() && !editing;
  const bool onVolumeRow = onVolume && touchOnVolumeRow;
  const bool onGrid = touchOnGrid && onAgentsCard() && !editing && !onPrompt;
  if (!down) touchOnVolume = touchOnVolumeRow = touchOnGrid = false;
  // In the button row the tracker stays in tap mode: a press that drifts a few
  // pixels is still a Tap (TAP_MAX_PX), not a drag of the slider. On the map a
  // move either way drags it.
  const bool onMap = navSession.active() && bleState() != BleState::Pairing && navScene.state != NavState::Arrived;
  const DragMode drag = onMap ? DragMode::Free
                       : ((onVolume && !onVolumeRow) || onPrompt || onGrid) ? DragMode::Vertical
                                                                             : DragMode::Off;
  const Gesture gesture = gestures.update(down, x, y, now, drag);
  // The deck left the card under a dragging finger (the bond went): let go, so
  // the last level is still sent.
  if (volSlider.held() && !onVolume) {
    volSlider.release(now);
    dirty = true;
  }
  // Navigation owns the screen: a drag moves the map, a tap brings it back to the rider (nav_pan.h).
  if (navSession.active()) {
    if (volSlider.held()) volSlider.release(now);
    switch (gesture) {
      case Gesture::DragStart:
        if (!onMap) break;
        navPan.grab(gestures.startX(), gestures.startY(), now);
        navPan.drag(gestures.lastX(), gestures.lastY(), now);
        dirty = true;
        break;
      case Gesture::Drag:
        if (navPan.drag(gestures.lastX(), gestures.lastY(), now)) dirty = true;
        break;
      case Gesture::DragEnd:
        navPan.release(now);
        break;
      case Gesture::Tap:
        if (onMap && navPan.recenter(now)) dirty = true;
        break;
      default:
        break;
    }
    return;
  }
  // Likewise under the "PAIRING OPEN" message: a tap dismisses it, nothing else acts.
  if (pairingMsgUp()) {
    if (volSlider.held()) volSlider.release(now);
    if (gesture == Gesture::Tap) {
      pairMsgDismissed = true;
      dirty = true;
    }
    return;
  }
  // While the panel slides in its buttons are not where the hit-test puts
  // them, so nothing is pressed until it has landed.
  if (editing && uiEditorSliding()) return;
  if (editing) {
    deviceEditorGesture(gesture);
    return;
  }
  if (onUnlock) {
    if (gesture == Gesture::Tap) {
      if (unlock.tap(now)) bleSendUnlock();
      dirty = true;
    } else if (gesture == Gesture::SwipeNext || gesture == Gesture::SwipePrev || gesture == Gesture::SwipeUp ||
               gesture == Gesture::SwipeDown) {
      unlock.dismiss(now);  // show the deck; the prompt comes back when the cube is left alone
      lastRotate = now;
      dirty = true;
    }
    return;
  }
  if (onPrompt) {
    promptGesture(gesture, now);
    return;
  }
  if (onAgentsCard() && !onVolume) {
    switch (gesture) {
      case Gesture::DragStart:
        if (!onGrid) break;
        gridScroll.grab(gestures.startY());
        gridScroll.drag(gestures.lastY());
        lastRotate = now;  // touching the card holds off auto-advance, as on Volume
        dirty = true;
        break;
      case Gesture::Drag:
        if (onGrid && gridScroll.drag(gestures.lastY())) {
          lastRotate = now;
          dirty = true;
        }
        break;
      case Gesture::DragEnd:
        gridScroll.release();
        break;
      case Gesture::Tap: {  // only a waiting agent has a prompt to open
        const AgentsData &a = payload.agents;
        const int i = agentsTileAt(gestures.startX(), gestures.startY(), a.nTiles, gridScroll.offset());
        if (i >= 0 && a.tiles[i].kind == AgentKind::Wait) openAgentView(a.tiles[i].id, now);
        break;
      }
      default:
        break;
    }
  }
  if (onVolume) {
    bool acted = false;
    switch (gesture) {
      case Gesture::DragStart:
        if (volumeInPill(gestures.startX(), gestures.startY()) && volSlider.grab(gestures.startY())) {
          volSlider.drag(gestures.lastY());
          acted = true;
        }
        break;
      case Gesture::Drag:
        acted = volSlider.drag(gestures.lastY());
        break;
      case Gesture::DragEnd:
        acted = volSlider.release(now);
        break;
      case Gesture::Tap: {
        MediaKey key;
        if (volSlider.state() != VolState::NoMac && volumeMediaAt(gestures.startX(), gestures.startY(), key)) {
          bleSendMedia(key);
          if (key == MediaKey::PlayPause) volSlider.guessPlayToggled(now);
          mediaPressed = (int8_t)key;
          mediaPressedAt = now;
          acted = true;
        } else {
          acted = volSlider.tap(gestures.startX(), gestures.startY(), now);
        }
        break;
      }
      default:
        break;
    }
    if (acted) {
      lastRotate = now;  // touching the card holds off auto-advance, as a swipe does
      dirty = true;
    }
  }
  switch (gesture) {
    case Gesture::SwipeNext:  // swipe left advances (a tap does nothing)
      step(1);
      break;
    case Gesture::SwipePrev:
      step(-1);
      break;
    case Gesture::SwipeDown:  // open the display settings panel, from any card but Volume
      if (!onVolume && devEditorMayOpen(editing)) openDeviceEditor();
      break;
    default:
      break;
  }
}

void pollOrientation(uint32_t now) {
  if (navSession.active()) return;  // a vibrating mount must not flip the map
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

void setCpu(uint32_t mhz) {
  if (mhz == cpuMhz) return;
  setCpuFrequencyMhz(mhz);
  cpuMhz = mhz;
}

uint32_t pollMs() { return (uint32_t)deviceSettings().pollSec * 1000u; }
uint32_t rotateMs() { return (uint32_t)deviceSettings().rotateSec * 1000u; }
uint32_t sleepMs() { return (uint32_t)deviceSettings().sleepMin * 60000u; }

}  // namespace

void setup() {
  const bool buzzOk = buzzerBegin();  // first: until then the buzzer pin floats (buzzer.h)
  batteryHoldPower();  // on battery, the board stays on after PWR is released only once this runs
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] claude-status-cube");
  if (!buzzOk) Serial.println("[buzz] ledc setup failed");
  settingsLoad();
  buzzerSetLevel(deviceSettings().sound);

  lcd.init();
  lcd.setRotation(0);
  lcd.setBrightness(deviceSettings().backlight);
  uiBegin(lcd);

  touch.begin();
  imuUp = imuBegin();  // after touch.begin(), which starts the shared I2C bus
  batteryBegin();
#ifdef BATTERY_LOG
  batteryLogBegin();
#endif

  // BLE first: the stack must be up before bleBonded() means anything. The
  // setup portal is asked for by holding the screen; WiFi is joined at boot
  // only when neither a Mac nor an iPhone is bonded (the old behaviour), and a
  // failed join opens the portal only with no bond at all (bootWifiPlan in
  // transport_policy.h). Otherwise WiFi comes up later, from the loop, when the
  // transport policy wants it.
  bleBegin();
  if (setupHoldRequested()) portalRun(lcd, false);

  const BootWifiPlan boot = bootWifiPlan(bleBonded(), bleMacBonded(), blePhoneBonded(), settingsHaveWifi());
  if (boot.join) {
    uiMessage(lcd, "CONNECTING", settings().ssid);
    const bool joined = netBegin();
    wifiUp = true;
    if (!joined && boot.portalOnFail) portalRun(lcd, true);
    if (joined) {
      otaBegin(lcd);
      otaUp = true;
    }
  } else if (bleMacBonded() && !settingsHaveWifi()) {
    uiMessage(lcd, "NO MAC", "WAITING FOR LINK");
  }

  if (blePairingOpen()) uiMessage(lcd, "PAIRING OPEN", "PAIR FROM THE APP OR MAC");
  idle.begin(millis());
  policy.begin(millis());
  cpuPolicy.begin(millis());
  // First poll on the first loop pass rather than a full interval from now.
  lastPoll = millis() - pollMs();
  lastRotate = millis();
  deckVolume = volumeShown();
  cardIndex = 0;
  dirty = true;
}

void loop() {
  if (otaUp) otaHandle();
  const uint32_t now = millis();

  buzzerUpdate(now);  // every pass, before the screen-off return below
  pollTouch();
  pollOrientation(now);
  batteryUpdate(now);
#ifdef BATTERY_LOG
  // "link" = the Mac delivered a payload in the last 15 s (its resend is every 5 s).
  batteryLogUpdate(now, screenOn, bleLastGood() != 0 && now - bleLastGood() < 15000);
#endif

  // Which transport feeds the cube (transport_policy.h): BLE while the Mac is
  // delivering, WiFi when it is not.
  // During a ride WiFi stays off (one radio: it would take airtime from the phone's link).
  const TransportDecision td = policy.update(now, bleLastGood(), bleMacBonded(), settingsHaveWifi(),
                                             WIFI_ALWAYS_ON != 0, navSession.active());
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
  // The card follows the bond. Settled before `was`, so a payload arriving in
  // the same pass keeps the viewer where this left them.
  if (volumeShown() != deckVolume) {
    const DeckSpot spot = deckSpot();
    deckVolume = !deckVolume;
    cardIndex = deckKeepIndex(spot, cardIndex, deckSize(), deckVolume);
    if (onVolumeCard()) uiReplayVolume();
    dirty = true;
  }
  const DeckSpot was = deckSpot();
  if (bleTake(payload)) {
    gotData = true;
    payloadViaBle = true;
  }
  // Navigation frames from the iPhone, drained every pass so the hand-over buffer stays fresh.
  const bool navFrame = bleTakeNav(navScene);
  if (navFrame) {
    navSession.frame(navScene.state, now);
    dirty = true;
  }
  navSession.tick(now);
  // NO PHONE shows the moment the phone counts as lost, not on the next once-a-second redraw.
  const bool phoneLost = navSession.phoneLost(now);
  if (phoneLost != navPhoneLost) {
    navPhoneLost = phoneLost;
    dirty = true;
  }
  if (navSession.takeStarted()) {
    Serial.println("[nav] ride started");
    navPan.reset();
    navLogAt = navComposeMax = 0;  // the first compose line comes NAV_LOG_MS into the ride
    if (editing) {  // closed without saving: the map cannot wait for DONE
      editing = false;
      uiDeviceSlide(false, editDevice);
      buzzerSetLevel(deviceSettings().sound);
    }
    if (volSlider.held()) volSlider.release(now);
    if (screenOn) lcd.setBrightness(255);
    dirty = true;
  }
  if (navSession.takeEnded()) {
    Serial.println("[nav] ride ended");
    navPan.reset();
    if (screenOn) lcd.setBrightness(deviceSettings().backlight);
    lastRotate = now;  // auto-advance does not fire at once
    if (onVolumeCard()) uiReplayVolume();
    else uiReplay(cardIndex);
    dirty = true;
  }
  if (navSession.active()) {
    if (navPan.tick(now)) dirty = true;  // the glide back to the rider
    int16_t px, py;
    if (navPan.takeSend(now, px, py)) bleSendNavPan(px, py);
  }
  handleBleSettings();
  // The Mac's volume. Not a reading: it never keeps the screen awake.
  MacVolume macVol{};
  if (bleTakeVolume(macVol)) {
    volSlider.fromMac(macVol, now);
    unlock.fromMac(macVol.known && macVol.macLocked, now);  // unknown = the link dropped
    // Locked at all (bit 6), whether or not it unlocks for the cube: nobody at
    // the desk answers for it. Unknown = the link dropped; no BLE payload is fresh then either.
    macLockedNow = macVol.known && (macVol.macLocked || macVol.screenLocked);
    updateAgentView(now);
    dirty = true;
  }
  if (unlock.tick(now)) dirty = true;
  if (volSlider.tick(now)) dirty = true;
  if (mediaPressed >= 0 && now - mediaPressedAt >= VOLUME_PRESS_MS) {
    mediaPressed = -1;
    dirty = true;
  }
  uint8_t volLevel = 0;
  bool volMuted = false;
  if (volSlider.takeSend(now, volLevel, volMuted)) bleSendVolume(volLevel, volMuted);
  if (wifiUp && now - lastPoll >= pollMs()) {
    lastPoll = now;
    netRequest();  // runs on its own task; netTake() hands over the result on a later pass
  }
  const NetFetch fetched = netTake(payload);
  if (fetched == NetFetch::Ok) {
    gotData = true;
    payloadViaBle = false;  // the LAN variant: never prompt text
  }
  if (fetched == NetFetch::Failed) Serial.printf("[net] fetch failed: %s\n", netLastError());
  if (fetched != NetFetch::None) dirty = true;
  if (gotData) {
    lastGood = now;
    // The Mac resends the same payload every 5 s, so only new readings count
    // as activity for screen sleep (readings_key.h).
    const uint32_t readings = readingsKey(payload);
    if (readings != lastReadings) {
      lastReadings = readings;
      idle.data(now);
    }
    // The deck may have changed size: keep the viewer's card under them.
    cardIndex = deckKeepIndex(was, cardIndex, deckSize(), deckVolume);
    agentAlert.update(payload.agents);
    promptLayoutStale = true;  // the prompt's text may have changed
    updateAgentView(now);
    dirty = true;
  }
  if (td.bleLive != lastBleLive) {
    lastBleLive = td.bleLive;
    updateAgentView(now);
  }
  if (agentView.tick(now)) {  // the settle or the answer wait ran out
    promptLayoutStale = true;
    dirty = true;
  }
  // Only now, with this pass's payload and lock state in the view.
  routePromptTap(now);
  // The grid starts at its top whenever the deck comes back to the card.
  if (onAgentsCard()) gridScroll.setRange(agentsContentH(payload.agents.nTiles), AGENTS_VIEW_H);
  else gridScroll.reset();

  // An agent started waiting: wake, jump to its prompt and beep, once per
  // prompt (agent_alert.h). Held while something else owns the screen or a
  // finger is down; it fires when that ends.
  char alertAgent[16];
  const bool alertBlocked = navSession.active() || editing || touchDown || bleState() == BleState::Pairing ||
                            pairingMsgUp() || waitingToPair() || unlock.screen(now) != UnlockScreen::Hidden;
  if (agentAlert.take(alertBlocked, alertAgent, sizeof(alertAgent))) {
    Serial.printf("[agents] %s is waiting\n", alertAgent);
    idle.wake(now);  // the screen comes on below, through the usual wake
    cardIndex = payload.agents.card;
    gridScroll.reset();
    if (!agentView.isOpen() || strcmp(agentView.agent(), alertAgent) != 0) openAgentView(alertAgent, now);
    buzzerPlay(Sound::Attention);  // silent at SOUND off
    dirty = true;
  }

  if (!blePairingOpen()) pairMsgDismissed = false;
  if (rotateMs() > 0 && !editing && !navSession.active() && !agentView.isOpen() && !touchDown && !volSlider.held() && now - lastRotate >= rotateMs()) {
    step(1);
  }

  const BatteryView bv = batteryView();
  if (bv.pct != lastBatPct) {
    lastBatPct = bv.pct;
    dirty = true;
  }

  // Screen sleep: never while the editor or a pairing code is up (see idle_sleep.h).
  const bool keepOn = editing || navSession.active() || pairingMsgUp() || bleState() == BleState::Pairing || (!pairWaitDismissed && waitingToPair());
  const bool wantOn = idle.update(now, keepOn, sleepMs());
  if (wantOn != screenOn) {
    screenOn = wantOn;
    if (screenOn) {
      setCpu(CPU_MHZ_FAST);
      lcd.wakeup();
      lcd.setBrightness(shownBacklight());
      uiInvalidate();  // the first frame after sleep goes out whole
      dirty = true;
    } else {
      lcd.setBrightness(0);
      lcd.sleep();
      setCpu(CPU_MHZ_SLOW);
    }
  }
  if (!screenOn) {
    delay(50);  // touch is still polled, so a tap wakes it at once
    return;
  }
  applyRotation();
  setCpu(cpuPolicy.update(now, touchDown || uiAnimating() || navSession.active()));

  // Redraw on change, every frame while a ring is still moving, and once a
  // second otherwise so the freshness counter ticks.
  if (dirty || uiAnimating() || now - lastDraw >= 1000) {
    const uint32_t age = lastGood ? now - lastGood : now;
    const bool pairing = bleState() == BleState::Pairing;
    const bool waiting = !pairWaitDismissed && waitingToPair();
    const bool editorUp = editing && !uiEditorSliding();
    const bool navOn = navSession.active();
    const UnlockScreen us = editing ? UnlockScreen::Hidden : unlock.screen(now);
    const bool online = td.bleLive || netOnline();
    const UiLink link = td.bleLive ? UiLink::Ble : (wifiUp && netOnline() ? UiLink::Wifi : UiLink::None);
    // The open prompt's agent (update() closed the view if its tile went).
    const AgentTile *promptTile = agentView.isOpen() ? agentsTile(payload.agents, agentView.agent()) : nullptr;
    promptDrawn = false;
    if (pairing) uiBlePair(lcd, blePasskey());  // the code must be seen
    else if (navOn) {
      const uint32_t t0 = millis();
      uiNav(lcd, navScene, navPhoneLost, bv, navPan.x(), navPan.y());
      logNavCompose(now, millis() - t0);
    }
    else if (pairingMsgUp()) uiMessage(lcd, "PAIRING OPEN", "PAIR FROM THE APP OR MAC");
    else if (waiting) uiBlePair(lcd, 0);
    else if (editorUp) uiDeviceEditor(lcd, editDevice);
    else if (us != UnlockScreen::Hidden) uiUnlock(lcd, us == UnlockScreen::Waiting);
    else if (promptTile) {
      const AgentsData &a = payload.agents;
      const AgentTile *first = a.hasPrompt ? agentsTile(a, a.prompt.agent) : nullptr;
      currentPromptLayout(now);  // keeps the column's scroll range current
      const PromptUi pv{agentView.screen(), agentView.buttonsOff(now), agentView.sentButton(), promptScroll.offset(),
                        first ? first->name : ""};
      uiAgentPrompt(lcd, a, *promptTile, pv, online, age, bv, link);
      promptDrawn = pv.screen == PromptScreen::Prompt;
    } else {
      VolumeView vv = volSlider.view();
      vv.pressed = mediaPressed;
      const AgentsUi au{gridScroll.offset()};
      uiRender(lcd, payload, cardIndex, online, age, bv, link, deckVolume ? &vv : nullptr, &au);
    }
    lastDraw = now;
    dirty = false;
  }

  // Touch sampling cadence; keeps the loop off a busy spin. While a ring is
  // moving the pass is paced to FRAME_MS instead, counting the frame just drawn.
  if (uiAnimating()) {
    const uint32_t spent = millis() - now;
    if (spent < FRAME_MS) delay(FRAME_MS - spent);
  } else {
    delay(15);
  }
}

#ifdef SIM_BENCH
// The bench checks that a ride leaves the deck where it was, that WiFi stays off during it, and
// that an agent alert leaves the prompt on screen.
uint8_t benchCardIndex() { return cardIndex; }
bool benchNavActive() { return navSession.active(); }
bool benchPromptOpen() { return promptDrawn; }
#endif
