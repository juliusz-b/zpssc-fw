/**
 * tuning.h - wyjscie 1: strojenie HCG na DAC1_OUT1 (PA4)
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * Poziom staly albo wolny sweep (trojkat / pila) generowany przez DMA
 * wyzwalany TIM6. Sweep odpowiada powolnemu przestrajaniu dlugosci fali.
 */
#ifndef TUNING_H
#define TUNING_H

#include "main.h"

#define TUNING_SHAPE_TRIANGLE 0
#define TUNING_SHAPE_SAW      1

extern DAC_HandleTypeDef hdac1;
extern TIM_HandleTypeDef htim6;
extern DMA_HandleTypeDef hdma_dac1;

void tuning_init(void);
void tuning_set_level(uint16_t level12);   /* poziom staly 0..4095, wylacza sweep */
int  tuning_sweep_start(uint32_t period_ms, uint16_t amp12, uint8_t shape);
void tuning_sweep_stop(void);
uint8_t tuning_is_sweeping(void);

#endif /* TUNING_H */
