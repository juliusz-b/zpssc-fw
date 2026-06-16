/**
 * board.h - zegar, mapa pinow i sygnalizacja LED
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * Mapa pinow NUCLEO-G431RB (zweryfikowana: datasheet STM32G431RBT6, UM2505):
 *   PA4  DAC1_OUT1   - Wy1: strojenie HCG (0..3,3 V)        [CN7 / A2]
 *   PA7  SPI1_MOSI   - Wy2: kod cyfrowy (AF5)                [CN5-4 / D11]
 *   PA5  DAC1_OUT2   - Wy2: kod analogowo (opcja ANALOG_MOD) [D13]
 *   PA0  ADC1_IN1    - We:  akwizycja sygnalu z detektora    [CN8-1 / A0]
 *   PA2  USART2_TX   - telemetria (AF7, VCP ST-Link)
 *   PA3  USART2_RX   - komendy    (AF7, VCP ST-Link)
 *   PA5  GPIO out    - LED stanu LD2 (gdy bez ANALOG_MOD)     [D13]
 */
#ifndef BOARD_H
#define BOARD_H

#include "main.h"

void board_clock_config(void);   /* HSI16 + PLL -> 170 MHz */
void board_gpio_init(void);       /* LED stanu */

void board_led_on(void);
void board_led_off(void);
void board_led_toggle(void);

#endif /* BOARD_H */
