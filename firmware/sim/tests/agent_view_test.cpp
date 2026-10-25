#include <cstring>

#include "agent_view.h"
#include "check.h"

namespace {

// A payload with one waiting agent "a1" and its prompt "p1". Static: a
// Payload-sized struct does not belong on a test's stack either.
AgentsData g;

void tile(int i, const char *id, const char *name, AgentKind k) {
  AgentTile &t = g.tiles[i];
  std::memset(&t, 0, sizeof t);
  std::strcpy(t.id, id);
  std::strcpy(t.name, name);
  t.kind = k;
}

void prompt(const char *agent, const char *id, PromptKind k, uint8_t nOpts) {
  std::memset(&g.prompt, 0, sizeof g.prompt);
  std::strcpy(g.prompt.agent, agent);
  std::strcpy(g.prompt.id, id);
  g.prompt.kind = k;
  g.prompt.nOpts = nOpts;
  g.hasPrompt = true;
}

void base() {
  std::memset(&g, 0, sizeof g);
  g.present = true;
  g.nTiles = 1;
  tile(0, "a1", "alpha", AgentKind::Wait);
  prompt("a1", "p1", PromptKind::Permission, 3);
}

// open + the update main.cpp always follows it with.
void openAt(AgentView &v, uint32_t now, bool ble = true, bool locked = false) {
  v.open("a1", now);
  v.update(g, ble, locked, now);
}

void wifiShowsOpenOnBluetooth() {
  base();
  AgentView v;
  openAt(v, 0, false);
  CHECK(v.isOpen());
  CHECK(v.screen() == PromptScreen::OverWifi);
  CHECK(!v.tap(0, 5000));
}

void promptForQAgent() {
  base();
  AgentView v;
  CHECK(v.screen() == PromptScreen::None);
  openAt(v, 0);
  CHECK(v.screen() == PromptScreen::Prompt);
  CHECK(std::strcmp(v.agent(), "a1") == 0);
}

void otherWaitingTileIsQueued() {
  base();
  g.nTiles = 2;
  tile(1, "a2", "beta", AgentKind::Wait);
  prompt("a2", "p9", PromptKind::Permission, 2);
  AgentView v;
  openAt(v, 0);
  CHECK(v.screen() == PromptScreen::Queued);
  CHECK(v.buttonsOff(5000) == ButtonsOff::No);  // no buttons at all on this screen
  CHECK(!v.tap(0, 5000));
}

void droppedAndNoText() {
  base();
  g.hasPrompt = false;
  g.promptDropped = true;
  AgentView v;
  openAt(v, 0);
  CHECK(v.screen() == PromptScreen::Dropped);
  g.promptDropped = false;
  CHECK(v.update(g, true, false, 10));
  CHECK(v.screen() == PromptScreen::NoText);
}

void settleBlocksEarlyTap() {
  base();
  AgentView v;
  openAt(v, 100);
  CHECK(v.buttonsOff(100) == ButtonsOff::Settling);
  CHECK(!v.tap(0, 100 + PROMPT_SETTLE_MS - 1));
  CHECK(v.sentButton() == -1);
  CHECK(v.tap(0, 100 + PROMPT_SETTLE_MS));
  CHECK(v.sentButton() == 0);
}

// The prompt was covered (unlock prompt, a ride, the passkey) and is drawn
// again: its buttons settle again before a tap counts.
void resettleRestartsSettle() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.buttonsOff(5000) == ButtonsOff::No);
  CHECK(v.tick(4999));  // settled: the frame shows live buttons
  v.resettle(5000);
  CHECK(v.buttonsOff(5000) == ButtonsOff::Settling);
  CHECK(v.tick(5000));
  CHECK(!v.tap(0, 5000 + PROMPT_SETTLE_MS - 1));
  CHECK(v.tap(0, 5000 + PROMPT_SETTLE_MS));
  // It does not forget the answer: resettling an answered prompt keeps Sent.
  v.resettle(6500);
  CHECK(v.buttonsOff(6500) == ButtonsOff::Sent);
  CHECK(!v.tap(1, 6500 + PROMPT_SETTLE_MS));
}

// An answer that timed out and then reappears (covered and uncovered)
// settles for PROMPT_SETTLE_MS before its buttons come back.
void noAnswerSettlesOnReappear() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(0, 1000));
  uint32_t t = 1000 + ANSWER_WAIT_MS;
  CHECK(v.buttonsOff(t) == ButtonsOff::NoAnswer);
  v.resettle(t);
  CHECK(v.buttonsOff(t + 500) == ButtonsOff::Settling);
  CHECK(!v.tap(0, t + 500));
  CHECK(v.buttonsOff(t + PROMPT_SETTLE_MS) == ButtonsOff::NoAnswer);
  CHECK(v.tap(0, t + PROMPT_SETTLE_MS));
}

// The Mac unlocks: the buttons that were off come on only after a settle.
void unlockRestartsSettle() {
  base();
  AgentView v;
  openAt(v, 0, true, true);
  CHECK(v.buttonsOff(5000) == ButtonsOff::Locked);
  v.update(g, true, false, 5000);
  CHECK(v.buttonsOff(5000) == ButtonsOff::Settling);
  CHECK(!v.tap(0, 5000 + PROMPT_SETTLE_MS - 1));
  CHECK(v.tap(0, 5000 + PROMPT_SETTLE_MS));
  // Staying unlocked does not restart it.
  base();
  AgentView w;
  openAt(w, 0, true, false);
  w.update(g, true, false, 5000);
  CHECK(w.buttonsOff(5000) == ButtonsOff::No);
}

void newIdResettles() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(1, 2000));
  CHECK(v.buttonsOff(2100) == ButtonsOff::Sent);
  std::strcpy(g.prompt.id, "p2");
  CHECK(v.update(g, true, false, 3000));
  CHECK(v.buttonsOff(3000) == ButtonsOff::Settling);
  CHECK(v.sentButton() == -1);
  CHECK(!v.tap(1, 3999));
  CHECK(v.tap(1, 4000));
}

void oneTapOneSend() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(0, 1000));
  CHECK(!v.tap(0, 1001));
  CHECK(!v.tap(1, 1002));
  CHECK(v.sentButton() == 0);
  // The same payload arriving again must not clear Sent.
  CHECK(!v.update(g, true, false, 1500));
  CHECK(v.buttonsOff(1500) == ButtonsOff::Sent);
}

void noAnswerAllowsRetry() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(0, 1000));
  CHECK(v.buttonsOff(1000 + ANSWER_WAIT_MS - 1) == ButtonsOff::Sent);
  CHECK(v.buttonsOff(1000 + ANSWER_WAIT_MS) == ButtonsOff::NoAnswer);
  CHECK(v.tap(2, 1000 + ANSWER_WAIT_MS));
  CHECK(v.sentButton() == 2);
  CHECK(v.buttonsOff(1000 + ANSWER_WAIT_MS + 1) == ButtonsOff::Sent);
}

// bleSendAnswer said no (no Mac subscribed): NoAnswer at once, not after the
// wait, and the buttons are live again. A later tap waits as usual.
void sendFailedIsNoAnswerAtOnce() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(1, 1000));
  v.answerFailed();
  CHECK(v.buttonsOff(1000) == ButtonsOff::NoAnswer);
  CHECK(v.tick(1000));
  CHECK(v.tap(0, 1100));
  CHECK(v.buttonsOff(1100) == ButtonsOff::Sent);
  CHECK(v.buttonsOff(1100 + ANSWER_WAIT_MS) == ButtonsOff::NoAnswer);
}

void inputHasNoButtons() {
  base();
  g.prompt.kind = PromptKind::Input;
  AgentView v;
  openAt(v, 0);
  CHECK(v.buttonsOff(5000) == ButtonsOff::Input);
  CHECK(!v.tap(0, 5000));
}

void notTmuxAndLocked() {
  base();
  g.prompt.notInTmux = true;
  AgentView v;
  openAt(v, 0, true, true);
  CHECK(v.buttonsOff(5000) == ButtonsOff::NotTmux);  // before Locked
  g.prompt.notInTmux = false;
  CHECK(v.update(g, true, true, 10));  // same prompt id, but the reason changed
  CHECK(v.buttonsOff(500) == ButtonsOff::Locked);    // still settling: Locked first
  CHECK(!v.tap(0, 5000));
  CHECK(v.update(g, true, false, 6000));
  CHECK(v.buttonsOff(6000) == ButtonsOff::Settling);  // unlocked: settle first
  CHECK(!v.tap(0, 6000));
  CHECK(v.buttonsOff(6000 + PROMPT_SETTLE_MS) == ButtonsOff::No);
  CHECK(v.tap(0, 6000 + PROMPT_SETTLE_MS));
}

void closesWhenAnswered() {
  base();
  AgentView v;
  openAt(v, 0);
  g.tiles[0].kind = AgentKind::Run;
  g.hasPrompt = false;
  CHECK(v.update(g, true, false, 10));
  CHECK(!v.isOpen());
  CHECK(!v.update(g, true, false, 20));  // nothing open: nothing changes
}

void closesWhenTileGone() {
  base();
  AgentView v;
  openAt(v, 0);
  g.nTiles = 0;
  CHECK(v.update(g, true, false, 10));
  CHECK(!v.isOpen());
}

void tapOutOfRange() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(!v.tap(3, 5000));  // nOpts is 3: buttons 0..2
  CHECK(v.sentButton() == -1);
  CHECK(v.tap(2, 5000));
}

void tickReportsChanges() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(!v.tick(500));
  CHECK(v.tick(PROMPT_SETTLE_MS));  // Settling ends
  CHECK(!v.tick(PROMPT_SETTLE_MS + 1));
  CHECK(v.tap(0, 1100));
  CHECK(!v.tick(1200));
  CHECK(v.tick(1100 + ANSWER_WAIT_MS));  // Sent -> NoAnswer
  CHECK(!v.tick(1100 + ANSWER_WAIT_MS + 1));
}

void closeThenLongWaitIsNoAnswer() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(0, 2000));
  v.close();
  CHECK(!v.isOpen());
  CHECK(v.sentButton() == -1);
  openAt(v, 20000);  // the wait is long over: the answer may be asked again
  CHECK(v.buttonsOff(20000) == ButtonsOff::Settling);
  CHECK(v.buttonsOff(20000 + PROMPT_SETTLE_MS) == ButtonsOff::NoAnswer);
  CHECK(v.tap(0, 20000 + PROMPT_SETTLE_MS));
}

void sentSurvivesScreenRoundTrip() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(0, 1000));
  CHECK(v.update(g, false, false, 1100));  // one WiFi payload
  CHECK(v.screen() == PromptScreen::OverWifi);
  CHECK(v.update(g, true, false, 1200));   // BLE again, same prompt
  CHECK(v.screen() == PromptScreen::Prompt);
  CHECK(v.buttonsOff(2500) == ButtonsOff::Sent);
  CHECK(!v.tap(0, 2500));
  CHECK(v.sentButton() == 0);
}

void sentSurvivesCloseReopen() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(1, 1000));
  v.close();
  v.open("a1", 1100);
  v.update(g, true, false, 1100);
  CHECK(v.buttonsOff(2500) == ButtonsOff::Sent);
  CHECK(!v.tap(1, 2500));
}

void newIdAfterSentIsTappable() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(0, 1000));
  std::strcpy(g.prompt.id, "p2");
  v.update(g, true, false, 2000);
  CHECK(!v.tap(0, 2999));
  CHECK(v.tap(0, 3000));
}

void lockAfterTapShowsLocked() {
  base();
  AgentView v;
  openAt(v, 0);
  CHECK(v.tap(0, 1000));
  CHECK(v.update(g, true, true, 1500));
  CHECK(v.buttonsOff(1500) == ButtonsOff::Locked);
}

void returnToPromptRestartsSettle() {
  base();
  g.nTiles = 2;
  tile(1, "a2", "beta", AgentKind::Wait);
  AgentView v;
  openAt(v, 0);
  prompt("a2", "p9", PromptKind::Permission, 2);
  CHECK(v.update(g, true, false, 3000));
  CHECK(v.screen() == PromptScreen::Queued);
  prompt("a1", "p1", PromptKind::Permission, 3);
  CHECK(v.update(g, true, false, 4000));
  CHECK(v.buttonsOff(4000) == ButtonsOff::Settling);
  CHECK(!v.tap(0, 4999));
}

void wrapSafe() {
  base();
  AgentView v;
  uint32_t t0 = 0xFFFFFF00u;  // settle and answer wait both cross the wrap
  openAt(v, t0);
  CHECK(!v.tap(0, t0 + PROMPT_SETTLE_MS - 1));
  CHECK(v.tap(0, t0 + PROMPT_SETTLE_MS));
  uint32_t sent = t0 + PROMPT_SETTLE_MS;
  CHECK(v.buttonsOff(sent + ANSWER_WAIT_MS - 1) == ButtonsOff::Sent);
  CHECK(v.buttonsOff(sent + ANSWER_WAIT_MS) == ButtonsOff::NoAnswer);
}

}  // namespace

int main() {
  wifiShowsOpenOnBluetooth();
  promptForQAgent();
  otherWaitingTileIsQueued();
  droppedAndNoText();
  settleBlocksEarlyTap();
  resettleRestartsSettle();
  noAnswerSettlesOnReappear();
  unlockRestartsSettle();
  newIdResettles();
  oneTapOneSend();
  noAnswerAllowsRetry();
  sendFailedIsNoAnswerAtOnce();
  inputHasNoButtons();
  notTmuxAndLocked();
  closesWhenAnswered();
  closesWhenTileGone();
  tapOutOfRange();
  tickReportsChanges();
  closeThenLongWaitIsNoAnswer();
  sentSurvivesScreenRoundTrip();
  sentSurvivesCloseReopen();
  newIdAfterSentIsTappable();
  lockAfterTapShowsLocked();
  returnToPromptRestartsSettle();
  wrapSafe();
  return checksDone("agent_view_test");
}
