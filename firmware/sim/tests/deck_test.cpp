#include "check.h"
#include "deck_util.h"

int main() {
  // Final review I2: the Pomodoro card is the last card; if you are on it, a
  // payload that grows or shrinks must leave you on it.
  CHECK(deckKeepIndex(true, 0, 1) == 0);   // bridge was down: deck of one
  CHECK(deckKeepIndex(true, 0, 3) == 2);   // bridge came back with 2 cards
  CHECK(deckKeepIndex(true, 2, 4) == 3);   // payload grew
  CHECK(deckKeepIndex(true, 3, 2) == 1);   // payload shrank
  // On a payload card: stay put while it exists, else wrap to the first.
  CHECK(deckKeepIndex(false, 1, 4) == 1);
  CHECK(deckKeepIndex(false, 2, 3) == 0);  // card 2 is now the Pomodoro slot
  CHECK(deckKeepIndex(false, 5, 3) == 0);
  CHECK(deckKeepIndex(false, 0, 1) == 0);
  return checksDone("deck_test");
}
