#include "check.h"
#include "agent_alert.h"

#include <string.h>

namespace {

AgentsData g;  // too big to be pleasant on a stack, and tests reuse it

void reset() { memset(&g, 0, sizeof g); g.present = true; }

void tile(const char *id, AgentKind k) {
  AgentTile &t = g.tiles[g.nTiles++];
  strcpy(t.id, id);
  t.kind = k;
}

void prompt(const char *agent, const char *id) {
  g.hasPrompt = true;
  strcpy(g.prompt.agent, agent);
  strcpy(g.prompt.id, id);
}

bool takeOne(AgentAlert &a, bool blocked, char *out = nullptr) {
  char buf[16];
  return a.take(blocked, out ? out : buf, 16);
}

void testFirstPromptAlerts() {
  AgentAlert a;
  reset(); tile("a1", AgentKind::Wait); prompt("a1", "p1");
  a.update(g);
  char id[16];
  CHECK(takeOne(a, false, id));
  CHECK(strcmp(id, "a1") == 0);
  CHECK(!takeOne(a, false));
}

void testSamePromptResentDoesNot() {
  AgentAlert a;
  reset(); tile("a1", AgentKind::Wait); prompt("a1", "p1");
  int n = 0;
  for (int i = 0; i < 3; i++) { a.update(g); if (takeOne(a, false)) n++; }
  CHECK(n == 1);
}

void testNextPromptAlertsOnce() {
  AgentAlert a;
  reset(); tile("a1", AgentKind::Wait); prompt("a1", "p1");
  a.update(g); CHECK(takeOne(a, false));
  reset(); tile("a1", AgentKind::Wait); tile("a2", AgentKind::Wait); prompt("a2", "p2");
  int n = 0; char id[16] = "";
  for (int i = 0; i < 3; i++) { a.update(g); if (takeOne(a, false, id)) n++; }
  CHECK(n == 1);
  CHECK(strcmp(id, "a2") == 0);
}

void testWaitingTileWithoutPromptAlerts() {
  AgentAlert a;
  reset(); tile("a1", AgentKind::Run);
  a.update(g); CHECK(!takeOne(a, false));
  reset(); tile("a1", AgentKind::Wait);
  a.update(g);
  char id[16];
  CHECK(takeOne(a, false, id));
  CHECK(strcmp(id, "a1") == 0);
  a.update(g);
  CHECK(!takeOne(a, false));
}

void testBlockedAlertWaits() {
  AgentAlert a;
  reset(); tile("a1", AgentKind::Wait); prompt("a1", "p1");
  a.update(g);
  CHECK(!takeOne(a, true));
  a.update(g);  // the resend while blocked must neither lose nor double it
  CHECK(!takeOne(a, true));
  CHECK(takeOne(a, false));
  CHECK(!takeOne(a, false));
}

void testBlockedAlertDropsWhenAnswered() {
  AgentAlert a;
  reset(); tile("a1", AgentKind::Wait); prompt("a1", "p1");
  a.update(g);
  CHECK(!takeOne(a, true));
  reset(); tile("a1", AgentKind::Run);
  a.update(g);
  CHECK(!takeOne(a, false));
}

void testPromptIdMemory() {
  char ids[10][4];
  for (int i = 0; i < 10; i++) { ids[i][0] = 'p'; ids[i][1] = (char)('0' + i); ids[i][2] = 0; }
  {
    AgentAlert a;  // 9 distinct ids: the first has been forgotten
    for (int i = 0; i < 9; i++) {
      reset(); tile("a1", AgentKind::Wait); prompt("a1", ids[i]);
      a.update(g); CHECK(takeOne(a, false));
    }
    reset(); tile("a1", AgentKind::Wait); prompt("a1", ids[0]);
    a.update(g); CHECK(takeOne(a, false));
  }
  {
    AgentAlert a;  // 8 distinct ids: the first is still remembered
    for (int i = 0; i < 8; i++) {
      reset(); tile("a1", AgentKind::Wait); prompt("a1", ids[i]);
      a.update(g); CHECK(takeOne(a, false));
    }
    reset(); tile("a1", AgentKind::Wait); prompt("a1", ids[0]);
    a.update(g); CHECK(!takeOne(a, false));
  }
}

}  // namespace

int main() {
  testFirstPromptAlerts();
  testSamePromptResentDoesNot();
  testNextPromptAlertsOnce();
  testWaitingTileWithoutPromptAlerts();
  testBlockedAlertWaits();
  testBlockedAlertDropsWhenAnswered();
  testPromptIdMemory();
  return checksDone("agent_alert_test");
}
