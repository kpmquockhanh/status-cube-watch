#pragma once
#include <stddef.h>
#include <stdint.h>

// The buzzer's tunes and the sequencer that plays them, minus the hardware:
// MelodyPlayer::update() says which pitch should sound now, and buzzer.cpp
// drives the pin. Pure: no Arduino. The caller passes `now`; elapsed time is
// unsigned subtraction, so a millis() wrap mid-tune is harmless.

struct Note {
  uint16_t hz;  // 0 = rest
  uint16_t ms;
};

enum class Sound : uint8_t {
  FocusDone,  // a focus phase ended: break time
  BreakDone,  // a short or long break ended: back to work
  Attention,  // Claude Code is waiting for you (played from sub-project B)
  Preview,    // the SOUND level changed in the display panel
};

// Starting pitches, tuned by ear on the board. Every note is >= 60 ms, because
// the loop times notes to about one pass (15-16 ms), and every tune is under
// 0.5 s.
constexpr Note MELODY_FOCUS_DONE[] = {{2093, 110}, {2637, 110}, {3136, 110}};  // rising
constexpr Note MELODY_BREAK_DONE[] = {{2637, 100}, {2349, 100}, {3136, 220}};  // ends high, held
constexpr Note MELODY_ATTENTION[] = {{2700, 70}, {0, 70}, {2700, 70}};        // near the loudest pitch
constexpr Note MELODY_PREVIEW[] = {{2700, 90}};

template <size_t N>
inline const Note *melodyOf(const Note (&tune)[N], uint8_t &len) {
  len = (uint8_t)N;
  return tune;
}

inline const Note *melodyFor(Sound s, uint8_t &len) {
  switch (s) {
    case Sound::FocusDone: return melodyOf(MELODY_FOCUS_DONE, len);
    case Sound::BreakDone: return melodyOf(MELODY_BREAK_DONE, len);
    case Sound::Attention: return melodyOf(MELODY_ATTENTION, len);
    case Sound::Preview: return melodyOf(MELODY_PREVIEW, len);
  }
  len = 0;
  return nullptr;
}

// For the [buzz] log line.
inline const char *melodyName(Sound s) {
  switch (s) {
    case Sound::FocusDone: return "focus-done";
    case Sound::BreakDone: return "break-done";
    case Sound::Attention: return "attention";
    case Sound::Preview: return "preview";
  }
  return "?";
}

class MelodyPlayer {
 public:
  // Replaces whatever is playing. Timing starts at the next update(): a
  // play() made after this pass read `now` (the editor's preview) must not
  // lose its first note.
  void play(Sound s) {
    notes_ = melodyFor(s, len_);
    if (!len_) notes_ = nullptr;
    idx_ = 0;
    armed_ = notes_ != nullptr;
  }

  void stop() {
    notes_ = nullptr;
    armed_ = false;
  }

  bool playing() const { return notes_ != nullptr; }

  // The pitch to output at `now`, or 0 for silence (a rest, or nothing
  // playing). A late call skips the notes it missed, so a tune keeps its
  // length.
  uint16_t update(uint32_t now) {
    if (armed_) {
      noteStart_ = now;
      armed_ = false;
    }
    while (notes_) {
      if (now - noteStart_ < notes_[idx_].ms) return notes_[idx_].hz;
      noteStart_ += notes_[idx_].ms;
      if (++idx_ >= len_) notes_ = nullptr;
    }
    return 0;
  }

 private:
  const Note *notes_ = nullptr;
  uint8_t len_ = 0;
  uint8_t idx_ = 0;
  bool armed_ = false;
  uint32_t noteStart_ = 0;
};
