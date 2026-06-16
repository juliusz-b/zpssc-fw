/**
 * stm32g4xx_it.h - deklaracje obslug przerwan
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#ifndef STM32G4xx_IT_H
#define STM32G4xx_IT_H

#ifdef __cplusplus
extern "C" {
#endif

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

/* DMA i peryferia uzywane przez firmware */
void DMA1_Channel1_IRQHandler(void);   /* ADC1   */
void USART2_IRQHandler(void);          /* komendy */

#ifdef __cplusplus
}
#endif

#endif /* STM32G4xx_IT_H */
