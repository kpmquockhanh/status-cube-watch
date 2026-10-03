#include <cstring>

#include "age_label.h"
#include "check.h"

namespace {

bool is(bool online, unsigned long sec, const char *want) {
  char buf[8];
  ageLabel(buf, sizeof(buf), online, sec);
  return strcmp(buf, want) == 0;
}

}  // namespace

int main() {
  CHECK(is(false, 5, "OFF"));
  CHECK(is(true, 0, "0s"));
  CHECK(is(true, 59, "59s"));
  CHECK(is(true, 60, "1m"));
  CHECK(is(true, 119, "1m"));  // floored, never rounded up
  CHECK(is(true, 59 * 60 + 59, "59m"));
  // Past an hour the readout switches to hours, so it never outgrows the slot
  // sized for "99m" ("100m" ran into the transport marker).
  CHECK(is(true, 3600, "1h"));
  CHECK(is(true, 99 * 3600 + 3599, "99h"));
  CHECK(is(true, 100 * 3600, "OLD"));
  CHECK(is(true, 0xFFFFFFFFul / 1000, "OLD"));
  return checksDone("age_label_test");
}
