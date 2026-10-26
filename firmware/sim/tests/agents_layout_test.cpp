#include "check.h"
#include "agents_layout.h"

int main() {
  // Tiles: two columns, rows 102 px apart.
  LayoutRect r = agentsTileRect(0, 0);
  CHECK(r.x == 9 && r.y == 54 && r.w == 108 && r.h == 96);
  r = agentsTileRect(1, 0);
  CHECK(r.x == 123 && r.y == 54 && r.w == 108 && r.h == 96);
  r = agentsTileRect(2, 0);
  CHECK(r.x == 9 && r.y == 156);
  r = agentsTileRect(3, 0);
  CHECK(r.x == 123 && r.y == 156);
  CHECK(agentsTileRect(2, 60).y == 96);
  // Two rows fill the view, so four tiles never scroll, and it ends clear of
  // the page dots.
  CHECK(agentsContentH(4) <= AGENTS_VIEW_H);
  CHECK(AGENTS_VIEW_Y + AGENTS_VIEW_H <= 254);

  CHECK(agentsContentH(0) == 0);
  CHECK(agentsContentH(1) == 96);
  CHECK(agentsContentH(2) == 96);
  CHECK(agentsContentH(3) == 198);
  CHECK(agentsContentH(5) == 300);

  CHECK(agentsTileAt(170, 200, 4, 0) == 3);
  CHECK(agentsTileAt(60, 100, 4, 0) == 0);
  CHECK(agentsTileAt(119, 100, 4, 0) == -1);  // the column gap
  CHECK(agentsTileAt(60, 152, 4, 0) == -1);   // the row gap
  CHECK(agentsTileAt(170, 200, 3, 0) == -1);  // no tile 3
  CHECK(agentsTileAt(60, 50, 4, 60) == -1);   // above the view
  CHECK(agentsTileAt(60, 254, 6, 0) == -1);   // below the view
  CHECK(agentsTileAt(60, 100, 4, 60) == 2);   // scrolled: row 2 now sits at y 96
  CHECK(agentsTileAt(60, 55, 4, 60) == 0);    // tile 0 is still partly in view at y 55

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
  CHECK(p.buttonAt(120, top + 134 + 10, 0) == 0);
  CHECK(p.buttonAt(120, top + 174 + 10, 0) == 1);
  CHECK(p.buttonAt(120, top + 134 + 36, 0) == -1);  // in the gap
  CHECK(p.buttonAt(120, top + 256 + 10, 0) == -1);       // BACK is below the view until scrolled
  CHECK(p.buttonAt(120, top + 256 + 10 - 90, 90) == PROMPT_HIT_BACK);
  CHECK(p.buttonAt(120, top + 10, 0) == -1);
  CHECK(p.buttonAt(120, top + 174 + 10 - 40, 40) == 1);  // scrolled 40
  CHECK(p.buttonAt(120, top - 5, 0) == -1);              // above the view
  CHECK(p.buttonAt(120, PROMPT_VIEW_Y + PROMPT_VIEW_H + 5, 100) == -1);
  // Across: only between the buttons' left and right edges (they span the column).
  CHECK(p.buttonAt(PROMPT_COL_X, top + 134 + 10, 0) == 0);
  CHECK(p.buttonAt(PROMPT_COL_X + PROMPT_COL_W - 1, top + 134 + 10, 0) == 0);
  CHECK(p.buttonAt(PROMPT_COL_X - 1, top + 134 + 10, 0) == -1);
  CHECK(p.buttonAt(PROMPT_COL_X + PROMPT_COL_W, top + 134 + 10, 0) == -1);
  CHECK(p.buttonAt(5, top + 256 + 10 - 90, 90) == -1);  // beside BACK

  // Optional blocks.
  PromptMetrics m2{1, 0, false, true};
  p.build(m2, 2);
  // title 0-20, gap, reason 28-46, gap, buttons 54 and 94 (to 128), gap, BACK 136
  CHECK(p.reasonY() == 28);
  CHECK(p.buttonY(0) == 54);
  CHECK(p.buttonY(1) == 94);
  CHECK(p.backY() == 136);
  CHECK(p.contentH() == 170);
  CHECK(p.buttonAt(120, PROMPT_VIEW_Y + 30, 0) == -1);  // the reason line is not a button
  CHECK(p.buttonAt(120, PROMPT_VIEW_Y + 60, 0) == 0);

  // The reason line pushes the buttons down by its height and one gap.
  PromptMetrics m3{2, 3, true, false};
  p.build(m3, 3);
  const int16_t noReason = p.buttonY(0), backNo = p.backY();
  CHECK(p.reasonY() == -1);
  m3.reason = true;
  p.build(m3, 3);
  CHECK(p.reasonY() == 134);  // after the context line (110-126) and a gap
  CHECK(p.buttonY(0) == noReason + PROMPT_REASON_H + PROMPT_BLOCK_GAP);
  CHECK(p.buttonY(0) == noReason + 26);
  CHECK(p.backY() == backNo + 26);
  p.build(m2, 0);  // no options (an input prompt): title, reason, BACK
  CHECK(p.backY() == 20 + 8 + 18 + 8);
  CHECK(p.buttonAt(120, PROMPT_VIEW_Y + 1000, 0) == -1);

  // runDotAlpha: base everywhere outside the sweep and off the crest; the
  // crest reaches the peak, and walks from bottom-left to top-right.
  CHECK(runDotAlpha(50, 50, TILE_W, TILE_H, RUN_WAVE_SWEEP_MS + 1) == RUN_DOT_BASE);  // the pause
  // The crest enters from just off the corner: it is on the bottom-left dot
  // (f = 0) and later on the top-right one (f = 1).
  {
    auto crestAt = [](float f) {
      return (uint32_t)((f + RUN_WAVE_BAND) / (1 + 2 * RUN_WAVE_BAND) * RUN_WAVE_SWEEP_MS + 0.5f);
    };
    CHECK(runDotAlpha(0, TILE_H - 1, TILE_W, TILE_H, 0) == RUN_DOT_BASE);
    CHECK(runDotAlpha(0, TILE_H - 1, TILE_W, TILE_H, crestAt(0)) >= RUN_DOT_PEAK - 1);
    CHECK(runDotAlpha(TILE_W - 1, 0, TILE_W, TILE_H, crestAt(0)) == RUN_DOT_BASE);  // top-right: not yet
    CHECK(runDotAlpha(TILE_W - 1, 0, TILE_W, TILE_H, crestAt(1)) >= RUN_DOT_PEAK - 1);
    CHECK(runDotAlpha(0, TILE_H - 1, TILE_W, TILE_H, crestAt(1)) == RUN_DOT_BASE);
  }
  {
    uint8_t hi = 0;
    for (uint32_t ms = 0; ms < 2 * RUN_WAVE_MS; ms += 5)
      for (int16_t y = 0; y < TILE_H; y += RUN_DOT_PITCH)
        for (int16_t x = 0; x < TILE_W; x += RUN_DOT_PITCH) {
          const uint8_t v = runDotAlpha(x, y, TILE_W, TILE_H, ms);
          CHECK(v >= RUN_DOT_BASE && v <= RUN_DOT_PEAK);
          if (v > hi) hi = v;
        }
    CHECK(hi >= RUN_DOT_PEAK - 2);
  }
  CHECK(runDotAlpha(1, 1, 1, 1, 100) == RUN_DOT_BASE);  // a degenerate tile does not divide by zero

  // runParticle: inside the tile, rising, faded at both ends, never in step.
  for (uint8_t i = 0; i < RUN_PARTICLES; i++) {
    RunParticle prev = runParticle(i, TILE_W, TILE_H, 0);
    for (uint32_t ms = 0; ms < 10000; ms += 10) {
      const RunParticle p = runParticle(i, TILE_W, TILE_H, ms);
      CHECK(p.x >= 2 && p.x + RUN_PARTICLE_SIZE <= TILE_W - 2);
      CHECK(p.y >= 2 && p.y + RUN_PARTICLE_SIZE <= TILE_H - 2);
      CHECK(p.alpha <= RUN_PARTICLE_ALPHA);
      if (p.alpha > 0 && prev.alpha > 0 && p.y > prev.y) CHECK(prev.y < 12 || p.alpha < 20);  // only a wrap moves it down
      prev = p;
    }
  }
  {
    const RunParticle a = runParticle(0, TILE_W, TILE_H, 0), b = runParticle(0, TILE_W, TILE_H, 1500);
    CHECK(a.alpha == 0 && b.alpha == RUN_PARTICLE_ALPHA && b.y < a.y);
    CHECK(runParticle(0, TILE_W, TILE_H, 500).y != runParticle(1, TILE_W, TILE_H, 500).y);
    CHECK(runParticle(RUN_PARTICLES, TILE_W, TILE_H, 0).alpha == 0);
    CHECK(runParticle(2, TILE_W, TILE_H, 0xFFFFFFFFu).alpha <= RUN_PARTICLE_ALPHA);  // a millis() wrap is harmless
  }

  return checksDone("agents_layout");
}
