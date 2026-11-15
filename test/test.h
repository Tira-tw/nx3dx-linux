/* A tiny check macro for the unit tests: each failed CHECK prints its line, test_done() prints
 * the count and gives the exit status (0 only if every check passed). */
#ifndef NX3DX_TEST_H
#define NX3DX_TEST_H

#include <stdio.h>

static int test_fails, test_checks;
#define CHECK(c)                                                              \
  do {                                                                        \
    test_checks++;                                                            \
    if (!(c)) test_fails++, printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); \
  } while (0)

static int test_done(const char *what) {
  if (test_fails) {
    printf("%s: %d of %d checks FAILED\n", what, test_fails, test_checks);
    return 1;
  }
  printf("%s: all %d checks passed\n", what, test_checks);
  return 0;
}

#endif
