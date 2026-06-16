/**
 * spectrum.c
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#include "spectrum.h"
#include "tuning.h"
#include "acquisition.h"

/* Swieze okno ADC po zmianie poziomu (odrzuca jedno przejsciowe). */
static const uint16_t *spec_fresh_window(uint16_t *len)
{
  uint32_t guard = 0;
  while (!acquisition_window_ready() && ++guard < 4000000UL) { }
  (void)acquisition_take_window(len);
  guard = 0;
  while (!acquisition_window_ready() && ++guard < 4000000UL) { }
  return acquisition_take_window(len);
}

static uint16_t window_mean(const uint16_t *w, uint16_t len)
{
  uint32_t s = 0;
  for (uint16_t i = 0; i < len; i++) {
    s += w[i];
  }
  return (uint16_t)(len ? (s / len) : 0u);
}

void spectrum_init(void)
{
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  /* PA7 = linia modulacji lasera. W trybie widmowym laser ma swiecic CW,
     wiec trzymamy staly poziom (domyslnie niski, SPEC_LASER_PA7). Polaryzacja
     i sposob sterowania do dobrania ze sterownikiem lasera. */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, SPEC_LASER_PA7 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  g.Pin   = GPIO_PIN_7;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &g);
}

/* Detekcja pikow odbicia w przebiegu: maksima lokalne nad progiem liczonym
   wzgledem zakresu (min..max) przebiegu. */
static void detect_peaks(spec_result_t *r)
{
  r->n_peaks = 0;
  uint16_t n = r->n_points;
  if (n < 3u) {
    return;
  }
  uint16_t mx = r->trace[0], mn = r->trace[0];
  for (uint16_t i = 1; i < n; i++) {
    if (r->trace[i] > mx) mx = r->trace[i];
    if (r->trace[i] < mn) mn = r->trace[i];
  }
  if (mx <= mn) {
    return;   /* plaski przebieg -> brak pikow */
  }
  uint16_t thr = (uint16_t)(mn + (uint16_t)((float)(mx - mn) * PEAK_THRESH_FRAC));

  for (uint16_t i = 1; i < n - 1u && r->n_peaks < MAX_PEAKS; i++) {
    uint16_t v = r->trace[i];
    if (v >= thr && v >= r->trace[i - 1] && v > r->trace[i + 1]) {
      uint16_t lvl = (uint16_t)(SPEC_LEVEL_MIN +
          (uint32_t)i * (SPEC_LEVEL_MAX - SPEC_LEVEL_MIN) / (n - 1u));
      r->peaks[r->n_peaks].level = lvl;
      r->peaks[r->n_peaks].power = v;
      r->n_peaks++;
    }
  }
}

void spectrum_sweep(spec_result_t *res)
{
  res->n_points = SPEC_POINTS;
  for (uint16_t k = 0; k < SPEC_POINTS; k++) {
    uint16_t lvl = (uint16_t)(SPEC_LEVEL_MIN +
        (uint32_t)k * (SPEC_LEVEL_MAX - SPEC_LEVEL_MIN) / (SPEC_POINTS - 1u));
    tuning_set_level(lvl);
    HAL_Delay(SPEC_SETTLE_MS);
    uint16_t len;
    const uint16_t *w = spec_fresh_window(&len);
    res->trace[k] = window_mean(w, len);
  }
  detect_peaks(res);
}
