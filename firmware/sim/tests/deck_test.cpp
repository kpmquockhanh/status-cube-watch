#include "check.h"
#include "deck_util.h"

int main() {
  // On a payload card: stay put while it exists, else wrap to the first.
  CHECK(deckKeepIndex(DeckSpot::Payload, 0, 1, false) == 0);  // the waiting card, or one card
  CHECK(deckKeepIndex(DeckSpot::Payload, 1, 4, false) == 1);
  CHECK(deckKeepIndex(DeckSpot::Payload, 3, 3, false) == 0);  // payload shrank under the viewer
  CHECK(deckKeepIndex(DeckSpot::Payload, 5, 3, false) == 0);

  // With the Volume card (a Mac is bonded): payload cards, then Volume, last.
  CHECK(deckKeepIndex(DeckSpot::Volume, 1, 4, true) == 3);    // payload arrived: still on Volume
  CHECK(deckKeepIndex(DeckSpot::Volume, 3, 2, true) == 1);    // payload gone: the waiting card, then Volume
  CHECK(deckKeepIndex(DeckSpot::Volume, 2, 3, false) == 0);   // the bond went: to the first card
  CHECK(deckKeepIndex(DeckSpot::Payload, 1, 4, true) == 1);
  CHECK(deckKeepIndex(DeckSpot::Payload, 3, 4, true) == 0);   // index 3 is the Volume slot now
  CHECK(deckKeepIndex(DeckSpot::Payload, 0, 2, true) == 0);   // the waiting card
  return checksDone("deck_test");
}
