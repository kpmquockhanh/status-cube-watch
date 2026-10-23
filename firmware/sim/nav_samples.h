#pragma once
// Sim-only sample navigation scenes for shots, the BLE stand-in and the bench.
// `turn`, `arrived` and `offroute` decode the golden frames of
// fixtures/nav-frames.txt; the rest are built in code.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cmath>

#include "../src/nav_frame.h"

namespace nav_samples {

inline bool decodeHex(const char *hex, NavScene &out) {
  static uint8_t buf[512];
  size_t n = 0;
  unsigned v;
  int used;
  while (n < sizeof(buf) && sscanf(hex, " %2x%n", &v, &used) == 1) {
    buf[n++] = (uint8_t)v;
    hex += used;
  }
  return navDecode(buf, n, out);
}

inline void clear(NavScene &s) { memset(&s, 0, sizeof(s)); }

inline void setText(char *dst, size_t cap, const char *src) { snprintf(dst, cap, "%s", src); }

inline void addRoute(NavScene &s, int x, int y) {
  if (s.nRoute < NAV_MAX_ROUTE) s.route[s.nRoute++] = NavPoint{(int16_t)x, (int16_t)y};
}

// A polyline street of up to `n` points; returns false when the scene is full.
inline bool addStreet(NavScene &s, uint8_t cls, const NavPoint *pts, uint8_t n) {
  if (s.nStreets >= NAV_MAX_STREETS || s.nStreetPts + n > NAV_MAX_STREET_PTS) return false;
  s.streetCls[s.nStreets] = cls;
  s.streetStart[s.nStreets] = s.nStreetPts;
  s.streetLen[s.nStreets] = n;
  memcpy(&s.streetPts[s.nStreetPts], pts, n * sizeof(NavPoint));
  s.nStreetPts = (uint16_t)(s.nStreetPts + n);
  s.nStreets++;
  return true;
}

// A turn scene on the golden `turn` map, with its own texts.
inline bool base(NavScene &s) {
  return decodeHex(
      "01 01 05 00 00 00 05 31 35 30 20 6d 09 4c c3 aa 20 4c e1 bb a3 69 06 31 32 20 6d 69 6e 03 00 78 00 "
      "c8 00 78 00 5a 00 c8 00 50 00 01 02 02 00 00 96 00 f0 00 a0 00",
      s);
}

// 96 streets, 640 points, a 256-point route: the most a frame can carry. A
// jittered grid of cross streets (deterministic) with a winding route up it,
// every point inside the clip box (nav_frame.h) as a real frame's must be.
inline void busy(NavScene &s) {
  clear(s);
  s.state = NavState::Navigating;
  s.turn = NavTurn::SlightRight;
  setText(s.dist, sizeof(s.dist), "320 m");
  setText(s.street, sizeof(s.street), "Đường Điện Biên Phủ");
  setText(s.eta, sizeof(s.eta), "9 min");
  uint32_t rng = 12345;
  auto rnd = [&](int range) {
    rng = rng * 1664525u + 1013904223u;
    return (int)((rng >> 16) % (uint32_t)range);
  };
  for (int i = 0; i < 96; i++) {
    const uint8_t n = i < 64 ? 7 : 6;  // 64*7 + 32*6 = 640
    const uint8_t cls = i < 6 ? 0 : i < 24 ? 1 : i < 60 ? 2 : 3;
    NavPoint p[7];
    const bool horiz = i % 2 == 0;
    const int lane = -40 + (i / 2) * 320 / 48 + rnd(12);  // -40 .. 284
    for (int k = 0; k < n; k++) {
      const int along = -60 + k * 360 / (n - 1);  // -60 .. 300
      const int off = lane + rnd(18) - 9;
      p[k] = horiz ? NavPoint{(int16_t)along, (int16_t)off} : NavPoint{(int16_t)off, (int16_t)along};
    }
    addStreet(s, cls, p, n);
  }
  for (int i = 0; i < 256; i++) {
    const float t = i / 255.0f;
    addRoute(s, 120 + (int)(46.0f * sinf(t * 9.0f) * t), 225 - (int)(t * 285.0f));  // y 225 .. -60
  }
}

inline bool navSample(const char *name, NavScene &s) {
  if (!strcmp(name, "turn") || !strcmp(name, "lost")) return base(s);
  if (!strcmp(name, "arrived"))
    return decodeHex("01 04 0b 00 00 00 00 0c 42 e1 ba bf 6e 20 54 68 c3 a0 6e 68 00 00 00 00", s);
  if (!strcmp(name, "offroute"))
    return decodeHex("01 03 0c 00 40 01 04 38 30 20 6d 00 00 02 00 c0 ff 30 01 2c 01 c0 ff 00", s);
  if (!strcmp(name, "busy")) {
    busy(s);
    return true;
  }
  if (!strcmp(name, "vn") || !strcmp(name, "long") || !strcmp(name, "reroute") || !strcmp(name, "longeta")) {
    if (!base(s)) return false;
    if (!strcmp(name, "vn")) {
      s.turn = NavTurn::Roundabout;
      s.exit = 2;
      setText(s.street, sizeof(s.street), "Nguyễn Thị Minh Khai");
    } else if (!strcmp(name, "long")) {
      setText(s.street, sizeof(s.street), "Đường Nguyễn Văn Linh nối dài");
    } else if (!strcmp(name, "longeta")) {  // the longest eta a frame carries (23 bytes)
      setText(s.street, sizeof(s.street), "Đường Nguyễn Văn Linh nối dài");
      setText(s.eta, sizeof(s.eta), "1 h 59 min (arr. 23:59)");
    } else {
      s.state = NavState::Rerouting;
    }
    return true;
  }
  return false;
}

}  // namespace nav_samples

using nav_samples::navSample;
