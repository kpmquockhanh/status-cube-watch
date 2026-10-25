#pragma once
// When the cube alerts (wakes the screen, jumps to the prompt, beeps) because
// an agent started waiting. Pure (no Arduino) so it is host-tested.
// One alert per new prompt: the Mac resends the same payload every 5 s, so an
// alert fires only when a prompt id is new (the last 8 are remembered) or a
// tile turns Wait that was not Wait in the previous payload. An alert that is
// held back (`blocked`: an editor, a ride, the unlock prompt owns the screen)
// stays pending until it can be taken, and is dropped if the agent has been
// answered meanwhile. Copies of ids are kept, never pointers into a payload.

#include <stddef.h>
#include <string.h>

#include "payload.h"

class AgentAlert {
 public:
  // Every new payload.
  void update(const AgentsData &a) {
    char cur[AGENT_TILES][16] = {};
    uint8_t nCur = 0;
    const char *firstNew = nullptr;
    for (uint8_t i = 0; i < a.nTiles && i < AGENT_TILES; i++) {
      if (a.tiles[i].kind != AgentKind::Wait) continue;
      strncpy(cur[nCur], a.tiles[i].id, 15);
      cur[nCur][15] = 0;
      if (!firstNew && !has(prev_, nPrev_, cur[nCur])) firstNew = cur[nCur];
      nCur++;
    }

    const bool promptNew = a.hasPrompt && !seen(a.prompt.id);
    if (promptNew) remember(a.prompt.id);
    if (promptNew || firstNew) {
      pending_ = true;
      copy(target_, (a.hasPrompt) ? a.prompt.agent : firstNew);
    }

    // What take() may open: only an agent that is waiting now.
    hasQ_ = a.hasPrompt && has(cur, nCur, a.prompt.agent);
    if (hasQ_) copy(qAgent_, a.prompt.agent);
    memcpy(prev_, cur, sizeof prev_);
    nPrev_ = nCur;
    if (nCur == 0) pending_ = false;
  }

  // When nothing blocks: the pending alert, once. `agent` is the id to open.
  bool take(bool blocked, char *agent, size_t cap) {
    if (!pending_ || blocked || cap == 0 || nPrev_ == 0) return false;
    const char *id = target_;
    if (!has(prev_, nPrev_, id)) id = hasQ_ ? qAgent_ : prev_[0];
    size_t n = strlen(id);
    if (n > cap - 1) n = cap - 1;
    memcpy(agent, id, n);
    agent[n] = 0;
    pending_ = false;
    return true;
  }

 private:
  static void copy(char *dst, const char *src) {
    strncpy(dst, src, 15);
    dst[15] = 0;
  }
  static bool has(const char (*list)[16], uint8_t n, const char *id) {
    for (uint8_t i = 0; i < n; i++)
      if (strncmp(list[i], id, 15) == 0) return true;
    return false;
  }
  bool seen(const char *id) const { return has(alerted_, nAlerted_, id); }
  void remember(const char *id) {  // a ring: the oldest of 8 is forgotten
    copy(alerted_[head_], id);
    head_ = (uint8_t)((head_ + 1) % 8);
    if (nAlerted_ < 8) nAlerted_++;
  }

  char prev_[AGENT_TILES][16] = {};
  uint8_t nPrev_ = 0;
  char alerted_[8][16] = {};
  uint8_t nAlerted_ = 0, head_ = 0;
  bool pending_ = false;
  char target_[16] = {};
  bool hasQ_ = false;
  char qAgent_[16] = {};
};
