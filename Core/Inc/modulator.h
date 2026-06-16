/**
 * modulator.h - wyjscie 2: kod cyfrowy na SPI1 MOSI (PA7) + DMA
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * Kod emitowany jest okresowo przez DMA w trybie circular. Bufor zawiera
 * calkowita liczbe okresow kodu dociagnieta do granicy bajtu, dzieki czemu
 * powtarzanie bufora odtwarza dokladnie okres kodu.
 */
#ifndef MODULATOR_H
#define MODULATOR_H

#include "main.h"
#include "code_gen.h"

extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_tx;
#ifdef ANALOG_MOD
/* G431RB nie ma DAC2. Modulacja analogowa korzysta z DAC1_OUT2 (PA5)
   wyzwalanego TIM7. PA5 to rowniez LD2, wiec przy ANALOG_MOD dioda stanu
   jest niedostepna (sygnalizacja LED wylaczona). */
extern TIM_HandleTypeDef htim7;
extern DMA_HandleTypeDef hdma_dac1_ch2;
void modulator_analog_init(void);
void modulator_analog_set_code(const code_t *code);
void modulator_analog_start(void);
void modulator_analog_stop(void);
#endif

void     modulator_init(void);
int      modulator_set_code(const code_t *code);    /* pakuje kod do bufora SPI */
int      modulator_set_chip_rate(uint32_t chip_hz); /* dobiera prescaler SPI    */
void     modulator_start(void);                     /* ciagla emisja okresowa   */
void     modulator_stop(void);
uint32_t modulator_actual_chip_rate(void);          /* rzeczywisty chip rate    */
uint8_t  modulator_is_running(void);

#endif /* MODULATOR_H */
