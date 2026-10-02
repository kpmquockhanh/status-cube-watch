#pragma once
// Tiny assertion helper for the host-side tests: no framework, one include.
#include <cstdio>

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    g_checks++;                                                              \
    if (!(cond)) {                                                           \
      g_failed++;                                                            \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                        \
  } while (0)

// Prints the summary line and returns the process exit code.
static int checksDone(const char *name) {
  std::printf("%s: %d checks, %d failed\n", name, g_checks, g_failed);
  return g_failed ? 1 : 0;
}
