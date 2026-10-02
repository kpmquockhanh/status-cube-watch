#include "pomodoro.h"

#include <stdio.h>

Pomodoro::Pomodoro(const PomoConfig &cfg) : _cfg(cfg) { reset(); }

uint32_t Pomodoro::lengthOf(PomoPhase p) const {
  switch (p) {
    case PHASE_FOCUS: return _cfg.focusMs;
    case PHASE_SHORT: return _cfg.shortMs;
    default: return _cfg.longMs;
  }
}

void Pomodoro::reset() {
  _state = POMO_IDLE;
  _resumeAs = POMO_FOCUS;
  _phase = PHASE_FOCUS;
  _next = PHASE_FOCUS;
  _left = _cfg.focusMs;
  _last = 0;
  _completed = 0;
  _alert = false;
}

void Pomodoro::setConfig(const PomoConfig &cfg) {
  if (_state != POMO_IDLE) return;
  _cfg = cfg;
  _left = _cfg.focusMs;
}

void Pomodoro::begin(PomoPhase p, uint32_t now) {
  _phase = p;
  _next = p;
  _left = lengthOf(p);
  _last = now;
  _state = p == PHASE_FOCUS ? POMO_FOCUS : POMO_BREAK;
}

void Pomodoro::finish() {
  _left = 0;
  if (_phase == PHASE_FOCUS) {
    if (_completed < _cfg.sessions) _completed++;
    _next = _completed >= _cfg.sessions ? PHASE_LONG : PHASE_SHORT;
  } else {
    if (_phase == PHASE_LONG) _completed = 0;  // the set is over
    _next = PHASE_FOCUS;
  }
  _state = POMO_DONE;
  _alert = true;
}

void Pomodoro::tick(uint32_t now) {
  if (_state != POMO_FOCUS && _state != POMO_BREAK) return;
  const uint32_t elapsed = now - _last;  // unsigned: correct across a millis() wrap
  _last = now;
  if (elapsed >= _left) finish();
  else _left -= elapsed;
}

void Pomodoro::longPress(uint32_t now) {
  switch (_state) {
    case POMO_IDLE:
      begin(PHASE_FOCUS, now);
      break;
    case POMO_FOCUS:
    case POMO_BREAK:
      tick(now);  // bring the clock up to date first: the phase may already be over
      if (_state == POMO_FOCUS || _state == POMO_BREAK) {
        _resumeAs = _state;
        _state = POMO_PAUSED;
      }
      break;
    case POMO_PAUSED:
      _state = _resumeAs;
      _last = now;
      break;
    case POMO_DONE:
      begin(_next, now);
      break;
  }
}

bool Pomodoro::takeAlert() {
  const bool a = _alert;
  _alert = false;
  return a;
}

PomoView Pomodoro::view() const {
  PomoView v;
  v.state = _state;
  v.phase = _phase;
  v.next = _next;
  v.leftMs = _left;
  const uint32_t len = lengthOf(_phase);
  v.fraction = len ? (uint8_t)(((uint64_t)_left * 100 + len / 2) / len) : 0;
  v.completed = _completed;
  v.sessions = _cfg.sessions;
  v.displaySec = (_left + 999) / 1000;
  return v;
}

void pomoFormatTime(uint32_t sec, char *buf, size_t cap) {
  if (sec > 99 * 60 + 59) sec = 99 * 60 + 59;
  snprintf(buf, cap, "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}
