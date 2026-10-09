#pragma once
#include "battery_util.h"
#include "device_settings.h"
#include "display.h"
#include "payload.h"
#include "pomo_settings.h"
#include "pomodoro.h"
#include "volume_slider.h"

void uiBegin(Display &lcd);
// The deck is the bridge's cards, then the local cards: the Volume card while
// a Mac is bonded (`volume`), then the Pomodoro timer, always last. The
// Pomodoro exists even with no payload (bridge down, no Mac): then it is the
// only card.
uint8_t uiDeckSize(const Payload &p, bool volume = false);       // always >= 1
uint8_t uiVolumeIndex(const Payload &p);                         // valid only when the card is in the deck
uint8_t uiPomodoroIndex(const Payload &p, bool volume = false);  // the last index

// Where the data on screen came from, shown as a small marker in the top bar.
enum class UiLink : uint8_t { None, Ble, Wifi };

// Draws one card plus the shared chrome. Everything is composed into an
// off-screen sprite, then only the rows that differ from the last frame are
// sent (every screen below works the same way), so the panel never tears.
// `vol`: the Volume card's state, or nullptr when the deck has no Volume card.
void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs,
              const PomoView &pomo, const BatteryView &bat, UiLink link = UiLink::None,
              const VolumeView *vol = nullptr);
// True when the last uiRender left a ring or a colour part-way through its
// transition, so the caller knows to keep drawing frames.
bool uiAnimating();
// Sends the next frame whole, for when the panel may no longer show the last
// one (it woke from sleep). A rotation change is noticed without this.
void uiInvalidate();
// Makes the next uiRender of card `index` sweep its ring in from empty again.
void uiReplay(uint8_t index);
// Makes the Pomodoro card sweep its ring in from empty again (as uiReplay does
// for payload cards).
void uiReplayPomodoro();
// Makes the next uiRender of the Volume card sweep its fill in from empty.
void uiReplayVolume();
// Starts the Pomodoro phase-end alert: the ring and the backlight pulse for
// about two seconds. uiAnimating() stays true while it runs.
void uiAlertStart();
// Ends a running alert at once. Only uiRender drives the pulse, so a caller
// about to draw another screen (an editor, a pairing code) calls this and, when
// it returns true, puts the backlight back: it may be left mid-pulse.
bool uiAlertCancel();
// The Pomodoro settings editor: four rows of - / + and a RESET / DONE bar.
// One static frame; hit-testing is in pomo_editor.h, which owns the layout.
void uiPomodoroEditor(Display &lcd, const PomoSettings &s);
// Slides the editor panel up over (open) or back down off (!open) the card
// that uiRender draws. uiRender paints it while uiEditorSliding(); once it
// reports false the open editor is drawn by uiPomodoroEditor.
void uiEditorSlide(bool open, const PomoSettings &s);
bool uiEditorSliding();
// The display settings panel (brightness, sleep, auto-advance, refresh): same
// layout as the Pomodoro editor, but it drops down from the top. The slide
// state is shared, so uiEditorSliding() covers both.
void uiDeviceSlide(bool open, const DeviceSettings &s);
void uiDeviceEditor(Display &lcd, const DeviceSettings &s);
void uiMessage(Display &lcd, const char *title, const char *body);
// Setup screen shown while the config portal runs: the cube's own WiFi name
// as text and as a join-QR, and the address of the form on that network.
// `lanIp`: the cube's address on the saved network once the station side has
// joined it (the form is reachable there too); null or empty = not joined.
void uiPortal(Display &lcd, const char *apName, const char *apIp, const char *lanIp);

// Pairing screen. With a passkey (non-zero) it shows the 6-digit code to type
// on the Mac; with 0 it shows the "waiting for a Mac" variant.
void uiBlePair(Display &lcd, uint32_t passkey);

// Progress screen while an OTA image is arriving.
void uiOta(Display &lcd, uint8_t percent);
