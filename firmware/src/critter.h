// The pixel critter on each Agents tile, as plain rectangles in sprite cells
// (x -5..20, y -8..10 with a hat or the celebrate hop; the body is x 0..14,
// y 0..7.6). Pure: no Arduino, no LovyanGFX; ui.cpp maps the cells to pixels,
// picks the colours and blends each shape at its alpha. The pose names are the
// bridge's (`Tile.P`).
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class Pose : uint8_t { Idle, Start, Run, Think, Read, Write, Bash, Web, Plan, Delegate, Compact, Wait, Err, Exit, Hungry, Celebrate };

// The bridge's pose name; unknown, empty or null -> Idle (asleep).
Pose poseFromName(const char *p);

// Body: the tile's status colour. Cut: the background showing through the body
// (eyes, mouth). Flag and Accent are fixed colours the renderer picks. The hat
// inks: HatMain / HatShade are the hat's colour pair (hatMainRgb / hatShadeRgb),
// HatHighlight a light tint (hatHighlightRgb), HatDark near-black (hatDarkRgb).
enum class CritterInk : uint8_t { Body, Cut, Flag, Accent, HatMain, HatShade, HatHighlight, HatDark };

struct CritterRect {
  float x, y, w, h;
  CritterInk ink;
  uint8_t alpha;  // 0..255 over what is below; never 0 (such a shape is left out)
};

// A hat: shape 0..12 in agents-deck's order (hard hat, cap, beanie, helmet,
// wizard, beret, chef, crown, headphones, propeller, top hat, cowboy, bandana),
// colour 0..7 (teal, blue, yellow, green, purple, pink, sky, ink). shape -1 =
// no hat.
struct HatId {
  int8_t shape;
  int8_t color;
};

// The tile's "h" (shape * 8 + colour, 0..103); anything else -> no hat.
HatId hatFromCode(int code);

// agents-deck's hat colours (critter_hat_color) as 0xRRGGBB; out of range -> ink.
uint32_t hatMainRgb(int8_t color);
uint32_t hatShadeRgb(int8_t color);
uint32_t hatHighlightRgb();
uint32_t hatDarkRgb();

// The most shapes any pose with any hat needs at any time (critter_test
// measures the worst case and keeps a margin under this).
constexpr size_t CRITTER_MAX_RECTS = 128;

// Shapes for `pose` wearing `hat` at time `ms`, in draw order (later ones over
// earlier); at most `cap` are written; returns the count.
size_t critterShapes(Pose pose, HatId hat, uint32_t ms, CritterRect *out, size_t cap);
