#pragma once
// The agent detail screen and its hat picker: which agent is open, when to tell
// the Mac again (Control 0A), and which hat to draw while a pick is on its way
// (Control 0B -> Mac -> bridge -> the next payload). Pure: no Arduino,
// host-tested in sim/tests/detail_view_test.cpp.
//
// It keeps its own copy of the agent id and never a pointer into a payload,
// because main.cpp overwrites the payload in place. Elapsed times are unsigned
// subtractions: a millis() wrap is harmless.
#include <stdint.h>
#include <string.h>

#include "payload.h"

// While the payload's `d` is missing or for another agent, Control 0A goes
// again this often (the Mac may have restarted or missed it).
constexpr uint32_t DETAIL_RESEND_MS = 5000;
// A picked hat is drawn this long, or until the payload shows it.
constexpr uint32_t HAT_PICK_HOLD_MS = 4000;
// A `0a 00` that could not go out is tried again this often, for this long
// after the first try (else the Mac would keep asking for `d` every second).
constexpr uint32_t DETAIL_CLOSE_RETRY_MS = 1000, DETAIL_CLOSE_GIVE_UP_MS = 5000;

class DetailView {
 public:
  // A tap on a tile that is not waiting. Opening the same agent again keeps a
  // pick in flight; another agent forgets it. nullptr or "" closes.
  void open(const char *agent, uint32_t now) {
    (void)now;
    if (!agent || !agent[0]) {
      close();
      return;
    }
    char id[sizeof(agent_)];
    size_t n = 0;
    for (; agent[n] && n < sizeof(id) - 1; n++) id[n] = agent[n];
    id[n] = 0;
    if (strcmp(id, agent_)) picked_ = false;
    memcpy(agent_, id, sizeof(id));
    open_ = true;
    sentOnce_ = false;
    picker_ = false;
    closePending_ = false;  // its own 0A replaces a close still owed
  }

  // Also closes the picker. The caller sends Control `0a 00`.
  void close() {
    open_ = false;
    picker_ = false;
    agent_[0] = 0;
  }

  bool isOpen() const { return open_; }
  // The open agent's id, "" when closed.
  const char *agent() const { return agent_; }

  // True when Control 0A should go now: once right after open, then every
  // DETAIL_RESEND_MS while the payload has no `d` for this agent.
  bool sendDue(const AgentsData &a, uint32_t now) const {
    if (!open_) return false;
    if (!sentOnce_) return true;
    if (a.detail.present && !strcmp(a.detail.id, agent_)) return false;
    return now - sentAt_ >= DETAIL_RESEND_MS;
  }
  void sent(uint32_t now) {
    sentOnce_ = true;
    sentAt_ = now;
  }

  // How the caller's `0a 00` went: a failure is tried again (closeDue) every
  // DETAIL_CLOSE_RETRY_MS until one goes out, a detail opens again, or
  // DETAIL_CLOSE_GIVE_UP_MS after the first failure.
  void closeSent(bool ok, uint32_t now) {
    if (ok) {
      closePending_ = false;
      return;
    }
    if (!closePending_) closeFirstAt_ = now;
    closePending_ = true;
    closeTriedAt_ = now;
  }
  bool closeDue(uint32_t now) const {
    return closePending_ && !open_ && now - closeTriedAt_ >= DETAIL_CLOSE_RETRY_MS &&
           now - closeFirstAt_ < DETAIL_CLOSE_GIVE_UP_MS;
  }

  // A tap on the critter. Only while the detail screen is open.
  void openPicker() { picker_ = open_; }
  void closePicker() { picker_ = false; }
  bool pickerOpen() const { return picker_; }

  // The user chose a hat: 0..103, -1 none, -2 automatic. The caller sends
  // Control 0B; this only remembers what to draw meanwhile.
  void pick(int16_t code, uint32_t now) {
    picked_ = true;
    pick_ = code;
    pickAt_ = now;
  }

  // The hat to draw for the open agent, given its tile's `h` (-1 = none):
  // a fresh code or none pick until the tile shows it or HAT_PICK_HOLD_MS
  // passes. An automatic pick shows the tile's hat (only the bridge knows it).
  int16_t shownHat(int16_t tileHat, uint32_t now) const {
    if (!held(now) || pick_ == PICK_AUTO || tileHat == pick_) return tileHat;
    return pick_;
  }

  // Whether the picker marks "auto" as chosen, given the payload's `ha`: a
  // fresh pick decides (auto or not) for HAT_PICK_HOLD_MS, then `ha` does.
  bool pickedAuto(bool hatAuto, uint32_t now) const {
    if (!held(now)) return hatAuto;
    return pick_ == PICK_AUTO;
  }

 private:
  static constexpr int16_t PICK_AUTO = -2;

  bool held(uint32_t now) const { return picked_ && now - pickAt_ < HAT_PICK_HOLD_MS; }

  char agent_[16] = {0};
  bool open_ = false;
  bool sentOnce_ = false;
  uint32_t sentAt_ = 0;
  bool picker_ = false;
  bool picked_ = false;
  int16_t pick_ = 0;
  uint32_t pickAt_ = 0;
  bool closePending_ = false;
  uint32_t closeFirstAt_ = 0, closeTriedAt_ = 0;
};
