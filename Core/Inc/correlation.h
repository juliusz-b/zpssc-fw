/**
 * correlation.h - korelacja Q15 (FMAC / CMSIS-DSP / C) + detekcja pikow
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * Korelacja realizowana jako filtr dopasowany: wspolczynniki = kod odwrocony
 * w czasie. Silnik wybierany w config.h (CORR_ENGINE). Wynik to lista pikow
 * (lag w chipach, amplituda).
 */
#ifndef CORRELATION_H
#define CORRELATION_H

#include "main.h"
#include "config.h"
#include "code_gen.h"

#if (CORR_ENGINE == CORR_ENGINE_FMAC)
extern FMAC_HandleTypeDef hfmac;
#endif

typedef struct {
  uint16_t lag;     /* pozycja piku (chip) */
  int32_t  amp;     /* amplituda korelacji */
} corr_peak_t;

typedef struct {
  uint8_t     n_peaks;
  uint16_t    n_lags;       /* dlugosc wektora korelacji */
  int32_t     max_amp;
  corr_peak_t peaks[MAX_PEAKS];
} corr_result_t;

void        correlation_init(void);
void        correlation_set_reference(const code_t *code);
void        correlation_set_reference_q15(const int16_t *ref, uint16_t len);
void        correlation_run(const uint16_t *window, uint16_t win_len, corr_result_t *res);
const char *correlation_engine_name(void);

#endif /* CORRELATION_H */
