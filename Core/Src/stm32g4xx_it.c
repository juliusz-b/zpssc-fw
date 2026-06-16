/**
 * stm32g4xx_it.c - obsluga przerwan rdzenia i peryferiow
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#include "main.h"
#include "stm32g4xx_it.h"
#include "acquisition.h"
#include "comms.h"

/* ----------------------- wyjatki rdzenia ----------------------- */
void NMI_Handler(void)
{
  while (1) { }
}

void HardFault_Handler(void)
{
  while (1) { }
}

void MemManage_Handler(void)
{
  while (1) { }
}

void BusFault_Handler(void)
{
  while (1) { }
}

void UsageFault_Handler(void)
{
  while (1) { }
}

void SVC_Handler(void) { }

void DebugMon_Handler(void) { }

void PendSV_Handler(void) { }

void SysTick_Handler(void)
{
  HAL_IncTick();
}

/* ----------------------- peryferia ----------------------- */
void DMA1_Channel1_IRQHandler(void)   /* ADC1 przez DMA */
{
  HAL_DMA_IRQHandler(&hdma_adc1);
}

void USART2_IRQHandler(void)          /* komendy / telemetria */
{
  HAL_UART_IRQHandler(&huart2);
}
