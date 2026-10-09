#include <cstring>

#include "check.h"
#include "volume_slider.h"

namespace {

// Where the finger lands in these tests (mid-card), the bar's centre line, and
// the travel that moves the level by 10.
constexpr int Y0 = 148;
constexpr int BX = VOL_BAR_X + VOL_BAR_W / 2;
constexpr int UP10 = 16;

MacVolume live(uint8_t level, bool muted = false) {
  MacVolume m{};
  m.known = true;
  m.level = level;
  m.muted = muted;
  m.canSet = m.canMute = true;
  strcpy(m.name, "MacBook Pro Speakers");
  return m;
}

void testGeometry() {
  CHECK(volumeLevelAfter(40, 0) == 40);              // no travel, no change
  CHECK(volumeLevelAfter(40, -UP10) == 50);          // up raises
  CHECK(volumeLevelAfter(40, UP10) == 30);           // down lowers
  CHECK(volumeLevelAfter(0, -VOL_DRAG_PX) == 100);   // the whole range in VOL_DRAG_PX
  CHECK(volumeLevelAfter(100, VOL_DRAG_PX) == 0);
  CHECK(volumeLevelAfter(90, -VOL_DRAG_PX) == 100);  // clamped
  CHECK(volumeLevelAfter(10, VOL_DRAG_PX) == 0);
  CHECK(volumeLevelAfter(40, -1) == 41);             // one pixel is a step (1.64 px each)
  CHECK(volumeLevelAfter(40, 1) == 39);
  // Monotonic: farther up, never lower.
  for (int dy = 200; dy > -200; dy--) CHECK(volumeLevelAfter(50, dy - 1) >= volumeLevelAfter(50, dy));
  CHECK(volumeFillTop(100) == VOL_BAR_Y);
  CHECK(volumeFillTop(0) == VOL_BAR_Y + VOL_BAR_H);
  CHECK(volumeFillTop(50) == VOL_BAR_Y + VOL_BAR_H / 2);
  int prev = volumeFillTop(0.0f);
  for (int i = 1; i <= 400; i++) {
    const int y = volumeFillTop(i * 0.25f);
    CHECK(y <= prev);
    prev = y;
  }
  CHECK(VOL_PILL_RECT.contains(VOL_MUTE_CX, VOL_MUTE_CY));
  CHECK(VOL_MUTE_ZONE.contains(VOL_MUTE_CX, VOL_MUTE_CY));
  CHECK(!VOL_MUTE_ZONE.contains(VOL_MUTE_CX, VOL_ROW_Y));  // not the buttons
  CHECK(!VOL_MUTE_ZONE.contains(BX, Y0));                  // not the bar
  CHECK(!VOL_MUTE_ZONE.contains(VOL_MUTE_CX, 60));         // not the name
}

void testNoMacByDefault() {
  VolumeSlider s;
  CHECK(s.state() == VolState::NoMac);
  CHECK(!s.grab(Y0));
  CHECK(!s.tap(BX, Y0, 0));
  CHECK(!s.tap(VOL_MUTE_CX, VOL_MUTE_CY, 0));
  const VolumeView v = s.view();
  CHECK(v.state == VolState::NoMac && v.level == 0 && !v.muted && v.name[0] == '\0');
  uint8_t l;
  bool m;
  CHECK(!s.takeSend(0, l, m));
}

void testLiveApplies() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  const VolumeView v = s.view();
  CHECK(v.state == VolState::Live && v.level == 40 && !v.muted && v.canMute && !v.tracking);
  CHECK(strcmp(v.name, "MacBook Pro Speakers") == 0);
}

// While dragging, at most one request every VOLUME_SEND_MS; the first goes at
// once, and the release always sends the final value.
void testDragThrottle() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l = 0;
  bool m = true;
  CHECK(s.grab(Y0));
  CHECK(s.held() && s.view().tracking);
  CHECK(s.view().level == 40);               // landing alone changes nothing
  CHECK(!s.takeSend(1000, l, m));
  CHECK(s.drag(Y0 - UP10));
  CHECK(s.takeSend(1000, l, m) && l == 50 && !m);
  CHECK(s.drag(Y0 - 2 * UP10));
  CHECK(!s.takeSend(1020, l, m));            // 20 ms after the last: wait
  CHECK(s.takeSend(1050, l, m) && l == 60);
  CHECK(s.drag(Y0 - 3 * UP10));
  CHECK(s.release(1070));
  CHECK(!s.held());
  CHECK(s.takeSend(1070, l, m) && l == volumeLevelAfter(40, -3 * UP10));  // final value, unthrottled
  CHECK(!s.takeSend(1100, l, m));            // nothing left
  CHECK(!s.drag(60));                        // released: drags mean nothing
}

void testDragSameLevelIsNoChange() {
  VolumeSlider s;
  s.fromMac(live(50), 0);
  CHECK(s.grab(Y0));
  CHECK(!s.drag(Y0));  // back where it landed: already 50, not muted
  uint8_t l;
  bool m;
  CHECK(!s.takeSend(0, l, m));
}

// Review Focus 4: a Mac state that arrives while the finger is down, or within
// VOLUME_HOLD_MS of the lift, is parked and applied when the window ends.
void testHoldParksMacState() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  CHECK(s.grab(Y0));
  CHECK(s.drag(Y0 - UP10));              // 50
  s.fromMac(live(45), 1000);             // an echo of a level the drag already passed
  CHECK(s.view().level == 50);
  CHECK(s.release(1100));
  s.fromMac(live(48), 1200);             // still inside the window
  CHECK(s.view().level == 50);
  CHECK(!s.tick(1400));                  // 300 ms after the lift
  CHECK(s.view().level == 50);
  CHECK(s.tick(1700));                   // 600 ms: the parked state lands
  CHECK(s.view().level == 48);
  CHECK(!s.tick(1800));                  // once
  s.fromMac(live(30), 1900);             // outside the window: at once
  CHECK(s.view().level == 30);
}

// Review Focus 2: the link drops mid-drag. The card is NO MAC at once and the
// lift that follows sends nothing.
void testLinkDropMidDrag() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l;
  bool m;
  CHECK(s.grab(Y0));
  CHECK(s.drag(Y0 - UP10));
  s.fromMac(MacVolume{}, 500);
  CHECK(s.state() == VolState::NoMac);
  CHECK(!s.held());
  CHECK(!s.release(600));
  CHECK(!s.takeSend(600, l, m));
  CHECK(s.view().level == 0);
}

void testFixed() {
  VolumeSlider s;
  MacVolume hdmi = live(100);
  hdmi.canSet = false;
  strcpy(hdmi.name, "HDMI");
  s.fromMac(hdmi, 0);
  CHECK(s.state() == VolState::Fixed);
  CHECK(!s.grab(Y0));
  CHECK(!s.tap(BX, Y0, 0));               // the bar does nothing
  CHECK(s.view().level == 100);
  uint8_t l;
  bool m;
  CHECK(s.tap(VOL_MUTE_CX, VOL_MUTE_CY, 0));  // the number still mutes
  CHECK(s.view().muted);
  CHECK(s.takeSend(0, l, m) && l == 100 && m);

  VolumeSlider t;
  hdmi.canMute = false;
  t.fromMac(hdmi, 0);
  CHECK(!t.tap(VOL_MUTE_CX, VOL_MUTE_CY, 0));
}

// Review Focus 1: no output device. Nothing reacts, and the fill is empty, not 255.
void testNoOutput() {
  VolumeSlider s;
  MacVolume none{};
  none.known = true;
  none.level = VOLUME_NO_DEVICE;
  s.fromMac(none, 0);
  CHECK(s.state() == VolState::NoOutput);
  CHECK(!s.grab(Y0));
  CHECK(!s.tap(VOL_MUTE_CX, VOL_MUTE_CY, 0));
  CHECK(!s.tap(BX, Y0, 0));
  const VolumeView v = s.view();
  CHECK(v.level == 0 && !v.muted);
}

// A tap never changes the level: not on the bar, not on the name.
void testTapNeverSetsLevel() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l;
  bool m;
  CHECK(!s.tap(BX, Y0, 0));                // the bar
  CHECK(!s.tap(BX, VOL_BAR_Y + 4, 0));     // its top
  CHECK(!s.tap(5, Y0, 0));                 // the card's edge
  CHECK(s.view().level == 40 && !s.view().muted);
  CHECK(!s.takeSend(0, l, m));
}

void testSpeakerToggle() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l;
  bool m;
  CHECK(s.tap(VOL_MUTE_CX, VOL_MUTE_CY, 0));
  CHECK(s.view().muted && s.view().level == 40);
  CHECK(s.takeSend(0, l, m) && l == 40 && m);
  CHECK(s.tap(VOL_MUTE_CX + 20, VOL_MUTE_CY - 20, 100));
  CHECK(!s.view().muted);
  CHECK(s.takeSend(100, l, m) && l == 40 && !m);
}

void testChangeUnmutes() {
  VolumeSlider s;
  s.fromMac(live(40, true), 0);
  uint8_t l;
  bool m;
  CHECK(s.grab(Y0));
  CHECK(s.view().muted);                 // landing alone does not unmute
  CHECK(s.drag(Y0 - UP10));
  CHECK(s.view().level == 50);
  CHECK(!s.view().muted);
  CHECK(s.takeSend(0, l, m) && l == 50 && !m);
  CHECK(s.release(10));

  VolumeSlider t;
  t.fromMac(live(40, true), 0);
  CHECK(!t.tap(BX, Y0, 0));              // a tap on the bar does not unmute either
  CHECK(t.view().muted && t.view().level == 40);
}

// The button row: each glyph's centre hits its own zone, the zones tile the
// row under the bar, and a media tap leaves the slider alone.
void testMediaZones() {
  MediaKey k;
  CHECK(volumeMediaAt(VOL_PREV_CX, VOL_ROW_Y, k) && k == MediaKey::Previous);
  CHECK(volumeMediaAt(VOL_PLAY_CX, VOL_ROW_Y, k) && k == MediaKey::PlayPause);
  CHECK(volumeMediaAt(VOL_NEXT_CX, VOL_ROW_Y, k) && k == MediaKey::Next);
  CHECK(VOL_PLAY_CX == 120);                                 // centred on the screen
  CHECK(!volumeMediaAt(BX, Y0, k));                          // the bar
  CHECK(!volumeMediaAt(VOL_PLAY_CX, Y0, k));                 // above the row
  CHECK(VOL_PREV_ZONE.x + VOL_PREV_ZONE.w == VOL_PLAY_ZONE.x);
  CHECK(VOL_PLAY_ZONE.x + VOL_PLAY_ZONE.w == VOL_NEXT_ZONE.x);
  CHECK(VOL_NEXT_ZONE.x + VOL_NEXT_ZONE.w <= 240);
  CHECK(VOL_ROW_TOP >= VOL_BAR_Y + VOL_BAR_H);
  CHECK(VOL_ROW_TOP >= VOL_MUTE_Y + VOL_MUTE_H);              // never both mute and a media key
  CHECK(VOL_PLAY_ZONE.w >= 70 && VOL_PLAY_ZONE.h >= 64);
  CHECK(volumeInRow(VOL_ROW_Y) && volumeInRow(VOL_ROW_TOP));
  CHECK(!volumeInRow(Y0) && !volumeInRow(VOL_BAR_Y + VOL_BAR_H - 1));
  VolumeSlider s;
  s.fromMac(live(40), 0);
  CHECK(!s.tap(VOL_PLAY_CX, VOL_ROW_Y, 0));                // the slider ignores it
  CHECK(s.view().level == 40 && !s.view().muted && s.view().pressed == -1);
  CHECK(s.view().playing == -1);                             // the Mac has not said
  MacVolume p = live(40);
  p.playKnown = p.playing = true;
  s.fromMac(p, 0);
  CHECK(s.view().playing == 1);
  p.playing = false;
  s.fromMac(p, 0);
  CHECK(s.view().playing == 0);
}

}  // namespace

void testPillHit() {
  CHECK(volumeInPill(VOL_PILL_X, VOL_PILL_Y));
  CHECK(volumeInPill(VOL_PILL_X + VOL_PILL_W - 1, VOL_PILL_Y + VOL_PILL_H - 1));
  CHECK(!volumeInPill(VOL_PILL_X - 1, VOL_PILL_Y + 10));
  CHECK(!volumeInPill(VOL_PILL_X + VOL_PILL_W, VOL_PILL_Y + 10));
  CHECK(!volumeInPill(VOL_PILL_X + 10, VOL_PILL_Y - 1));
  CHECK(!volumeInPill(VOL_PILL_X + 10, VOL_PILL_Y + VOL_PILL_H));
  // A drag that never grabbed does nothing: drag and release are no-ops.
  VolumeSlider s;
  MacVolume m{};
  m.known = true;
  m.level = 40;
  m.canSet = m.canMute = true;
  s.fromMac(m, 0);
  CHECK(!s.held());
  CHECK(!s.drag(Y0 + UP10));
  CHECK(!s.release(10));
  uint8_t l = 0;
  bool mu = false;
  CHECK(!s.takeSend(100, l, mu));
}

int main() {
  testPillHit();
  testGeometry();
  testNoMacByDefault();
  testLiveApplies();
  testDragThrottle();
  testDragSameLevelIsNoChange();
  testHoldParksMacState();
  testLinkDropMidDrag();
  testFixed();
  testNoOutput();
  testTapNeverSetsLevel();
  testSpeakerToggle();
  testChangeUnmutes();
  testMediaZones();
  return checksDone("volume_slider_test");
}
