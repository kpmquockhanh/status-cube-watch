#pragma once
// Navigation frame decoder. The phone streams ready-to-draw map frames over the
// Nav characteristic (chunked like Payload, reassembled by NavAssembler); this
// turns one reassembled frame into a NavScene. Pure: no Arduino, no NimBLE.
// Wire format: docs/superpowers/plans/2026-10-22-nav-2-cube.md, pinned by
// firmware/sim/fixtures/nav-frames.txt.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ble_frame.h"

enum class NavState : uint8_t { Ended, Navigating, Rerouting, OffRouteNoSignal, Arrived };
enum class NavTurn : uint8_t {
  Straight, SlightLeft, Left, SharpLeft, SlightRight, Right, SharpRight,
  UTurn, KeepLeft, KeepRight, Roundabout, Arrive, Rejoin
};

struct NavPoint {
  int16_t x, y;
};

constexpr uint16_t NAV_MAX_ROUTE = 256;
constexpr uint8_t NAV_MAX_STREETS = 96;
constexpr uint16_t NAV_MAX_STREET_PTS = 640;

// About 4 KB: keep it off small stacks (the NimBLE host task); hold it in a
// static or member.
struct NavScene {
  NavState state;
  NavTurn turn;
  uint8_t exit, dir, flags;
  char dist[16], street[64], eta[24];  // NUL-terminated
  uint16_t nRoute;
  NavPoint route[NAV_MAX_ROUTE];
  uint8_t nStreets;
  uint8_t streetCls[NAV_MAX_STREETS];
  uint16_t streetStart[NAV_MAX_STREETS];
  uint8_t streetLen[NAV_MAX_STREETS];
  uint16_t nStreetPts;
  NavPoint streetPts[NAV_MAX_STREET_PTS];
};

using NavAssembler = ChunkAssembler<BLE_NAV_MAX_CHUNKS, BLE_NAV_MAX_BYTES>;

namespace nav_detail {

struct Reader {
  const uint8_t *p;
  size_t left;
  bool ok;
  uint8_t u8() {
    if (left < 1) {
      ok = false;
      return 0;
    }
    left--;
    return *p++;
  }
  uint16_t u16() {
    const uint16_t lo = u8();
    return (uint16_t)(lo | ((uint16_t)u8() << 8));
  }
  int16_t i16() { return (int16_t)u16(); }
  // Reads a length-prefixed string of at most cap-1 bytes into dst (when given).
  void str(char *dst, size_t cap) {
    const size_t n = u8();
    if (n >= cap || n > left) {
      ok = false;
      return;
    }
    if (dst) {
      memcpy(dst, p, n);
      dst[n] = '\0';
    }
    p += n;
    left -= n;
  }
};

// One parser for both passes: with out == nullptr it only validates and writes
// nothing; with out set it fills it (call it only on a frame already validated).
inline bool parse(const uint8_t *data, size_t len, NavScene *out) {
  if (!data) return false;
  Reader r = {data, len, true};
  if (r.u8() != 1) return false;
  const uint8_t state = r.u8(), turn = r.u8(), exit = r.u8(), dir = r.u8(), flags = r.u8();
  if (!r.ok || state > (uint8_t)NavState::Arrived || turn > (uint8_t)NavTurn::Rejoin) return false;
  if (out) {
    out->state = (NavState)state;
    out->turn = (NavTurn)turn;
    out->exit = exit;
    out->dir = dir;
    out->flags = flags;
  }
  r.str(out ? out->dist : nullptr, sizeof(NavScene::dist));
  r.str(out ? out->street : nullptr, sizeof(NavScene::street));
  r.str(out ? out->eta : nullptr, sizeof(NavScene::eta));
  if (!r.ok) return false;

  const uint16_t nRoute = r.u16();
  if (!r.ok || nRoute > NAV_MAX_ROUTE || r.left < (size_t)nRoute * 4) return false;
  if (out) out->nRoute = nRoute;
  for (uint16_t i = 0; i < nRoute; i++) {
    const int16_t x = r.i16(), y = r.i16();
    if (out) out->route[i] = NavPoint{x, y};
  }

  const uint8_t nStreets = r.u8();
  if (!r.ok || nStreets > NAV_MAX_STREETS) return false;
  if (out) out->nStreets = nStreets;
  uint16_t total = 0;
  for (uint8_t s = 0; s < nStreets; s++) {
    const uint8_t cls = r.u8(), nPts = r.u8();
    if (!r.ok || cls > 3 || nPts < 2 || total + nPts > NAV_MAX_STREET_PTS || r.left < (size_t)nPts * 4) return false;
    if (out) {
      out->streetCls[s] = cls;
      out->streetStart[s] = total;
      out->streetLen[s] = nPts;
    }
    for (uint8_t i = 0; i < nPts; i++) {
      const int16_t x = r.i16(), y = r.i16();
      if (out) out->streetPts[total + i] = NavPoint{x, y};
    }
    total = (uint16_t)(total + nPts);
  }
  if (out) out->nStreetPts = total;
  return r.ok && r.left == 0;  // trailing bytes reject the frame
}

}  // namespace nav_detail

// Decodes one reassembled frame into `out`. Returns false, leaving `out`
// untouched, for anything malformed (wrong version, enum or length out of
// range, truncated body, trailing bytes). Two passes, validate then fill, so
// no NavScene is ever built on the stack.
inline bool navDecode(const uint8_t *data, size_t len, NavScene &out) {
  if (!nav_detail::parse(data, len, nullptr)) return false;
  return nav_detail::parse(data, len, &out);
}
