#include "check.h"
#include "utf8_fit.h"

int main() {
  CHECK(utf8FitBytes("Lê", 2) == 1);   // does not split ê
  CHECK(utf8FitBytes("Lê", 3) == 3);
  CHECK(utf8FitBytes("Lê", 1) == 1);
  CHECK(utf8FitBytes("ợ", 2) == 0);    // 3-byte char
  CHECK(utf8FitBytes("ợ", 3) == 3);
  CHECK(utf8FitBytes("ợ", 5) == 3);
  CHECK(utf8FitBytes("abc", 2) == 2);  // ASCII unchanged
  CHECK(utf8FitBytes("abc", 10) == 3);
  CHECK(utf8FitBytes("abc", 0) == 0);
  CHECK(utf8FitBytes("", 4) == 0);
  CHECK(utf8FitBytes("aợb", 3) == 1);
  CHECK(utf8FitBytes("aợb", 4) == 4);
  return checksDone("utf8_fit");
}
