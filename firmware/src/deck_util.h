#pragma once
#include <stdint.h>

// Which kind of card the viewer was on before the deck changed size.
enum class DeckSpot : uint8_t { Payload, Volume, Pomodoro };

// The deck is the payload cards, then the Volume card (while a Mac is bonded,
// `hasVolume`), then the Pomodoro card, always last. A viewer on either local
// card stays on it when the payload arrives, grows or shrinks; one on the
// Volume card when it goes away lands on the Pomodoro. A viewer on a payload
// card keeps it while it still exists and otherwise goes to the first.
// Precondition: with hasVolume, newDeckSize >= 2 (uiDeckSize(p, true) always is).
inline uint8_t deckKeepIndex(DeckSpot was, uint8_t index, uint8_t newDeckSize, bool hasVolume) {
  if (was == DeckSpot::Pomodoro) return newDeckSize - 1;
  if (was == DeckSpot::Volume) return hasVolume ? newDeckSize - 2 : newDeckSize - 1;
  const int payloadCards = newDeckSize - 1 - (hasVolume ? 1 : 0);
  return index < payloadCards ? index : 0;
}
