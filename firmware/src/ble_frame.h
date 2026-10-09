#pragma once
// BLE payload framing, shared by net_ble.cpp (firmware) and the host tests.
// Pure: no Arduino, no NimBLE. The wire format is in docs/ble-protocol.md and
// is pinned by firmware/sim/fixtures/ble-frames.txt, which the Mac app's tests
// read too.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr uint8_t BLE_PROTO_VER = 1;
constexpr uint8_t BLE_FW_REV = 7;  // bumped when behaviour the Mac can see changes
constexpr size_t BLE_HDR = 4;      // ver, seq, idx, total
constexpr uint8_t BLE_MAX_CHUNKS = 16;
constexpr size_t BLE_MAX_PAYLOAD = 2048;

constexpr uint8_t BLE_CTRL_SEND_NOW = 0x01;  // cube -> Mac: send the payload now
constexpr uint8_t BLE_CTRL_ACK = 0x02;       // cube -> Mac: [0x02, seq] payload reassembled
constexpr uint8_t BLE_CTRL_SETTINGS = 0x03;  // cube -> Mac: [0x03, SettingsResult] a Settings write was handled
constexpr uint8_t BLE_CTRL_POMO_ENDED = 0x04;  // cube -> Mac: [0x04, ended, next] a Pomodoro phase ended (0 focus, 1 short, 2 long)
constexpr size_t BLE_POMO_ENDED_LEN = 3;
constexpr uint8_t BLE_CTRL_VOLUME = 0x05;  // cube -> Mac: [0x05, level, muted] set the Mac's output (fw_rev 5)
constexpr uint8_t BLE_CTRL_MEDIA = 0x06;   // cube -> Mac: [0x06, key] press a media key (fw_rev 6)
constexpr uint8_t BLE_CTRL_UNLOCK = 0x07;  // cube -> Mac: [0x07] unlock the Mac's screen (fw_rev 7)

// Writes the Control frame for a finished Pomodoro phase into `out`; returns its length.
inline size_t bleEncodePomoEnded(uint8_t ended, uint8_t next, uint8_t out[BLE_POMO_ENDED_LEN]) {
  out[0] = BLE_CTRL_POMO_ENDED;
  out[1] = ended;
  out[2] = next;
  return BLE_POMO_ENDED_LEN;
}

constexpr char BLE_SERVICE_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a01";
constexpr char BLE_PAYLOAD_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a02";
constexpr char BLE_CONTROL_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a03";
constexpr char BLE_INFO_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a04";
constexpr char BLE_SETTINGS_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a05";
constexpr char BLE_VOLUME_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a06";

enum class FrameResult : uint8_t {
  Partial,   // stored; more chunks expected
  Complete,  // json() now holds a whole payload
  Ignored,   // a duplicate chunk or a retransmit of a finished payload
  Bad,       // malformed, out of order or too large; the partial payload is dropped
};

// Reassembles one payload at a time. ATT writes arrive in order, so chunks
// must too: a repeated idx is Ignored, a gap or a mid-payload join is Bad. A
// new seq discards a half-finished payload. json() is only meaningful right
// after feed() returned Complete.
class FrameAssembler {
 public:
  FrameResult feed(const uint8_t *d, size_t len) {
    if (!d || len <= BLE_HDR) return bad();
    const uint8_t ver = d[0], seq = d[1], idx = d[2], total = d[3];
    if (ver != BLE_PROTO_VER || total == 0 || total > BLE_MAX_CHUNKS || idx >= total) return bad();
    if (seq == lastDone_) return FrameResult::Ignored;

    if (!active_ || seq != seq_) {
      if (idx != 0) return bad();  // joined mid-payload
      active_ = true;
      seq_ = seq;
      total_ = total;
      next_ = 0;
      len_ = 0;
    } else {
      if (total != total_) return bad();
      if (idx < next_) return FrameResult::Ignored;
      if (idx > next_) return bad();
    }

    const size_t n = len - BLE_HDR;
    if (len_ + n > BLE_MAX_PAYLOAD) return bad();
    memcpy(buf_ + len_, d + BLE_HDR, n);
    len_ += n;
    next_++;
    if (next_ == total_) {
      buf_[len_] = '\0';
      active_ = false;
      lastDone_ = seq_;
      return FrameResult::Complete;
    }
    return FrameResult::Partial;
  }

  const char *json() const { return buf_; }
  size_t size() const { return len_; }
  uint8_t seq() const { return seq_; }

  // Drops a half-finished payload; keeps the "already finished" seq so a late
  // retransmit is still ignored.
  void reset() {
    active_ = false;
    len_ = 0;
    next_ = 0;
    buf_[0] = '\0';
  }
  // reset() plus forgetting the last finished seq. Call on every new connection,
  // so a restarted helper that reuses a seq is not ignored.
  void clear() {
    reset();
    lastDone_ = -1;
  }

 private:
  FrameResult bad() {
    reset();
    return FrameResult::Bad;
  }

  char buf_[BLE_MAX_PAYLOAD + 1] = {0};
  size_t len_ = 0;
  bool active_ = false;
  uint8_t seq_ = 0, total_ = 0, next_ = 0;
  int lastDone_ = -1;
};
