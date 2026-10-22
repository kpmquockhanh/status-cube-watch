#pragma once
#include <stdint.h>

// Which kind of card the viewer was on before the deck changed size.
enum class DeckSpot : uint8_t { Payload, Volume };

// The deck is the payload cards (or one "waiting for data" card while there
// are none), then the Volume card while a Mac is bonded (`hasVolume`), always
// last. A viewer on the Volume card stays on it when the payload arrives, grows
// or shrinks, and lands on the first card when it goes away. A viewer on a
// payload card keeps it while it still exists and otherwise goes to the first.
// Precondition: newDeckSize >= 1, and >= 2 with hasVolume (uiDeckSize always is).
inline uint8_t deckKeepIndex(DeckSpot was, uint8_t index, uint8_t newDeckSize, bool hasVolume) {
  if (was == DeckSpot::Volume) return hasVolume ? newDeckSize - 1 : 0;
  const int payloadCards = newDeckSize - (hasVolume ? 1 : 0);
  return index < payloadCards ? index : 0;
}
