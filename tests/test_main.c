/* Runner testow hostowych. */
#include "test.h"

int g_checks = 0;
int g_fails  = 0;

/* main.h deklaruje Error_Handler; firmware go definiuje, tu zaslepka. */
void Error_Handler(void) { }

void test_code_gen(void);
void test_correlation(void);

int main(void)
{
  RUN(test_code_gen);
  RUN(test_correlation);
  printf("\n%d sprawdzen, %d bledow\n", g_checks, g_fails);
  return g_fails ? 1 : 0;
}
