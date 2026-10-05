#include <cstring>

#include "check.h"
#include "dev_editor.h"

namespace {

const DeviceSettings DEF{160, 15, 0, 5, 2};

// One tap, with `saved` as what is stored (the panel's starting point).
DeviceSettings press(DeviceSettings s, EditAction a, uint8_t row, int times = 1, DeviceSettings saved = DEF) {
  for (int i = 0; i < times; i++) devEditorApply(s, EditHit{a, row}, DEF, saved);
  return s;
}

void centre(const EditRect &r, int16_t &x, int16_t &y) {
  x = (int16_t)(r.x + r.w / 2);
  y = (int16_t)(r.y + r.h / 2);
}

void testBrightness() {
  CHECK(press(DEF, EditAction::Inc, 0).backlight == 200);
  CHECK(press(DEF, EditAction::Inc, 0, 3).backlight == 255);  // stops at the top
  CHECK(press(DeviceSettings{10, 15, 0, 5, 2}, EditAction::Dec, 0).backlight == 10);  // and the bottom: never dark
  // A value set from the portal that is not a preset snaps to the neighbour.
  CHECK(press(DeviceSettings{100, 15, 0, 5, 2}, EditAction::Inc, 0).backlight == 120);
  CHECK(press(DeviceSettings{100, 15, 0, 5, 2}, EditAction::Dec, 0).backlight == 80);
}

void testStepsNeverReachOff() {
  CHECK(press(DEF, EditAction::Dec, 1, 5).sleepMin == 1);  // 15 -> 10 -> 5 -> 1 -> stays: off is the switch's
  CHECK(press(DEF, EditAction::Inc, 1, 20).sleepMin == 240);
  CHECK(press(DeviceSettings{160, 15, 30, 5, 2}, EditAction::Dec, 2, 9).rotateSec == 5);
  CHECK(press(DeviceSettings{160, 15, 30, 5, 2}, EditAction::Inc, 2, 9).rotateSec == 120);
  CHECK(press(DeviceSettings{160, 2, 0, 5, 2}, EditAction::Inc, 1).sleepMin == 5);  // between presets
  CHECK(press(DeviceSettings{160, 2, 0, 5, 2}, EditAction::Dec, 1).sleepMin == 1);
  CHECK(press(DEF, EditAction::Dec, 3, 5).sound == 1);  // MED -> LOW -> stays
  CHECK(press(DEF, EditAction::Inc, 3, 5).sound == 3);  // MED -> HIGH -> stays
}

void testSwitch() {
  CHECK(!devEditorHasSwitch(0) && devEditorHasSwitch(1) && devEditorHasSwitch(2) && devEditorHasSwitch(3));
  CHECK(!devEditorHasSwitch(4) && !devEditorHasSwitch(-1));
  CHECK(devEditorRowOn(1, DEF) && !devEditorRowOn(2, DEF) && devEditorRowOn(3, DEF));
  CHECK(devEditorRowOn(0, DeviceSettings{10, 0, 0, 2, 0}) && !devEditorRowOn(3, DeviceSettings{10, 0, 0, 2, 0}));

  // Off, then the steppers do nothing until it is back on.
  const DeviceSettings off = press(DEF, EditAction::Toggle, 1);
  CHECK(off.sleepMin == 0 && off.backlight == 160 && off.pollSec == 5 && off.sound == 2);
  CHECK(press(off, EditAction::Inc, 1).sleepMin == 0);
  CHECK(press(off, EditAction::Dec, 1).sleepMin == 0);

  // Back on: the stored value, else the default, else the row's preset.
  CHECK(press(off, EditAction::Toggle, 1, 1, DeviceSettings{160, 60, 0, 5, 2}).sleepMin == 60);
  CHECK(press(off, EditAction::Toggle, 1, 1, DeviceSettings{160, 0, 0, 5, 2}).sleepMin == 15);  // DEF
  CHECK(press(DEF, EditAction::Toggle, 2).rotateSec == 10);  // stored and default both off
  CHECK(press(DEF, EditAction::Toggle, 2, 2).rotateSec == 0);

  // Rows without a switch, and rows that do not exist, ignore Toggle.
  CHECK(press(DEF, EditAction::Toggle, 0) == DEF);
  CHECK(press(DEF, EditAction::Toggle, 9) == DEF);
}

// SOUND is a switch row like SLEEP and ADVANCE: OFF is the switch's, the
// steppers move between LOW, MED and HIGH.
void testSound() {
  const DeviceSettings muted = press(DEF, EditAction::Toggle, 3);
  CHECK(muted.sound == 0 && muted.pollSec == 5 && muted.backlight == 160);
  CHECK(press(muted, EditAction::Inc, 3) == muted);  // inert while off
  CHECK(press(muted, EditAction::Dec, 3) == muted);

  // Back on: the stored level, else the default, else MED.
  CHECK(press(muted, EditAction::Toggle, 3, 1, DeviceSettings{160, 15, 0, 5, 3}).sound == 3);
  CHECK(press(muted, EditAction::Toggle, 3, 1, DeviceSettings{160, 15, 0, 5, 0}).sound == 2);  // DEF
  const DeviceSettings quiet{160, 15, 0, 5, 0};  // stored and default both off
  DeviceSettings s = muted;
  devEditorApply(s, EditHit{EditAction::Toggle, 3}, quiet, quiet);
  CHECK(s.sound == 2);

  CHECK(press(DeviceSettings{160, 15, 0, 5, 1}, EditAction::Inc, 3).sound == 2);
  CHECK(press(DeviceSettings{160, 15, 0, 5, 3}, EditAction::Dec, 3).sound == 2);
}

void testHit() {
  int16_t x, y;
  for (int r = 0; r < DEV_EDIT_ROWS; r++) {
    centre(editorLead(r), x, y);
    const EditHit h = devEditorHit(x, y);
    if (devEditorHasSwitch(r)) CHECK(h.action == EditAction::Toggle && h.row == r);
    else CHECK(h.action == EditAction::None);
    centre(editorMinus(r), x, y);
    CHECK(devEditorHit(x, y).action == EditAction::Dec && devEditorHit(x, y).row == r);
    centre(editorPlus(r), x, y);
    CHECK(devEditorHit(x, y).action == EditAction::Inc && devEditorHit(x, y).row == r);
  }
  centre(editorDoneBtn(), x, y);
  CHECK(devEditorHit(x, y).action == EditAction::Done);
  centre(editorResetBtn(), x, y);
  CHECK(devEditorHit(x, y).action == EditAction::Reset);
}

void testResetAndNoOps() {
  // RESET restores the defaults, except WiFi refresh: it has no row here (the
  // portal and the Mac edit it), so the panel must not change it unseen.
  DeviceSettings r = press(DeviceSettings{255, 0, 30, 60, 0}, EditAction::Reset, 0);
  CHECK(r.pollSec == 60);
  r.pollSec = DEF.pollSec;
  CHECK(r == DEF);
  const DeviceSettings s{200, 30, 0, 10, 1};
  CHECK(press(s, EditAction::Done, 0) == s);
  CHECK(press(s, EditAction::None, 0) == s);
  CHECK(press(s, EditAction::Inc, 9) == s);
}

// Every value any row can reach is inside what the cube accepts, and no row
// touches WiFi refresh.
void testReachableValuesAreValid() {
  const EditAction moves[] = {EditAction::Inc, EditAction::Dec, EditAction::Toggle};
  for (int row = 0; row < DEV_EDIT_ROWS; row++) {
    for (EditAction first : moves) {
      for (EditAction then : moves) {
        DeviceSettings t = press(DEF, first, (uint8_t)row, 12);
        t = press(t, then, (uint8_t)row, 3);
        CHECK(deviceClamp(t) == t);
        CHECK(t.pollSec == DEF.pollSec);
      }
    }
  }
}

void testValues() {
  char b[16];
  devEditorValue(0, DeviceSettings{255, 0, 0, 5, 2}, b, sizeof(b));
  CHECK(!strcmp(b, "100%"));
  devEditorValue(0, DeviceSettings{10, 0, 0, 5, 2}, b, sizeof(b));
  CHECK(!strcmp(b, "4%"));
  devEditorValue(1, DeviceSettings{160, 0, 0, 5, 2}, b, sizeof(b));
  CHECK(!strcmp(b, "OFF"));
  devEditorValue(1, DEF, b, sizeof(b));
  CHECK(!strcmp(b, "15 min"));
  devEditorValue(2, DEF, b, sizeof(b));
  CHECK(!strcmp(b, "OFF"));
  devEditorValue(2, DeviceSettings{160, 15, 10, 5, 2}, b, sizeof(b));
  CHECK(!strcmp(b, "10 s"));
  devEditorValue(3, DEF, b, sizeof(b));
  CHECK(!strcmp(b, "MED"));
  devEditorValue(3, DeviceSettings{160, 15, 0, 5, 1}, b, sizeof(b));
  CHECK(!strcmp(b, "LOW"));
  devEditorValue(3, DeviceSettings{160, 15, 0, 5, 3}, b, sizeof(b));
  CHECK(!strcmp(b, "HIGH"));
  devEditorValue(3, DeviceSettings{160, 15, 0, 5, 0}, b, sizeof(b));
  CHECK(!strcmp(b, "OFF"));
  CHECK(!strcmp(devEditorLabel(3), "SOUND"));
  CHECK(!strcmp(devEditorLabel(4), ""));
}

}  // namespace

int main() {
  testBrightness();
  testStepsNeverReachOff();
  testSwitch();
  testSound();
  testHit();
  testResetAndNoOps();
  testReachableValuesAreValid();
  testValues();
  return checksDone("dev_editor_test");
}
