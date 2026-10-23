#pragma once
// The connection parameters the cube asks the Mac for once a link is secured
// (net_ble.cpp). Pure, so the host test can pin them against Apple's rules.
//
// macOS keeps the link at a short interval, which suits pairing and a burst of
// writes. After that the cube hears from the Mac only once every 5 s, and its
// radio still wakes for every connection event. A longer interval plus some
// peripheral latency lets it skip most of them while it has nothing to send.
// A write from the Mac then waits at most maxItvl x (latency + 1) to start,
// 315 ms here. The cube's own notifications (acks, send-now, volume) go out
// at the next event either way.
//
// Units are the Bluetooth spec's: intervals in 1.25 ms, the supervision
// timeout in 10 ms.

#include <stdint.h>

struct BleConnParams {
  uint16_t minItvl, maxItvl, latency, timeout;
};

constexpr BleConnParams BLE_IDLE_PARAMS{24, 36, 6, 400};  // 30..45 ms, skip up to 6 events, 4 s

// The iPhone's link while it streams map frames (one every ~200 ms, several
// writes each): short interval and no latency, so a frame starts at the next
// connection event and lands well inside its 200 ms. The Mac's link keeps
// BLE_IDLE_PARAMS.
constexpr BleConnParams BLE_NAV_PARAMS{12, 24, 0, 400};  // 15..30 ms, no latency, 4 s

// Apple's Accessory Design Guidelines, Bluetooth "Connection Parameters". A
// request that breaks any of these is rejected, and the link keeps the Mac's
// own choice. One return per function: the board builds as C++11.
namespace ble_conn_detail {
constexpr bool accepts(uint32_t minUs, uint32_t maxUs, uint32_t latency, uint32_t timeoutUs,
                       uint32_t spanUs /* longest the peripheral may stay silent */) {
  return minUs >= 15000 && minUs % 15000 == 0 &&
         (maxUs >= minUs + 15000 || (minUs == 15000 && maxUs == 15000)) &&
         latency <= 30 && spanUs <= 2000000 &&
         timeoutUs >= 2000000 && timeoutUs <= 6000000 && spanUs * 3 < timeoutUs;
}
}  // namespace ble_conn_detail

constexpr bool bleAppleAccepts(const BleConnParams &p) {
  return ble_conn_detail::accepts(p.minItvl * 1250u, p.maxItvl * 1250u, p.latency, p.timeout * 10000u,
                                  p.maxItvl * 1250u * (p.latency + 1u));
}

static_assert(bleAppleAccepts(BLE_IDLE_PARAMS), "macOS would reject BLE_IDLE_PARAMS");
static_assert(bleAppleAccepts(BLE_NAV_PARAMS), "iOS would reject BLE_NAV_PARAMS");
