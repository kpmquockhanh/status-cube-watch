#include <cstring>

#include "check.h"
#include "detail_view.h"

namespace {

// AgentsData is large: kept off the stack, as on the board.
AgentsData g_a;

void setDetail(const char *id) {
  memset(&g_a, 0, sizeof(g_a));
  g_a.present = true;
  if (id) {
    g_a.detail.present = true;
    strncpy(g_a.detail.id, id, sizeof(g_a.detail.id) - 1);
  }
}

void testClosed() {
  DetailView v;
  setDetail(nullptr);
  CHECK(!v.isOpen());
  CHECK(!strcmp(v.agent(), ""));
  CHECK(!v.sendDue(g_a, 0));
  CHECK(!v.pickerOpen());
}

void testResend(uint32_t t0) {
  DetailView v;
  setDetail(nullptr);
  v.open("l00002", t0);
  CHECK(v.isOpen() && !strcmp(v.agent(), "l00002"));
  CHECK(v.sendDue(g_a, t0));  // right after open
  v.sent(t0);
  CHECK(!v.sendDue(g_a, t0));
  CHECK(!v.sendDue(g_a, t0 + 4999));
  CHECK(v.sendDue(g_a, t0 + 5000));  // `d` still missing
  v.sent(t0 + 5000);
  CHECK(!v.sendDue(g_a, t0 + 9999));
  setDetail("l00002");  // the Mac caught up
  CHECK(!v.sendDue(g_a, t0 + 10000));
  CHECK(!v.sendDue(g_a, t0 + 60000));
  setDetail("l00009");  // `d` for another agent
  CHECK(v.sendDue(g_a, t0 + 10000));
  CHECK(!v.sendDue(g_a, t0 + 9999));
  // Not an Agents card at all: still due.
  memset(&g_a, 0, sizeof(g_a));
  CHECK(v.sendDue(g_a, t0 + 10000));
}

void testOpenEvenWithDetail() {
  DetailView v;
  setDetail("l00002");
  v.open("l00002", 100);
  CHECK(v.sendDue(g_a, 100));  // the Mac must hear the open once
  v.sent(100);
  CHECK(!v.sendDue(g_a, 5100));
}

void testClose() {
  DetailView v;
  v.open("l00002", 0);
  v.openPicker();
  CHECK(v.pickerOpen());
  v.closePicker();
  CHECK(!v.pickerOpen());
  v.openPicker();
  v.close();
  CHECK(!v.isOpen() && !v.pickerOpen());
  CHECK(!strcmp(v.agent(), ""));
  setDetail(nullptr);
  CHECK(!v.sendDue(g_a, 6000));
  v.openPicker();  // no picker without a detail screen
  CHECK(!v.pickerOpen());
}

void testLongIdIsCut() {
  DetailView v;
  v.open("0123456789abcdefXYZ", 0);
  CHECK(strlen(v.agent()) == 15);
  v.open(nullptr, 0);
  CHECK(!v.isOpen());
}

void testPick(uint32_t t) {
  DetailView v;
  v.open("l00002", t);
  CHECK(v.shownHat(5, t) == 5);  // nothing picked
  v.pick(17, t);
  CHECK(v.shownHat(5, t + 100) == 17);
  CHECK(v.shownHat(5, t + 3999) == 17);
  CHECK(v.shownHat(5, t + 4001) == 5);
  CHECK(v.shownHat(17, t + 100) == 17);
  CHECK(v.shownHat(17, t + 4001) == 17);
  CHECK(!v.pickedAuto(false, t + 100));
  CHECK(!v.pickedAuto(true, t + 100));  // a code pick overrides a stale "auto"
  CHECK(v.pickedAuto(true, t + 4001));
  v.pick(-1, t);
  CHECK(v.shownHat(5, t + 100) == -1);  // none
  CHECK(v.shownHat(5, t + 4001) == 5);
  v.pick(-2, t);
  CHECK(v.pickedAuto(false, t + 100));
  CHECK(!v.pickedAuto(false, t + 4001));
  CHECK(v.pickedAuto(true, t + 4001));
  CHECK(v.shownHat(5, t + 100) == 5);  // the automatic hat is the bridge's to say
  // Reopening another agent forgets the pick.
  v.pick(17, t);
  v.open("l00003", t + 10);
  CHECK(v.shownHat(5, t + 100) == 5);
  CHECK(!v.pickedAuto(false, t + 100));
}

}  // namespace

int main() {
  testClosed();
  testResend(1000);
  testResend(0xFFFFFFFFu - 2000);  // millis() wraps inside the 5 s
  testOpenEvenWithDetail();
  testClose();
  testLongIdIsCut();
  testPick(500);
  testPick(0xFFFFFFFFu - 50);
  CHECK(DETAIL_RESEND_MS == 5000 && HAT_PICK_HOLD_MS == 4000);
  return checksDone("detail_view_test");
}
