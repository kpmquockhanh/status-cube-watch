#pragma once
// What the cards read, folded into one number, for screen sleep: main.cpp
// counts a payload as activity (idle_sleep.h) only when its key differs from
// the last one's. "A payload arrived" is not enough: the Mac resends the same
// payload every 5 s and the bridge rebuilds it for every WiFi poll, so the
// screen would stay on for as long as the link is up. Pure, so it is
// host-tested.
//
// In the key: the card count, titles, every gauge, the mail badge, and a text
// card's value (spend, tokens, model), which move only when Claude is used.
// Left out: a ring card's value, which is the reset countdown, and every
// sub-line and legend row, which carry countdowns, projections and "active 3m
// ago". All of those change with the clock alone and would keep the screen
// awake. Accents follow the gauges, so they add nothing.
//
// The Agents part: how many tiles, each one's id and kind, which prompt is
// open and whether the bridge dropped it. A new prompt, an agent starting or
// finishing, or one turning from running to waiting is something to look at.
// Left out: a tile's status line, pose, context and unseen flag, which move
// as an agent works (and the pose follows the clock), and the prompt's text,
// which its id already stands for.

#include <stddef.h>
#include <stdint.h>

#include "payload.h"

namespace readings_detail {
inline uint32_t mix(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }  // FNV-1a

inline uint32_t mixStr(uint32_t h, const char *s, size_t cap) {
  for (size_t i = 0; i < cap && s[i]; i++) h = mix(h, (uint8_t)s[i]);
  return mix(h, 0);  // so "AB" + "C" and "A" + "BC" differ
}
}  // namespace readings_detail

inline uint32_t readingsKey(const Payload &p) {
  using namespace readings_detail;
  uint32_t h = 2166136261u;
  h = mix(h, p.valid);
  h = mix(h, p.nCards);
  for (uint8_t i = 0; i < p.nCards && i < MAX_CARDS; i++) {
    const Card &c = p.cards[i];
    h = mixStr(h, c.title, sizeof(c.title));
    h = mix(h, (uint8_t)c.gauge);
    h = mix(h, (uint8_t)c.gauge2);
    if (c.gauge == GAUGE_NONE) h = mixStr(h, c.value, sizeof(c.value));
    h = mixStr(h, c.mail, sizeof(c.mail));
  }
  const AgentsData &a = p.agents;
  h = mix(h, a.present);
  h = mix(h, a.nTiles);
  for (uint8_t i = 0; i < a.nTiles && i < AGENT_TILES; i++) {
    h = mixStr(h, a.tiles[i].id, sizeof(a.tiles[i].id));
    h = mix(h, (uint8_t)a.tiles[i].kind);
  }
  h = mixStr(h, a.hasPrompt ? a.prompt.id : "", sizeof(a.prompt.id));
  h = mix(h, a.promptDropped);
  return h;
}
