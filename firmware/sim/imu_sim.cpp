// Desktop stand-in for imu.cpp: there is no accelerometer on a laptop, so the
// cube's pose comes from CUBE_ORIENT. 0 (default) = upright, 2 = upside down.
// `make run` then flips about a second after start, exactly as a cube powered
// up upside down would.
#include <Arduino.h>
#include "../src/board_pins.h"
#include "../src/imu.h"

namespace {
bool flipped() {
  const char *e = getenv("CUBE_ORIENT");
  return e && atoi(e) == 2;
}
}  // namespace

bool imuBegin() {
  Serial.printf("[imu] simulated, CUBE_ORIENT=%d\n", flipped() ? 2 : 0);
  return true;
}

bool imuReadAccel(float &ax, float &ay, float &az) {
  ax = 0.0f;
  ay = (flipped() ? -1.0f : 1.0f) * (float)IMU_UP_SIGN;  // main.cpp multiplies by IMU_UP_SIGN again
  az = 0.0f;
  return true;
}
