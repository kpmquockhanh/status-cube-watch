#include "check.h"
#include "detail_layout.h"

int main() {
  CHECK(DETAIL_BACK.x == 0 && DETAIL_BACK.y == 30 && DETAIL_BACK.w == 96 && DETAIL_BACK.h == 30);
  CHECK(DETAIL_CRITTER.x == 4 && DETAIL_CRITTER.y == 58 && DETAIL_CRITTER.w == 80 && DETAIL_CRITTER.h == 56);
  CHECK(DETAIL_HIT_BACK == 1 && DETAIL_HIT_CRITTER == 2);

  CHECK(detailHit(10, 40) == DETAIL_HIT_BACK);
  CHECK(detailHit(40, 80) == DETAIL_HIT_CRITTER);
  CHECK(detailHit(200, 200) == 0);

  // Left and top edges are inside, right and bottom edges are not.
  CHECK(detailHit(0, 30) == DETAIL_HIT_BACK);
  CHECK(detailHit(95, 59) == DETAIL_HIT_BACK);
  CHECK(detailHit(96, 40) == 0);
  CHECK(detailHit(10, 29) == 0);
  CHECK(detailHit(90, 60) == 0);  // under the back zone, right of the critter
  // The two zones share rows 58-59 below `< back`: back wins there.
  CHECK(detailHit(4, 58) == DETAIL_HIT_BACK);
  CHECK(detailHit(4, 60) == DETAIL_HIT_CRITTER);
  CHECK(detailHit(83, 113) == DETAIL_HIT_CRITTER);
  CHECK(detailHit(84, 80) == 0);
  CHECK(detailHit(40, 114) == 0);
  CHECK(detailHit(3, 80) == 0);
  CHECK(detailHit(-1, -1) == 0);

  return checksDone("detail_layout");
}
