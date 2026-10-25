#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// The bridge sends fully formatted strings, so the firmware never does any
// number formatting or unit conversion -- redesigning the dashboard means
// editing bridge/internal/cards and rebuilding the bridge, not reflashing the board.

constexpr uint8_t MAX_CARDS = 8;

// `gauge` is the one number the firmware does read, because a ring cannot be
// drawn from a pre-formatted string. It is a whole percentage; the two
// sentinels below cover the cases where there is no arc to fill.
constexpr int8_t GAUGE_NONE = -2;   // key absent: draw the plain text card
constexpr int8_t GAUGE_BLANK = -1;  // no reading: draw the empty track only

enum Accent : uint8_t { ACC_INK, ACC_ACCENT, ACC_BLUE, ACC_GREEN, ACC_AMBER, ACC_VIOLET, ACC_RED };

// One legend line under a dual-ring card ("5H  14%  4h 09m"), pre-formatted.
struct LegendRow {
  char key[6];
  char pct[8];
  char reset[12];
};

struct Card {
  char title[24];
  char value[24];
  char sub1[40];
  char sub2[40];
  uint8_t color;
  int8_t gauge;
  // Dual-ring card (`g2` present): `gauge`/`color` are the outer ring, these
  // the inner one. `gauge2` is GAUGE_NONE on every other card.
  int8_t gauge2;
  uint8_t color2;
  uint8_t nRows;
  LegendRow rows[2];
  // Unread-mail badge in the gap of the rings; empty = no badge.
  char mail[8];
  uint8_t mailColor;
  // True for the Agents card (the bridge's `a` array): the tiles and prompt
  // live in Payload::agents, not here.
  bool agents;
};

// The Agents card. Field sizes are the bridge's maxima (agents.go) plus a NUL.
constexpr uint8_t AGENT_TILES = 8, PROMPT_OPTS = 6;

enum class AgentKind : uint8_t { Run, Wait, Idle, Err, Exit, Start };  // unknown -> Idle
enum class PromptKind : uint8_t { Permission, Question, Input };       // unknown -> Input (no buttons)

struct AgentTile {
  char id[16];
  char name[17];
  char line[17];
  char pose[9];
  AgentKind kind;
  uint8_t ctx;   // context used, 0-100
  bool unseen;   // finished a turn nobody looked at
};

struct PromptOpt {
  char key;  // '1'..'9'
  char label[41];
};

struct AgentPrompt {
  char agent[16];
  char id[16];
  PromptKind kind;
  char title[41];
  char detail[121];
  char context[41];
  uint8_t nOpts;
  PromptOpt opts[PROMPT_OPTS];
  bool notInTmux;  // `x` = "tmux": cannot be answered from here
  bool cut;        // the bridge's budget dropped the detail
};

struct AgentsData {
  bool present;
  uint8_t card;  // index of the Agents card in Payload::cards
  uint8_t nTiles;
  AgentTile tiles[AGENT_TILES];
  bool hasPrompt;       // `q` arrived (full variant only) and passed validation
  bool promptDropped;   // `qc`: the budget dropped it
  AgentPrompt prompt;
};

// Roughly 3 KB: never a local variable (loop and fetch task stacks are 8 KB).
struct Payload {
  Card cards[MAX_CARDS];
  uint8_t nCards;
  bool valid;
  AgentsData agents;
};

// The tile with this agent id, or nullptr.
const AgentTile *agentsTile(const AgentsData &a, const char *id);

Accent accentFromName(const char *name);

// Maps the bridge's JSON onto a Payload. Shared by the firmware and the
// desktop simulator so both exercise exactly the same parsing and defaults.
// Clears and fills `out` in place (it is too big for a stack); on failure
// `out.valid` is false. Returns false and writes a short reason into `err` if the payload is unusable.
bool payloadFromJson(const JsonDocument &doc, Payload &out, char *err, size_t errLen);
