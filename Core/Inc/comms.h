/**
 * comms.h - telemetria i parser komend ASCII przez USART2 (VCP ST-Link)
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * 921600 8N1. Komendy zakonczone znakiem nowej linii. Lista komend w README
 * oraz w odpowiedzi na HELP.
 */
#ifndef COMMS_H
#define COMMS_H

#include "main.h"
#include "correlation.h"

extern UART_HandleTypeDef huart2;

void comms_init(void);
void comms_task(void);                            /* parser - super-petla */
void comms_report_corr(const corr_result_t *res); /* telemetria wyniku    */
void comms_print(const char *s);
void comms_print_u32(uint32_t v);
void comms_print_i32(int32_t v);

#endif /* COMMS_H */
