#include <cstring>

#include "check.h"
#include "dev_editor.h"

int main() {
  const DeviceSettings defaults{160, 15, 0, 5};
  DeviceSettings s = defaults;
  const EditHit inc0{EditAction::Inc, 0}, dec0{EditAction::Dec, 0};

  devEditorApply(s, inc0, defaults);
  CHECK(s.backlight == 200);
  devEditorApply(s, inc0, defaults);
  devEditorApply(s, inc0, defaults);
  CHECK(s.backlight == 255);  // stops at the top
  s.backlight = 10;
  devEditorApply(s, dec0, defaults);
  CHECK(s.backlight == 10);   // and the bottom: never dark

  // A value set from the portal that is not a preset snaps to the neighbour.
  s.backlight = 100;
  devEditorApply(s, inc0, defaults);
  CHECK(s.backlight == 120);
  s.backlight = 100;
  devEditorApply(s, dec0, defaults);
  CHECK(s.backlight == 80);

  s.sleepMin = 15;
  devEditorApply(s, EditHit{EditAction::Dec, 1}, defaults);
  devEditorApply(s, EditHit{EditAction::Dec, 1}, defaults);
  devEditorApply(s, EditHit{EditAction::Dec, 1}, defaults);
  devEditorApply(s, EditHit{EditAction::Dec, 1}, defaults);
  devEditorApply(s, EditHit{EditAction::Dec, 1}, defaults);
  CHECK(s.sleepMin == 0);  // 15 -> 10 -> 5 -> 1 -> 0 -> stays

  s.pollSec = 5;
  devEditorApply(s, EditHit{EditAction::Dec, 3}, defaults);
  devEditorApply(s, EditHit{EditAction::Dec, 3}, defaults);
  CHECK(s.pollSec == 2);  // the firmware's floor

  devEditorApply(s, EditHit{EditAction::Reset, 0}, defaults);
  CHECK(s.backlight == 160 && s.sleepMin == 15 && s.rotateSec == 0 && s.pollSec == 5);
  const DeviceSettings before = s;
  devEditorApply(s, EditHit{EditAction::Done, 0}, defaults);
  devEditorApply(s, EditHit{EditAction::Inc, 9}, defaults);
  CHECK(s.backlight == before.backlight && s.sleepMin == before.sleepMin);

  // Every preset any row can reach is inside what the cube accepts.
  for (int row = 0; row < DEV_EDIT_ROWS; row++) {
    DeviceSettings t = defaults;
    for (int i = 0; i < 12; i++) devEditorApply(t, EditHit{EditAction::Inc, (uint8_t)row}, defaults);
    CHECK(deviceClamp(t).backlight == t.backlight && deviceClamp(t).sleepMin == t.sleepMin);
    CHECK(deviceClamp(t).rotateSec == t.rotateSec && deviceClamp(t).pollSec == t.pollSec);
    for (int i = 0; i < 12; i++) devEditorApply(t, EditHit{EditAction::Dec, (uint8_t)row}, defaults);
    CHECK(deviceClamp(t).backlight == t.backlight && deviceClamp(t).pollSec == t.pollSec);
  }

  char b[16];
  devEditorValue(0, DeviceSettings{255, 0, 0, 5}, b, sizeof(b));
  CHECK(!strcmp(b, "100%"));
  devEditorValue(0, DeviceSettings{10, 0, 0, 5}, b, sizeof(b));
  CHECK(!strcmp(b, "4%"));
  devEditorValue(1, DeviceSettings{160, 0, 0, 5}, b, sizeof(b));
  CHECK(!strcmp(b, "NEVER"));
  devEditorValue(1, DeviceSettings{160, 15, 0, 5}, b, sizeof(b));
  CHECK(!strcmp(b, "15 min"));
  devEditorValue(2, DeviceSettings{160, 15, 0, 5}, b, sizeof(b));
  CHECK(!strcmp(b, "OFF"));
  devEditorValue(3, DeviceSettings{160, 15, 0, 5}, b, sizeof(b));
  CHECK(!strcmp(b, "5 s"));
  CHECK(!strcmp(devEditorLabel(4), ""));
  return checksDone("dev_editor_test");
}
