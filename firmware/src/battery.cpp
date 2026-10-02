#include "battery.h"

#include <Arduino.h>

#include "board_pins.h"

namespace {
constexpr uint32_t SAMPLE_EVERY_MS = 5000;
constexpr int SAMPLES = 16;
constexpr int DIVIDER = 3;  // R3 200K over R7 100K: VBAT = VADC x 3

BatteryFilter g_filter;
uint32_t g_lastSample = 0;

void sample() {
  // Median rather than mean: the screen refresh and WiFi bursts pull the rail
  // down in short spikes, which a median ignores and a mean does not.
  uint32_t v[SAMPLES];
  for (int i = 0; i < SAMPLES; i++) {
    v[i] = analogReadMilliVolts(PIN_BAT_ADC);
    delayMicroseconds(200);
  }
  for (int i = 1; i < SAMPLES; i++) {  // insertion sort, 16 items
    const uint32_t x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) v[j + 1] = v[j--];
    v[j + 1] = x;
  }
  const uint32_t mv = (v[SAMPLES / 2 - 1] + v[SAMPLES / 2]) / 2 * DIVIDER;
  g_filter.update(mv);
  // Handy when checking a new board: what the divider really reads.
  Serial.printf("[bat] raw %lu..%lu mV x3, shown %lu mV -> %u%%%s\n", (unsigned long)v[0] * DIVIDER,
                (unsigned long)v[SAMPLES - 1] * DIVIDER, (unsigned long)g_filter.mv(), g_filter.view().pct,
                g_filter.view().onUsb ? " (no cell, USB)" : "");
}
}  // namespace

void batteryBegin() {
  sample();
  g_lastSample = millis();
}

void batteryUpdate(uint32_t nowMs) {
  if (nowMs - g_lastSample < SAMPLE_EVERY_MS) return;
  g_lastSample = nowMs;
  sample();
}

BatteryView batteryView() { return g_filter.view(); }
