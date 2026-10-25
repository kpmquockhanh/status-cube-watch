// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, firmware/devices/ws169-panel/src/critter.h.
// The hats' pixel art (hat_art.cpp), in sprite cells: the art the critter
// wears, Claude's critter only. Pure data: no Arduino, no LovyanGFX.
#pragma once

#include <stdint.h>

struct HatExtra {
  int8_t x, y;  // absolute cell, not offset by the frame's x
  char key;
};

struct HatFrame {
  int8_t x, top;  // cell of the first row's first character
  uint8_t nrows;
  const char *const *rows;  // a = hat colour, b = shade, c = highlight, d = near-black, '.' = empty
  uint8_t nextras;
  const HatExtra *extras;
};

struct HatArt {
  const char *name;
  uint16_t ms;  // per frame while working
  HatFrame still;
  uint8_t nframes;
  const HatFrame *frames;
};

// In agents-deck's hash order (hats.go Shapes): hard hat, cap, beanie, helmet,
// wizard, beret, chef, crown, headphones, propeller, top hat, cowboy, bandana.
extern const HatArt HAT_ART[];
extern const int HAT_ART_COUNT;
