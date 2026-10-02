#pragma once
// Just enough of the Arduino core for the desktop simulator.
//
// This is deliberately tiny: the firmware sources only reach for millis(),
// delay(), Serial logging and the clamp helpers. Everything here is an inline
// function rather than a macro so it cannot collide with <algorithm> or with
// LovyanGFX's own templates.

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

inline uint32_t millis() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline int max(int a, int b) { return a > b ? a : b; }
inline int min(int a, int b) { return a < b ? a : b; }
inline int constrain(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Serial goes to the terminal, so the simulator prints the same boot and
// fetch diagnostics you would see over USB.
struct SerialShim {
  void begin(unsigned long) {}
  void print(char c) { fputc(c, stdout); fflush(stdout); }
  void print(const char *s) { fputs(s, stdout); fflush(stdout); }
  void println() { fputc('\n', stdout); }
  void println(const char *s) { printf("%s\n", s); }
  int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
    return n;
  }
};
inline SerialShim Serial;
