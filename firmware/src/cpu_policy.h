#pragma once
// The CPU clock while the screen is on. Pure (no Arduino) so it is host-tested.
//
// Most of an awake cube's time is spent waiting: one small frame a second for
// the age counter, a payload every few seconds. At 80 MHz the chip draws ~12 mA
// less than at 240 MHz while it waits, and APB (SPI, I2C, the backlight PWM)
// stays at 80 MHz at either clock, so nothing but the CPU slows down. A static
// frame takes ~3x longer to compose at 80 MHz, a few ms, which nobody sees. A
// moving ring or a finger on the glass is different: frames must keep up, so
// those run at full speed, and for CPU_HOLD_MS after, which covers the frame
// that answers a tap (a double tap acts 350 ms after the finger lifts) and
// keeps a run of animations from switching the clock back and forth between
// frames. With the screen off, main.cpp drops to the slow clock regardless.

#include <stdint.h>

constexpr uint32_t CPU_MHZ_FAST = 240;  // matches board_build.f_cpu
constexpr uint32_t CPU_MHZ_SLOW = 80;   // lowest clock WiFi/BLE still run at
constexpr uint32_t CPU_HOLD_MS = 1000;

class CpuPolicy {
 public:
  // Boot counts as busy, so the cube starts at full speed.
  void begin(uint32_t now) { last_ = now; }

  // `busy`: a finger is down or a frame is part-way through an animation.
  // Returns the clock to run at.
  uint32_t update(uint32_t now, bool busy) {
    if (busy) last_ = now;
    // A stamp newer than `now` is zero elapsed, not ~49 days (as in idle_sleep.h).
    const int32_t d = (int32_t)(now - last_);
    return d < (int32_t)CPU_HOLD_MS ? CPU_MHZ_FAST : CPU_MHZ_SLOW;
  }

 private:
  uint32_t last_ = 0;
};
