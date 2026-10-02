#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// The bridge sends fully formatted strings, so the firmware never does any
// number formatting or unit conversion -- redesigning the dashboard means
// editing bridge/cards.mjs and reloading, not reflashing the board.

constexpr uint8_t MAX_CARDS = 8;

// `gauge` is the one number the firmware does read, because a ring cannot be
// drawn from a pre-formatted string. It is a whole percentage; the two
// sentinels below cover the cases where there is no arc to fill.
constexpr int8_t GAUGE_NONE = -2;   // key absent: draw the plain text card
constexpr int8_t GAUGE_BLANK = -1;  // no reading: draw the empty track only

enum Accent : uint8_t { ACC_INK, ACC_ACCENT, ACC_BLUE, ACC_GREEN, ACC_AMBER, ACC_VIOLET, ACC_RED };

struct Card {
  char title[24];
  char value[24];
  char sub1[40];
  char sub2[40];
  uint8_t color;
  int8_t gauge;
};

struct Payload {
  Card cards[MAX_CARDS];
  uint8_t nCards;
  char source[12];
  uint32_t ts;
  bool estimated;
  bool valid;
};

Accent accentFromName(const char *name);

// Maps the bridge's JSON onto a Payload. Shared by the firmware and the
// desktop simulator so both exercise exactly the same parsing and defaults.
// Returns false and writes a short reason into `err` if the payload is unusable.
bool payloadFromJson(const JsonDocument &doc, Payload &out, char *err, size_t errLen);
