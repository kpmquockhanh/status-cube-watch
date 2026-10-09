#include <cstring>

#include "check.h"
#include "volume_slider.h"

namespace {

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
  CHECK(volumeLevelAt(147) == 50);
  CHECK(volumeLevelAt(55) == 99);
  CHECK(volumeLevelAt(239) == 1);
  CHECK(volumeLevelAt(VOL_TRAVEL_TOP) == 100);
  CHECK(volumeLevelAt(VOL_TRAVEL_BOTTOM) == 0);
  CHECK(volumeLevelAt(0) == 100);    // above the travel: clamped
  CHECK(volumeLevelAt(280) == 0);    // below it
  CHECK(volumeFillTop(100) == VOL_PILL_Y);
  CHECK(volumeFillTop(0) == VOL_PILL_Y + VOL_PILL_H);
  CHECK(volumeFillTop(50) == 147);
  // The fill edge sits on the finger: reading it back gives the level it was drawn from.
  for (int L = 1; L <= 99; L++) CHECK(volumeLevelAt(volumeFillTop((float)L)) == L);
  // Higher level, higher fill (smaller y), never the other way round.
  int prev = volumeFillTop(0.0f);
  for (int i = 1; i <= 400; i++) {
    const int y = volumeFillTop(i * 0.25f);
    CHECK(y <= prev);
    prev = y;
  }
  CHECK(VOL_PILL_RECT.contains(VOL_SPK_CX, VOL_SPK_CY));
  CHECK(VOL_SPEAKER_ZONE.contains(VOL_SPK_CX, VOL_SPK_CY));
  CHECK(!VOL_SPEAKER_ZONE.contains(VOL_SPK_CX, 147));
}

void testNoMacByDefault() {
  VolumeSlider s;
  CHECK(s.state() == VolState::NoMac);
  CHECK(!s.grab());
  CHECK(!s.tap(120, 147, 0));
  CHECK(!s.tap(VOL_SPK_CX, VOL_SPK_CY, 0));
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
  CHECK(s.grab());
  CHECK(s.held() && s.view().tracking);
  CHECK(s.drag(147));
  CHECK(s.takeSend(1000, l, m) && l == 50 && !m);
  CHECK(s.drag(100));
  CHECK(!s.takeSend(1020, l, m));            // 20 ms after the last: wait
  CHECK(s.takeSend(1050, l, m) && l == volumeLevelAt(100));
  CHECK(s.drag(80));
  CHECK(s.release(1070));
  CHECK(!s.held());
  CHECK(s.takeSend(1070, l, m) && l == volumeLevelAt(80));  // final value, unthrottled
  CHECK(!s.takeSend(1100, l, m));            // nothing left
  CHECK(!s.drag(60));                        // released: drags mean nothing
}

void testDragSameLevelIsNoChange() {
  VolumeSlider s;
  s.fromMac(live(50), 0);
  CHECK(s.grab());
  CHECK(!s.drag(147));  // already 50, not muted
  uint8_t l;
  bool m;
  CHECK(!s.takeSend(0, l, m));
}

// Review Focus 4: a Mac state that arrives while the finger is down, or within
// VOLUME_HOLD_MS of the lift, is parked and applied when the window ends.
void testHoldParksMacState() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  CHECK(s.grab());
  CHECK(s.drag(147));                    // 50
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
  CHECK(s.grab());
  CHECK(s.drag(100));
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
  CHECK(!s.grab());
  CHECK(!s.tap(120, 147, 0));              // no tap-jump
  CHECK(s.view().level == 100);
  uint8_t l;
  bool m;
  CHECK(s.tap(VOL_SPK_CX, VOL_SPK_CY, 0));  // the speaker still mutes
  CHECK(s.view().muted);
  CHECK(s.takeSend(0, l, m) && l == 100 && m);

  VolumeSlider t;
  hdmi.canMute = false;
  t.fromMac(hdmi, 0);
  CHECK(!t.tap(VOL_SPK_CX, VOL_SPK_CY, 0));
}

// Review Focus 1: no output device. Nothing reacts, and the fill is empty, not 255.
void testNoOutput() {
  VolumeSlider s;
  MacVolume none{};
  none.known = true;
  none.level = VOLUME_NO_DEVICE;
  s.fromMac(none, 0);
  CHECK(s.state() == VolState::NoOutput);
  CHECK(!s.grab());
  CHECK(!s.tap(VOL_SPK_CX, VOL_SPK_CY, 0));
  CHECK(!s.tap(120, 100, 0));
  const VolumeView v = s.view();
  CHECK(v.level == 0 && !v.muted);
}

void testTapJump() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l;
  bool m;
  CHECK(s.tap(120, 147, 0));
  CHECK(s.view().level == 50);
  CHECK(s.takeSend(0, l, m) && l == 50 && !m);
  CHECK(!s.tap(120, 147, 10));  // the same height again: nothing to send
  CHECK(!s.takeSend(10, l, m));
  CHECK(!s.tap(5, 147, 20));    // left of the pill
  CHECK(!s.tap(120, 20, 20));   // above it (the top bar)
}

void testSpeakerToggle() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l;
  bool m;
  CHECK(s.tap(VOL_SPK_CX, VOL_SPK_CY, 0));
  CHECK(s.view().muted && s.view().level == 40);
  CHECK(s.takeSend(0, l, m) && l == 40 && m);
  CHECK(s.tap(VOL_SPK_CX + 20, VOL_SPK_CY - 20, 100));
  CHECK(!s.view().muted);
  CHECK(s.takeSend(100, l, m) && l == 40 && !m);
}

void testChangeUnmutes() {
  VolumeSlider s;
  s.fromMac(live(40, true), 0);
  uint8_t l;
  bool m;
  CHECK(s.grab());
  CHECK(s.drag(volumeFillTop(40)));  // the same level as before: unmuting is still a change
  CHECK(s.view().level == 40);
  CHECK(!s.view().muted);
  CHECK(s.takeSend(0, l, m) && !m);
  CHECK(s.release(10));

  VolumeSlider t;
  t.fromMac(live(40, true), 0);
  CHECK(t.tap(120, 147, 0));
  CHECK(!t.view().muted && t.view().level == 50);
}

}  // namespace

int main() {
  testGeometry();
  testNoMacByDefault();
  testLiveApplies();
  testDragThrottle();
  testDragSameLevelIsNoChange();
  testHoldParksMacState();
  testLinkDropMidDrag();
  testFixed();
  testNoOutput();
  testTapJump();
  testSpeakerToggle();
  testChangeUnmutes();
  return checksDone("volume_slider_test");
}
