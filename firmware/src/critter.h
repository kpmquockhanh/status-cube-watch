// The pixel critter on each Agents tile, as plain rectangles in sprite cells
// (x 0..18, y -4..9). Pure: no Arduino, no LovyanGFX; ui.cpp maps the cells to
// pixels and fills them. The pose names are the bridge's (`Tile.P`).
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class Pose : uint8_t { Idle, Start, Run, Think, Read, Write, Bash, Web, Plan, Delegate, Compact, Wait, Err, Exit, Hungry };

// The bridge's pose name; unknown, empty or null -> Idle (asleep).
Pose poseFromName(const char *p);

// Body: the tile's status colour. Cut: the background showing through the body
// (eyes, mouth). Flag and Accent are fixed colours the renderer picks.
enum class CritterInk : uint8_t { Body, Cut, Flag, Accent };

struct CritterRect {
  float x, y, w, h;
  CritterInk ink;
};

// The most shapes any pose needs at any time.
constexpr size_t CRITTER_MAX_RECTS = 64;

// Shapes for `pose` at time `ms`, in draw order (later ones over earlier);
// at most `cap` are written; returns the count.
size_t critterShapes(Pose pose, uint32_t ms, CritterRect *out, size_t cap);
