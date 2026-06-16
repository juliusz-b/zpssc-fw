/* Minimalny harness testow (bez zaleznosci). */
#ifndef TEST_H
#define TEST_H
#include <stdio.h>

extern int g_checks;
extern int g_fails;

#define CHECK(cond) do {                                                   \
  g_checks++;                                                              \
  if (!(cond)) { g_fails++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do {                                                \
  g_checks++; long _a = (long)(a), _b = (long)(b);                        \
  if (_a != _b) { g_fails++;                                              \
    printf("  FAIL %s:%d  %s=%ld != %s=%ld\n", __FILE__, __LINE__, #a, _a, #b, _b); } \
} while (0)

#define RUN(fn) do { printf("[RUN] %s\n", #fn); fn(); } while (0)

#endif /* TEST_H */
