#include "check.h"
#include "deck_util.h"

int main() {
  // Final review I2: the Pomodoro card is the last card; if you are on it, a
  // payload that grows or shrinks must leave you on it.
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 0, 1, false) == 0);   // bridge was down: deck of one
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 0, 3, false) == 2);   // bridge came back with 2 cards
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 2, 4, false) == 3);   // payload grew
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 3, 2, false) == 1);   // payload shrank
  // On a payload card: stay put while it exists, else wrap to the first.
  CHECK(deckKeepIndex(DeckSpot::Payload, 1, 4, false) == 1);
  CHECK(deckKeepIndex(DeckSpot::Payload, 2, 3, false) == 0);  // card 2 is now the Pomodoro slot
  CHECK(deckKeepIndex(DeckSpot::Payload, 5, 3, false) == 0);
  CHECK(deckKeepIndex(DeckSpot::Payload, 0, 1, false) == 0);

  // With the Volume card (a Mac is bonded): payload cards, Volume, Pomodoro.
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 1, 4, true) == 3);
  CHECK(deckKeepIndex(DeckSpot::Volume, 0, 4, true) == 2);    // payload arrived: still on Volume
  CHECK(deckKeepIndex(DeckSpot::Volume, 3, 2, true) == 0);    // payload gone: Volume is card 0
  CHECK(deckKeepIndex(DeckSpot::Volume, 2, 3, false) == 2);   // the bond went: to the Pomodoro
  CHECK(deckKeepIndex(DeckSpot::Payload, 1, 4, true) == 1);
  CHECK(deckKeepIndex(DeckSpot::Payload, 2, 4, true) == 0);   // index 2 is the Volume slot now
  CHECK(deckKeepIndex(DeckSpot::Payload, 0, 2, true) == 0);   // no payload cards left
  return checksDone("deck_test");
}
