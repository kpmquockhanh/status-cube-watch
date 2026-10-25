#pragma once
// Which prompt screen shows for the agent whose waiting tile the user opened,
// and whether its answer buttons may send. Pure: no Arduino, host-tested in
// sim/tests/agent_view_test.cpp.
//
// This is the guard against a stale tap answering a newer prompt. It keeps its
// own copies of what it needs (ids, kind, option count) and never a pointer
// into a payload, because main.cpp overwrites the payload in place. The settle
// time starts whenever the Prompt screen is entered or its prompt id changes,
// so a tap aimed at one prompt cannot land on the next one a moment after it
// replaces the first. Elapsed times are unsigned subtractions: a millis() wrap
// is harmless.
#include <stdint.h>
#include <string.h>

#include "payload.h"

constexpr uint32_t PROMPT_SETTLE_MS = 1000, ANSWER_WAIT_MS = 8000;

enum class PromptScreen : uint8_t { None, Prompt, Queued, OverWifi, Dropped, NoText };

// Why the buttons are off; drawn as one caps line.
enum class ButtonsOff : uint8_t { No, Input, NotTmux, Locked, Settling, Sent, NoAnswer };

class AgentView {
 public:
  // A tap on a waiting tile, or the alert. The screen stays None until the
  // first update(), which main.cpp calls right after.
  void open(const char *agentId, uint32_t now) {
    (void)now;
    open_ = true;
    strncpy(agent_, agentId, sizeof agent_ - 1);
    agent_[sizeof agent_ - 1] = 0;
    screen_ = PromptScreen::None;
    promptId_[0] = 0;
    macLocked_ = false;
    lastScreen_ = PromptScreen::None;
    lastOff_ = ButtonsOff::No;
  }

  // Swipe right / BACK.
  void close() {
    open_ = false;
    screen_ = PromptScreen::None;
    promptId_[0] = 0;
    lastScreen_ = PromptScreen::None;
    lastOff_ = ButtonsOff::No;
  }

  bool isOpen() const { return open_; }
  const char *agent() const { return agent_; }

  // Every new payload, and whenever the link or lock state changes. Closes the
  // view when the agent's tile is gone or no longer waiting. Returns true when
  // the screen, the prompt id shown or the button state changed (main.cpp
  // redraws on every payload anyway, so text-only changes need no report).
  bool update(const AgentsData &a, bool viaBle, bool macLocked, uint32_t now) {
    if (!open_) return false;
    macLocked_ = macLocked;
    const AgentTile *t = agentsTile(a, agent_);
    if (!t || t->kind != AgentKind::Wait) {
      close();
      return true;
    }

    PromptScreen s;
    if (!viaBle) s = PromptScreen::OverWifi;
    else if (a.hasPrompt && strcmp(a.prompt.agent, agent_) == 0) s = PromptScreen::Prompt;
    else if (a.hasPrompt) s = PromptScreen::Queued;
    else if (a.promptDropped) s = PromptScreen::Dropped;
    else s = PromptScreen::NoText;

    bool changed = s != screen_;
    if (s == PromptScreen::Prompt) {
      bool newId = strcmp(promptId_, a.prompt.id) != 0;
      if (newId || screen_ != PromptScreen::Prompt) {
        shownAt_ = now;  // entering Prompt, or a new prompt: settle again
      }
      if (newId) changed = true;
      strncpy(promptId_, a.prompt.id, sizeof promptId_ - 1);
      promptId_[sizeof promptId_ - 1] = 0;
      kind_ = a.prompt.kind;
      nOpts_ = a.prompt.nOpts;
      notInTmux_ = a.prompt.notInTmux;
    } else {
      if (promptId_[0]) changed = true;
      promptId_[0] = 0;
    }
    screen_ = s;

    ButtonsOff off = buttonsOff(now);
    if (off != lastOff_) changed = true;
    lastScreen_ = screen_;
    lastOff_ = off;
    return changed;
  }

  PromptScreen screen() const { return screen_; }

  // Only the Prompt screen has buttons; first match wins.
  ButtonsOff buttonsOff(uint32_t now) const {
    if (screen_ != PromptScreen::Prompt) return ButtonsOff::No;
    if (kind_ == PromptKind::Input) return ButtonsOff::Input;
    if (notInTmux_) return ButtonsOff::NotTmux;
    if (macLocked_) return ButtonsOff::Locked;
    if (answeredShown()) {
      return !failed_ && (uint32_t)(now - sentAt_) < ANSWER_WAIT_MS ? ButtonsOff::Sent : ButtonsOff::NoAnswer;
    }
    if ((uint32_t)(now - shownAt_) < PROMPT_SETTLE_MS) return ButtonsOff::Settling;
    return ButtonsOff::No;
  }

  // A tap on button i: true when Control 09 should go out (the view is then
  // Sent, i lit). One tap, one send; nothing is resent.
  bool tap(uint8_t i, uint32_t now) {
    if (screen_ != PromptScreen::Prompt || i >= nOpts_) return false;
    ButtonsOff off = buttonsOff(now);
    if (off != ButtonsOff::No && off != ButtonsOff::NoAnswer) return false;
    strcpy(sentId_, promptId_);
    sent_ = true;
    sentAt_ = now;
    sentBtn_ = (int8_t)i;
    failed_ = false;
    lastOff_ = ButtonsOff::Sent;  // the caller redraws after a tap itself
    return true;
  }

  // The tap's Control 09 could not go out (no Mac on the link): no answer is
  // coming, so the buttons show NoAnswer now instead of after ANSWER_WAIT_MS.
  void answerFailed() { failed_ = true; }

  int8_t sentButton() const { return answeredShown() ? sentBtn_ : (int8_t)-1; }

  // The settle or answer wait ran out (or anything else time-driven changed):
  // redraw.
  bool tick(uint32_t now) {
    ButtonsOff off = buttonsOff(now);
    bool changed = off != lastOff_ || screen_ != lastScreen_;
    lastOff_ = off;
    lastScreen_ = screen_;
    return changed;
  }

 private:
  // The shown prompt is the one already answered. The memory outlives screen
  // changes, close() and open(): a WiFi payload in between or BACK and reopen
  // must not let a second Control 09 out for the same prompt. A tap on another
  // prompt id replaces it.
  bool answeredShown() const { return sent_ && promptId_[0] && strcmp(promptId_, sentId_) == 0; }

  bool open_ = false;
  char agent_[16] = {0};
  char promptId_[16] = {0};
  PromptScreen screen_ = PromptScreen::None;
  PromptKind kind_ = PromptKind::Input;
  uint8_t nOpts_ = 0;
  bool notInTmux_ = false;
  bool macLocked_ = false;
  uint32_t shownAt_ = 0;
  char sentId_[16] = {0};
  bool sent_ = false;
  bool failed_ = false;  // the last tap's send failed
  uint32_t sentAt_ = 0;
  int8_t sentBtn_ = -1;
  PromptScreen lastScreen_ = PromptScreen::None;
  ButtonsOff lastOff_ = ButtonsOff::No;
};
