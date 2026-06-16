/**
 * main.h
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

void Error_Handler(void);

/* LED stanu (LD2 = PA5) */
#define LED_GPIO_PORT   GPIOA
#define LED_PIN         GPIO_PIN_5

#ifdef __cplusplus
}
#endif

#endif /* MAIN_H */
