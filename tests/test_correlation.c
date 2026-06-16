/* Testy korelacji i detekcji pikow (przez publiczne correlation_run).
   Syntetyczne okno ADC z kodem wstawionym na zadanym lagu. */
#include "test.h"
#include "correlation.h"
#include "code_gen.h"
#include "config.h"

/* okno z jednym kodem na lagu 'lag' (reszta = poziom srodkowy = brak sygnalu) */
static void win_one(uint16_t *w, const code_t *code, int lag)
{
  for (int j = 0; j < (int)WINDOW_CHIPS; j++) {
    int v = 2048;
    int ci = j - lag;
    if (ci >= 0 && ci < code->length) {
      v = code->bits[ci] ? 3000 : 1000;
    }
    for (int s = 0; s < (int)SAMPLES_PER_CHIP; s++) {
      w[j * SAMPLES_PER_CHIP + s] = (uint16_t)v;
    }
  }
}

/* okno z dwoma kopiami kodu (dwie siatki) na lagach l1, l2 */
static void win_two(uint16_t *w, const code_t *code, int l1, int l2)
{
  for (int j = 0; j < (int)WINDOW_CHIPS; j++) {
    int v = 2048;
    int c1 = j - l1, c2 = j - l2;
    if (c1 >= 0 && c1 < code->length) v += code->bits[c1] ? 600 : -600;
    if (c2 >= 0 && c2 < code->length) v += code->bits[c2] ? 600 : -600;
    for (int s = 0; s < (int)SAMPLES_PER_CHIP; s++) {
      w[j * SAMPLES_PER_CHIP + s] = (uint16_t)v;
    }
  }
}

static int lag_of_max(const corr_result_t *r)
{
  for (int i = 0; i < r->n_peaks; i++) {
    if (r->peaks[i].amp == r->max_amp) return r->peaks[i].lag;
  }
  return -1;
}

static int has_lag(const corr_result_t *r, int lag)
{
  for (int i = 0; i < r->n_peaks; i++) {
    if (r->peaks[i].lag == lag) return 1;
  }
  return 0;
}

void test_correlation(void)
{
  static code_t code;
  static uint16_t w[WINDOW_SAMPLES];
  corr_result_t res;

  correlation_init();
  code_gen_build(CODE_MSEQ, 127, &code);
  correlation_set_reference_q15(code.ref_q15, code.length);

  /* pik dokladnie na zadanym lagu */
  win_one(w, &code, 10);
  correlation_run(w, WINDOW_SAMPLES, &res);
  CHECK(res.n_peaks >= 1);
  CHECK_EQ(lag_of_max(&res), 10);

  win_one(w, &code, 30);
  correlation_run(w, WINDOW_SAMPLES, &res);
  CHECK_EQ(lag_of_max(&res), 30);

  /* brak sygnalu -> 0 pikow (regresja na pseudo-pik |64,0|) */
  for (int i = 0; i < (int)WINDOW_SAMPLES; i++) w[i] = 2048;
  correlation_run(w, WINDOW_SAMPLES, &res);
  CHECK_EQ(res.n_peaks, 0);

  /* dwie siatki -> piki na obu lagach */
  win_two(w, &code, 5, 40);
  correlation_run(w, WINDOW_SAMPLES, &res);
  CHECK(res.n_peaks >= 2);
  CHECK(has_lag(&res, 5));
  CHECK(has_lag(&res, 40));
}
