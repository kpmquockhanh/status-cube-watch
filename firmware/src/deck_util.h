#pragma once
#include <stdint.h>

// Where the view should be after the deck changes size. The Pomodoro card is
// always the last one, so a viewer on it stays on it when the bridge payload
// arrives, grows or shrinks; a viewer on a payload card keeps their card while
// it still exists and otherwise goes to the first.
inline uint8_t deckKeepIndex(bool wasOnPomodoro, uint8_t index, uint8_t newDeckSize) {
  if (wasOnPomodoro) return newDeckSize - 1;
  return index < newDeckSize - 1 ? index : 0;
}
