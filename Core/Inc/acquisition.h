/**
 * acquisition.h - wejscie: ADC1_IN1 (PA0) + TIM2 trigger + DMA double-buffer
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * ADC probkuje na wyzwoleniu TIM2 (rowne odstepy). DMA w trybie circular
 * pracuje na buforze 2*WINDOW_SAMPLES. Przerwanie half-transfer udostepnia
 * pierwsza polowke, transfer-complete druga - to daje podwojne buforowanie.
 */
#ifndef ACQUISITION_H
#define ACQUISITION_H

#include "main.h"
#include "config.h"

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;
extern DMA_HandleTypeDef hdma_adc1;

void     acquisition_init(void);
int      acquisition_set_rate(uint32_t sample_hz);   /* czestotliwosc TIM2 */
uint32_t acquisition_get_rate(void);
void     acquisition_start(void);
void     acquisition_stop(void);

/* Czy czeka gotowe okno (ustawiane w przerwaniu DMA). */
uint8_t  acquisition_window_ready(void);
/* Zwraca wskaznik na gotowe okno i kasuje flage. len = WINDOW_SAMPLES. */
const uint16_t *acquisition_take_window(uint16_t *len);

/* --- Tryb ETS (equivalent time sampling) - zaczatek, do dostrojenia na HW --- */
void acquisition_ets_begin(uint16_t phase_steps, uint16_t periods_per_step);
int  acquisition_ets_collect(uint16_t *out, uint16_t out_len);  /* -1 = TODO HW */

#endif /* ACQUISITION_H */
