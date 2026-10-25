#include "check.h"
#include "agents_layout.h"

int main() {
  // Tiles: two columns, rows 92 px apart.
  LayoutRect r = agentsTileRect(0, 0);
  CHECK(r.x == 14 && r.y == 58 && r.w == 102 && r.h == 84);
  r = agentsTileRect(1, 0);
  CHECK(r.x == 124 && r.y == 58 && r.w == 102 && r.h == 84);
  r = agentsTileRect(2, 0);
  CHECK(r.x == 14 && r.y == 150);
  r = agentsTileRect(3, 0);
  CHECK(r.x == 124 && r.y == 150);
  CHECK(agentsTileRect(2, 60).y == 90);

  CHECK(agentsContentH(0) == 0);
  CHECK(agentsContentH(1) == 84);
  CHECK(agentsContentH(2) == 84);
  CHECK(agentsContentH(3) == 176);
  CHECK(agentsContentH(5) == 268);

  CHECK(agentsTileAt(170, 200, 4, 0) == 3);
  CHECK(agentsTileAt(60, 100, 4, 0) == 0);
  CHECK(agentsTileAt(119, 100, 4, 0) == -1);  // the column gap
  CHECK(agentsTileAt(60, 146, 4, 0) == -1);   // the row gap
  CHECK(agentsTileAt(170, 200, 3, 0) == -1);  // no tile 3
  CHECK(agentsTileAt(60, 50, 4, 60) == -1);   // above the view
  CHECK(agentsTileAt(60, 254, 6, 0) == -1);   // below the view
  CHECK(agentsTileAt(60, 100, 4, 60) == 2);   // scrolled: row 2 now sits at y 90
  CHECK(agentsTileAt(60, 59, 4, 60) == 0);    // tile 0 is still partly in view at y 59

  // Scroll: 268 in a 196 view leaves 72.
  ScrollY s;
  CHECK(s.offset() == 0);
  CHECK(!s.drag(100));  // no grab
  s.setRange(268, 196);
  s.grab(200);
  CHECK(s.drag(150));  // finger up 50: content up 50
  CHECK(s.offset() == 50);
  CHECK(s.drag(100));
  CHECK(s.offset() == 72);
  CHECK(!s.drag(60));  // at the stop
  CHECK(s.drag(160));  // finger 40 above where it grabbed: 40
  CHECK(s.offset() == 40);
  CHECK(s.drag(400));
  CHECK(s.offset() == 0);
  CHECK(!s.drag(500));
  s.release();
  CHECK(!s.drag(100));  // a drag needs a grab
  s.grab(200);
  s.drag(130);
  CHECK(s.offset() == 70);
  s.release();
  CHECK(s.offset() == 70);
  s.grab(0);  // from 70, the finger goes up 50 more: clamped to 72
  CHECK(s.drag(-50));
  CHECK(s.offset() == 72);
  s.release();
  s.setRange(100, 196);  // fits: clamps to 0
  CHECK(s.offset() == 0);
  s.setRange(268, 196);
  s.grab(10);
  s.drag(-30);
  CHECK(s.offset() == 40);
  s.release();
  s.reset();
  CHECK(s.offset() == 0);

  // Prompt: 2 title lines, 3 detail lines, context, 3 buttons, no reason.
  PromptMetrics m{2, 3, true, false};
  PromptLayout p;
  p.build(m, 3);
  // title 0-40, gap, detail 48-102, gap, context 110-126, gap, buttons from 134
  CHECK(p.buttonY(0) == 134);
  CHECK(p.buttonY(1) == 174);
  CHECK(p.buttonY(2) == 214);
  CHECK(p.backY() == 256);
  CHECK(p.contentH() == 290);
  const int16_t top = PROMPT_VIEW_Y;
  CHECK(p.buttonAt(top + 134 + 10, 0) == 0);
  CHECK(p.buttonAt(top + 174 + 10, 0) == 1);
  CHECK(p.buttonAt(top + 134 + 36, 0) == -1);  // in the gap
  CHECK(p.buttonAt(top + 256 + 10, 0) == -1);       // BACK is below the view until scrolled
  CHECK(p.buttonAt(top + 256 + 10 - 90, 90) == PROMPT_HIT_BACK);
  CHECK(p.buttonAt(top + 10, 0) == -1);
  CHECK(p.buttonAt(top + 174 + 10 - 40, 40) == 1);  // scrolled 40
  CHECK(p.buttonAt(top - 5, 0) == -1);              // above the view
  CHECK(p.buttonAt(PROMPT_VIEW_Y + PROMPT_VIEW_H + 5, 100) == -1);

  // Optional blocks.
  PromptMetrics m2{1, 0, false, true};
  p.build(m2, 2);
  // title 0-20, gap, buttons 28 and 68 (to 102), gap, reason 110-128, gap, BACK 136
  CHECK(p.buttonY(0) == 28);
  CHECK(p.buttonY(1) == 68);
  CHECK(p.backY() == 136);
  CHECK(p.contentH() == 170);
  p.build(m2, 0);  // no options (an input prompt): title, reason, BACK
  CHECK(p.backY() == 20 + 8 + 18 + 8);
  CHECK(p.buttonAt(PROMPT_VIEW_Y + 1000, 0) == -1);

  return checksDone("agents_layout");
}
