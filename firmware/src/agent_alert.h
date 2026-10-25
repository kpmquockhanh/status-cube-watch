#pragma once
// When the cube alerts (wakes the screen, jumps to the prompt, beeps) because
// an agent started waiting. Pure (no Arduino) so it is host-tested.
// One alert per new prompt: the Mac resends the same payload every 5 s, so an
// alert fires only when a prompt id is new (the last 8 are remembered) or a
// tile turns Wait that was not Wait in the previous payload. A tile that turns
// Wait one payload before its prompt arrives is one wait, so one alert: that
// first prompt id is remembered silently (a later one alerts). An alert that is
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
      nCur++;
    }
    // A bare wait ends when its tile stops waiting: the next one is a new wait.
    uint8_t keep = 0;
    for (uint8_t i = 0; i < nBare_; i++)
      if (has(cur, nCur, bare_[i])) copy(bare_[keep++], bare_[i]);
    nBare_ = keep;
    for (uint8_t i = 0; i < nCur; i++) {
      if (has(prev_, nPrev_, cur[i])) continue;
      if (!firstNew) firstNew = cur[i];
      const bool itsPrompt = a.hasPrompt && strncmp(a.prompt.agent, cur[i], 15) == 0;
      if (!itsPrompt && nBare_ < AGENT_TILES) copy(bare_[nBare_++], cur[i]);
    }

    bool promptNew = a.hasPrompt && !seen(a.prompt.id);
    if (promptNew) {
      remember(a.prompt.id);
      // The prompt of a wait already alerted for (its tile came first): silent.
      if (dropBare(a.prompt.agent)) promptNew = false;
    }
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
  bool dropBare(const char *agent) {
    for (uint8_t i = 0; i < nBare_; i++) {
      if (strncmp(bare_[i], agent, 15) != 0) continue;
      for (uint8_t j = i; j + 1 < nBare_; j++) copy(bare_[j], bare_[j + 1]);
      nBare_--;
      return true;
    }
    return false;
  }
  void remember(const char *id) {  // a ring: the oldest of 8 is forgotten
    copy(alerted_[head_], id);
    head_ = (uint8_t)((head_ + 1) % 8);
    if (nAlerted_ < 8) nAlerted_++;
  }

  char prev_[AGENT_TILES][16] = {};
  uint8_t nPrev_ = 0;
  char alerted_[8][16] = {};
  uint8_t nAlerted_ = 0, head_ = 0;
  // Agents whose tile turned Wait with no prompt of theirs in that payload, and
  // has stayed Wait since: that wait was alerted for, so its first prompt id is not.
  char bare_[AGENT_TILES][16] = {};
  uint8_t nBare_ = 0;
  bool pending_ = false;
  char target_[16] = {};
  bool hasQ_ = false;
  char qAgent_[16] = {};
};
