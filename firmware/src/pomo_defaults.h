#pragma once
// The compile-time Pomodoro defaults: what a cube with nothing stored in NVS
// uses, and what the editor's RESET returns to. Includes config.h, so only the
// settings layer (and its simulator stand-in) includes this.
#include "config.h"
#include "pomo_settings.h"

// Defaults, so a config.h from before the Pomodoro still builds.
#ifndef POMO_FOCUS_MIN
#define POMO_FOCUS_MIN 25
#endif
#ifndef POMO_BREAK_MIN
#define POMO_BREAK_MIN 5
#endif
#ifndef POMO_LONG_MIN
#define POMO_LONG_MIN 15
#endif
#ifndef POMO_SESSIONS
#define POMO_SESSIONS 4
#endif

static_assert(POMO_FOCUS_MIN >= 1 && POMO_FOCUS_MIN <= 99, "POMO_FOCUS_MIN must be 1..99");
static_assert(POMO_BREAK_MIN >= 1 && POMO_BREAK_MIN <= 99, "POMO_BREAK_MIN must be 1..99");
static_assert(POMO_LONG_MIN >= 1 && POMO_LONG_MIN <= 99, "POMO_LONG_MIN must be 1..99");
static_assert(POMO_SESSIONS >= 1 && POMO_SESSIONS <= 9, "POMO_SESSIONS must be 1..9");

constexpr PomoSettings POMO_DEFAULTS{POMO_FOCUS_MIN, POMO_BREAK_MIN, POMO_LONG_MIN, POMO_SESSIONS};
