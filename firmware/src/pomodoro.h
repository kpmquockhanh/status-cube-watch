#pragma once
#include <stddef.h>
#include <stdint.h>

// Pomodoro timer state machine. Pure logic: time comes in as an argument
// (millis() on the board, a fake clock in sim/tests), so the whole cycle can
// be checked on the host. It never starts a phase by itself -- every phase
// begins with a long-press, so it cannot start a focus session while you are
// away from the desk.

enum PomoState : uint8_t { POMO_IDLE, POMO_FOCUS, POMO_BREAK, POMO_PAUSED, POMO_DONE };
enum PomoPhase : uint8_t { PHASE_FOCUS, PHASE_SHORT, PHASE_LONG };

struct PomoConfig {
  uint32_t focusMs, shortMs, longMs;
  uint8_t sessions;  // focus sessions before the long break
};

// What the UI needs to draw one frame.
struct PomoView {
  PomoState state;
  // IDLE: focus (what a start would begin). FOCUS/BREAK/PAUSED: the phase in
  // progress. DONE: the phase that just ended.
  PomoPhase phase;
  PomoPhase next;       // what a long-press starts from DONE
  uint32_t leftMs;
  uint8_t fraction;     // time left in the phase, 0..100
  uint8_t completed;    // focus sessions finished in this set; resets after the long break
  uint8_t sessions;
  uint32_t displaySec;  // leftMs rounded UP to whole seconds: 25:00 for the first second
};

class Pomodoro {
 public:
  explicit Pomodoro(const PomoConfig &cfg);

  void tick(uint32_t now);       // every loop; advances a running phase
  void longPress(uint32_t now);  // start / pause / resume / begin the next phase
  void reset();                  // back to IDLE, count cleared, alert dropped
  // Replaces the durations. Only honoured while IDLE (the editor only opens
  // then); a running, paused or finished phase keeps the lengths it began
  // with. Refreshes the idle countdown so the card shows the new focus length.
  void setConfig(const PomoConfig &cfg);
  bool takeAlert();              // true exactly once after each phase end
  PomoView view() const;

 private:
  uint32_t lengthOf(PomoPhase p) const;
  void begin(PomoPhase p, uint32_t now);
  void finish();

  PomoConfig _cfg;
  PomoState _state;
  PomoState _resumeAs;  // FOCUS or BREAK: what PAUSED goes back to
  PomoPhase _phase;
  PomoPhase _next;
  uint32_t _left;       // ms left in the phase
  uint32_t _last;       // `now` at the last tick of a running phase
  uint8_t _completed;
  bool _alert;
};

// "MM:SS", clamped to 99:59 so it never needs more than five characters.
void pomoFormatTime(uint32_t sec, char *buf, size_t cap);
