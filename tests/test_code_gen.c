/* Testy generatora kodow: dlugosci, zrownowazenie i autokorelacja
   m-sequence, mapowanie Q15, ograniczona korelacja wzajemna banku Gold. */
#include "test.h"
#include "code_gen.h"

static int ones(const uint8_t *b, uint16_t n)
{
  int c = 0;
  for (uint16_t i = 0; i < n; i++) { c += b[i] ? 1 : 0; }
  return c;
}

static int autocorr_offpeak(const uint8_t *b, uint16_t n, int shift)
{
  int acc = 0;
  for (uint16_t i = 0; i < n; i++) {
    int x = b[i] ? 1 : -1;
    int y = b[(i + shift) % n] ? 1 : -1;
    acc += x * y;
  }
  return acc;
}

static int xcorr_absmax(const uint8_t *a, const uint8_t *b, uint16_t n)
{
  int m = 0;
  for (int s = 0; s < n; s++) {
    int acc = 0;
    for (uint16_t i = 0; i < n; i++) {
      int x = a[i] ? 1 : -1;
      int y = b[(i + s) % n] ? 1 : -1;
      acc += x * y;
    }
    if (acc < 0) { acc = -acc; }
    if (acc > m) { m = acc; }
  }
  return m;
}

void test_code_gen(void)
{
  static code_t c;

  /* dlugosci i odrzucenie bledu */
  CHECK_EQ(code_gen_build(CODE_MSEQ, 127, &c), 0); CHECK_EQ(c.length, 127);
  CHECK_EQ(code_gen_build(CODE_MSEQ, 255, &c), 0); CHECK_EQ(c.length, 255);
  CHECK_EQ(code_gen_build(CODE_MSEQ, 511, &c), 0); CHECK_EQ(c.length, 511);
  CHECK_EQ(code_gen_build(CODE_MSEQ, 200, &c), -1);

  /* zrownowazenie m-sequence: liczba jedynek = (len+1)/2 */
  code_gen_build(CODE_MSEQ, 127, &c); CHECK_EQ(ones(c.bits, 127), 64);
  code_gen_build(CODE_MSEQ, 255, &c); CHECK_EQ(ones(c.bits, 255), 128);
  code_gen_build(CODE_MSEQ, 511, &c); CHECK_EQ(ones(c.bits, 511), 256);

  /* mapowanie ref_q15: 1 -> +32767, 0 -> -32767 */
  code_gen_build(CODE_MSEQ, 127, &c);
  for (int i = 0; i < 127; i++) {
    CHECK_EQ(c.ref_q15[i], c.bits[i] ? 32767 : -32767);
  }

  /* idealna autokorelacja m-sequence: off-peak = -1 (potwierdza maksymalnosc) */
  {
    int bad;
    code_gen_build(CODE_MSEQ, 127, &c);
    bad = 0; for (int s = 1; s < 127; s++) if (autocorr_offpeak(c.bits, 127, s) != -1) bad++;
    CHECK_EQ(bad, 0);
    code_gen_build(CODE_MSEQ, 255, &c);
    bad = 0; for (int s = 1; s < 255; s++) if (autocorr_offpeak(c.bits, 255, s) != -1) bad++;
    CHECK_EQ(bad, 0);
    code_gen_build(CODE_MSEQ, 511, &c);
    bad = 0; for (int s = 1; s < 511; s++) if (autocorr_offpeak(c.bits, 511, s) != -1) bad++;
    CHECK_EQ(bad, 0);
  }

  /* bank Gold-127: czlony rozne i ograniczona korelacja wzajemna t(7)=17 */
  {
    static code_t g[16];
    for (int k = 0; k < 16; k++) {
      CHECK_EQ(code_gen_gold_member(127, (uint16_t)k, &g[k]), 0);
      CHECK_EQ(g[k].length, 127);
    }
    int diff = 0; for (int i = 0; i < 127; i++) diff += (g[0].bits[i] != g[1].bits[i]);
    CHECK(diff > 0);

    int worst = 0;
    for (int a = 0; a < 16; a++) {
      for (int b = a + 1; b < 16; b++) {
        int x = xcorr_absmax(g[a].bits, g[b].bits, 127);
        if (x > worst) worst = x;
      }
    }
    CHECK(worst <= 17);   /* ograniczenie pary preferowanej Gold dla n=7 */
    CHECK(worst >= 1);
  }

  /* Gold nie istnieje dla n=8 (255) */
  {
    static code_t g;
    CHECK_EQ(code_gen_gold_member(255, 0, &g), -1);
  }
}
