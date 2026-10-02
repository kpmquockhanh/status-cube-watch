#include "check.h"
#include "touch_map.h"

namespace {

void testUprightIsIdentity() {
  int16_t x = 12, y = 34;
  touchToScreen(0, x, y);
  CHECK(x == 12);
  CHECK(y == 34);
}

void testFlippedMirrorsBothAxes() {
  int16_t x = 0, y = 0;
  touchToScreen(2, x, y);
  CHECK(x == LCD_WIDTH - 1);
  CHECK(y == LCD_HEIGHT - 1);

  x = LCD_WIDTH - 1;
  y = LCD_HEIGHT - 1;
  touchToScreen(2, x, y);
  CHECK(x == 0);
  CHECK(y == 0);

  x = 100;
  y = 40;
  touchToScreen(2, x, y);
  CHECK(x == LCD_WIDTH - 1 - 100);
  CHECK(y == LCD_HEIGHT - 1 - 40);
}

void testFlippedIsItsOwnInverse() {
  for (int16_t sx = 0; sx < LCD_WIDTH; sx += 17) {
    for (int16_t sy = 0; sy < LCD_HEIGHT; sy += 19) {
      int16_t x = sx, y = sy;
      touchToScreen(2, x, y);
      touchToScreen(2, x, y);
      CHECK(x == sx);
      CHECK(y == sy);
    }
  }
}

void testOutOfRangeIsClamped() {  // the controller can report a hair past the glass
  int16_t x = 300, y = -5;
  touchToScreen(2, x, y);
  CHECK(x == 0);
  CHECK(y == LCD_HEIGHT - 1);
}

}  // namespace

int main() {
  testUprightIsIdentity();
  testFlippedMirrorsBothAxes();
  testFlippedIsItsOwnInverse();
  testOutOfRangeIsClamped();
  return checksDone("touch_map_test");
}
