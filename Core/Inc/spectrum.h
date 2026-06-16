/**
 * spectrum.h - tryb widmowy: prosty analizator widma odbiciowego FBG
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * Laser CW (bez modulacji), DAC przestraja dlugosc fali schodkowo po
 * SPEC_POINTS poziomach, ADC mierzy moc odbita na kazdym poziomie. Wynik to
 * przebieg moc(dlugosc fali) plus piki (dlugosci Bragga siatek). Probka k
 * zawsze odpowiada poziomowi k, wiec mapowanie indeks -> dlugosc fali jest
 * deterministyczne miedzy przebiegami.
 */
#ifndef SPECTRUM_H
#define SPECTRUM_H

#include "main.h"
#include "config.h"

typedef struct {
  uint16_t level;   /* poziom DAC piku (proxy dlugosci fali) */
  uint16_t power;   /* moc odbita w piku (ADC, 12-bit)       */
} spec_peak_t;

typedef struct {
  uint16_t    n_points;
  uint16_t    trace[SPEC_POINTS];   /* moc odbita na kolejnych poziomach */
  uint8_t     n_peaks;
  spec_peak_t peaks[MAX_PEAKS];
} spec_result_t;

void spectrum_init(void);
void spectrum_sweep(spec_result_t *res);

#endif /* SPECTRUM_H */
