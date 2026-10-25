#pragma once
#include "agent_view.h"
#include "agents_layout.h"
#include "battery_util.h"
#include "device_settings.h"
#include "display.h"
#include "nav_frame.h"
#include "payload.h"
#include "volume_slider.h"

void uiBegin(Display &lcd);
// The deck is the bridge's cards, or one "no data yet" card while there are
// none (bridge down, no Mac), then the Volume card while a Mac is bonded
// (`volume`), always last.
uint8_t uiDeckSize(const Payload &p, bool volume = false);  // always >= 1
uint8_t uiVolumeIndex(const Payload &p);                    // the last index, when the card is in the deck

// Where the data on screen came from, shown as a small marker in the top bar.
enum class UiLink : uint8_t { None, Ble, Wifi };

// What main.cpp owns for the Agents card: how far its grid is scrolled
// (agents_layout.h, ScrollY).
struct AgentsUi {
  int16_t gridScroll;
};

// Draws one card plus the shared chrome. Everything is composed into an
// off-screen sprite, then only the rows that differ from the last frame are
// sent (every screen below works the same way), so the panel never tears.
// `vol`: the Volume card's state, or nullptr when the deck has no Volume card.
// `agents`: the Agents card's grid scroll; nullptr = scrolled to the top.
void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs,
              const BatteryView &bat, UiLink link = UiLink::None,
              const VolumeView *vol = nullptr, const AgentsUi *agents = nullptr);
// True when the last uiRender left a ring or a colour part-way through its
// transition, so the caller knows to keep drawing frames.
bool uiAnimating();
// Sends the next frame whole, for when the panel may no longer show the last
// one (it woke from sleep). A rotation change is noticed without this.
void uiInvalidate();
// Makes the next uiRender of card `index` sweep its ring in from empty again.
void uiReplay(uint8_t index);
// Makes the next uiRender of the Volume card sweep its fill in from empty.
void uiReplayVolume();
// The display settings panel (brightness, sleep, auto-advance, sound): four
// rows of - / + and a RESET / DONE bar; hit-testing is in dev_editor.h and
// editor_layout.h, which own the layout. uiDeviceSlide drops it down over
// (open) or back up off (!open) the card that uiRender draws; uiRender paints
// it while uiEditorSliding(), and once that reports false the open panel is
// one static frame from uiDeviceEditor.
void uiDeviceSlide(bool open, const DeviceSettings &s);
bool uiEditorSliding();
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

// The Mac is locked: a tap asks it to unlock (unlock_prompt.h). `waiting`: a
// tap was sent and the Mac has not answered yet.
void uiUnlock(Display &lcd, bool waiting);

// The navigation map: streets, route and rider arrow under a turn banner, the
// street and ETA at the bottom; `phoneLost` shows NO PHONE. Arrived is a full
// screen of its own. Battery under 15% shows as red text, not the top bar.
// `panX`/`panY`: where the rider has dragged the map (nav_pan.h); the frame is
// shifted by that minus the pan it was built around.
void uiNav(Display &lcd, const NavScene &s, bool phoneLost, const BatteryView &bat, int16_t panX = 0, int16_t panY = 0);

// The prompt screen of a waiting agent, opened from its tile (agent_view.h
// decides `screen` and `off`). Prompt: the tile's name over a column that
// scrolls by `scroll` (agents_layout.h, PromptLayout): title, detail, context,
// one button per option, the reason the buttons are off, BACK. `lit`: the
// button whose answer was sent (-1 none). The other screens are the tile's
// critter with two lines of fixed copy; `queuedName` is the name of the agent
// to answer first (Queued only).
struct PromptUi {
  PromptScreen screen;
  ButtonsOff off;
  int8_t lit;
  int16_t scroll;
  const char *queuedName;
};
void uiAgentPrompt(Display &lcd, const AgentsData &a, const AgentTile &tile, const PromptUi &v,
                   bool online, uint32_t ageMs, const BatteryView &bat, UiLink link);
// What the prompt's text measures to with the real fonts, wrapped exactly as
// uiAgentPrompt wraps it. `reason`: the buttons' state has a reason line
// (Input, NotTmux, Locked, Sent, NoAnswer).
PromptMetrics uiPromptMetrics(const AgentPrompt &q, bool reason);
// The Prompt screen's layout as uiAgentPrompt draws it (an input prompt has no
// option buttons): main.cpp hit-tests and sizes its scroll with this.
PromptLayout uiPromptLayout(const AgentPrompt &q, ButtonsOff off);

// The detail screen of an agent that is not waiting, opened from its tile
// (detail_layout.h places `< back` and the critter, which main.cpp
// hit-tests): status pill, critter in `hat` (the shown one: a pick in flight
// wins over the tile's `h`), title, subagents, what it is doing, context bar,
// last prompt, meta line, from the Agents card's `d` when it is this agent's
// and came over BLE (`viaBle`); until then `loading...`, or
// `details need Bluetooth` for a WiFi payload.
void uiAgentDetail(Display &lcd, const AgentsData &a, const AgentTile &tile, int16_t hat, bool viaBle,
                   bool online, uint32_t ageMs, const BatteryView &bat, UiLink link);

// Progress screen while an OTA image is arriving.
void uiOta(Display &lcd, uint8_t percent);
