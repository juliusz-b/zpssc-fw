/**
 * modulator.c
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#include "modulator.h"
#include <string.h>
#ifdef ANALOG_MOD
#include "tuning.h"   /* hdac1 wspoldzielony: ch1 = strojenie, ch2 = mod. analog. */
#endif

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;
#ifdef ANALOG_MOD
TIM_HandleTypeDef htim7;
DMA_HandleTypeDef hdma_dac1_ch2;
static uint32_t analog_lut[CODE_MAXLEN];   /* slowa: zapis DHR12R1 32-bit */
static uint16_t analog_len;
#endif

/* Maksymalny bufor: 511 chipow * 8 powtorzen / 8 bitow = 511 bajtow przy OSF=1.
   Zapas na wieksze OSF. */
#define MOD_BUF_BYTES   1024U

static uint8_t  mod_buf[MOD_BUF_BYTES];
static uint32_t mod_nbytes;
static uint32_t mod_actual_chip_rate;
static uint8_t  mod_running;

static const uint32_t k_presc_div[8] = {2,4,8,16,32,64,128,256};
static const uint32_t k_presc_hal[8] = {
  SPI_BAUDRATEPRESCALER_2,   SPI_BAUDRATEPRESCALER_4,
  SPI_BAUDRATEPRESCALER_8,   SPI_BAUDRATEPRESCALER_16,
  SPI_BAUDRATEPRESCALER_32,  SPI_BAUDRATEPRESCALER_64,
  SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256
};

static uint32_t gcd_u32(uint32_t a, uint32_t b)
{
  while (b) { uint32_t t = a % b; a = b; b = t; }
  return a;
}

/* Wybiera prescaler SPI dajacy baud najblizszy zadanej czestotliwosci bitowej */
static uint32_t pick_prescaler(uint32_t target_spi_hz, uint32_t *baud_out, uint32_t *hal_out)
{
  uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
  uint32_t best = 0;
  uint32_t best_err = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < 8; i++) {
    uint32_t baud = pclk2 / k_presc_div[i];
    uint32_t err  = (baud > target_spi_hz) ? (baud - target_spi_hz) : (target_spi_hz - baud);
    if (err < best_err) { best_err = err; best = i; }
  }
  *baud_out = pclk2 / k_presc_div[best];
  *hal_out  = k_presc_hal[best];
  return best;
}

void modulator_init(void)
{
  hspi1.Instance               = SPI1;
  hspi1.Init.Mode              = SPI_MODE_MASTER;
  hspi1.Init.Direction         = SPI_DIRECTION_2LINES;  /* MOSI niesie dane */
  hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
  hspi1.Init.NSS               = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial     = 7;
  hspi1.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) {
    Error_Handler();
  }
  modulator_set_chip_rate(CHIP_RATE_HZ);
#ifdef ANALOG_MOD
  modulator_analog_init();
#endif
}

int modulator_set_chip_rate(uint32_t chip_hz)
{
  uint32_t baud, hal;
  uint32_t target = chip_hz * CHIP_OVERSAMPLE;
  pick_prescaler(target, &baud, &hal);

  __HAL_SPI_DISABLE(&hspi1);
  MODIFY_REG(hspi1.Instance->CR1, SPI_CR1_BR_Msk, hal);
  hspi1.Init.BaudRatePrescaler = hal;
  if (mod_running) {
    __HAL_SPI_ENABLE(&hspi1);
  }
  mod_actual_chip_rate = baud / CHIP_OVERSAMPLE;
  return 0;
}

int modulator_set_code(const code_t *code)
{
  uint32_t osf = CHIP_OVERSAMPLE;
  uint32_t bits_per_period = (uint32_t)code->length * osf;
  uint32_t rep   = 8u / gcd_u32(8u, bits_per_period);  /* dociagniecie do bajtu */
  uint32_t bits  = bits_per_period * rep;
  uint32_t nbytes = bits / 8u;
  if (nbytes > MOD_BUF_BYTES) {
    return -1;
  }

  memset(mod_buf, 0, nbytes);
  uint32_t bitidx = 0;
  for (uint32_t r = 0; r < rep; r++) {
    for (uint16_t c = 0; c < code->length; c++) {
      uint8_t chip = code->bits[c] & 1u;
      for (uint32_t k = 0; k < osf; k++) {
        if (chip) {
          mod_buf[bitidx >> 3] |= (uint8_t)(0x80u >> (bitidx & 7u)); /* MSB first */
        }
        bitidx++;
      }
    }
  }
  mod_nbytes = nbytes;
#ifdef ANALOG_MOD
  modulator_analog_set_code(code);
#endif
  return 0;
}

void modulator_start(void)
{
  if (mod_running || mod_nbytes == 0) {
    return;
  }
  /* Ciagla emisja: DMA circular -> SPI DR, zadanie TX DMA, wlaczenie SPI */
  HAL_DMA_Start(&hdma_spi1_tx, (uint32_t)mod_buf,
                (uint32_t)&hspi1.Instance->DR, mod_nbytes);
  SET_BIT(hspi1.Instance->CR2, SPI_CR2_TXDMAEN);
  __HAL_SPI_ENABLE(&hspi1);
  mod_running = 1;
#ifdef ANALOG_MOD
  modulator_analog_start();
#endif
}

void modulator_stop(void)
{
  if (!mod_running) {
    return;
  }
  CLEAR_BIT(hspi1.Instance->CR2, SPI_CR2_TXDMAEN);
  HAL_DMA_Abort(&hdma_spi1_tx);
  __HAL_SPI_DISABLE(&hspi1);
  mod_running = 0;
#ifdef ANALOG_MOD
  modulator_analog_stop();
#endif
}

uint32_t modulator_actual_chip_rate(void) { return mod_actual_chip_rate; }
uint8_t  modulator_is_running(void)       { return mod_running; }

/* ----------------------- MSP: zegar, GPIO, DMA ----------------------- */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
  GPIO_InitTypeDef g = {0};
  if (hspi->Instance != SPI1) {
    return;
  }
  __HAL_RCC_SPI1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* PA7 = SPI1_MOSI (AF5). PA5/SCK celowo nieuzywane (zostaje LED LD2). */
  g.Pin       = GPIO_PIN_7;
  g.Mode      = GPIO_MODE_AF_PP;
  g.Pull      = GPIO_NOPULL;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOA, &g);

  hdma_spi1_tx.Instance                 = DMA1_Channel2;
  hdma_spi1_tx.Init.Request             = DMA_REQUEST_SPI1_TX;
  hdma_spi1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
  hdma_spi1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_spi1_tx.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_spi1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
  hdma_spi1_tx.Init.Mode                = DMA_CIRCULAR;
  hdma_spi1_tx.Init.Priority            = DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(&hdma_spi1_tx) != HAL_OK) {
    Error_Handler();
  }
  __HAL_LINKDMA(hspi, hdmatx, hdma_spi1_tx);
}

#ifdef ANALOG_MOD
/* ---------- opcjonalna modulacja analogowa: DAC1_OUT2 (PA5) + TIM7 ---------- */
void modulator_analog_init(void)
{
  __HAL_RCC_TIM7_CLK_ENABLE();
  /* DAC1 kanal 2 wyzwalany TIM7 (pin/DMA skonfigurowane w HAL_DAC_MspInit) */
  DAC_ChannelConfTypeDef sc = {0};
  sc.DAC_HighFrequency           = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
  sc.DAC_DMADoubleDataMode       = DISABLE;
  sc.DAC_SignedFormat            = DISABLE;
  sc.DAC_SampleAndHold           = DAC_SAMPLEANDHOLD_DISABLE;
  sc.DAC_Trigger                 = DAC_TRIGGER_T7_TRGO;
  sc.DAC_Trigger2                = DAC_TRIGGER_NONE;
  sc.DAC_OutputBuffer            = DAC_OUTPUTBUFFER_ENABLE;
  sc.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
  sc.DAC_UserTrimming            = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sc, DAC_CHANNEL_2) != HAL_OK) {
    Error_Handler();
  }
}

void modulator_analog_set_code(const code_t *code)
{
  analog_len = code->length;
  for (uint16_t i = 0; i < analog_len; i++) {
    analog_lut[i] = code->bits[i] ? 4095u : 0u;   /* poziomy chipow */
  }
}

void modulator_analog_start(void)
{
  uint32_t chip = modulator_actual_chip_rate();
  if (chip == 0u) {
    chip = 1u;
  }
  uint32_t arr = HAL_RCC_GetPCLK1Freq() / chip;
  if (arr == 0u) {
    arr = 1u;
  }
  htim7.Instance               = TIM7;
  htim7.Init.Prescaler         = 0;
  htim7.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim7.Init.Period            = arr - 1u;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim7) != HAL_OK) {
    return;
  }
  TIM_MasterConfigTypeDef mc = {0};
  mc.MasterOutputTrigger = TIM_TRGO_UPDATE;
  mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(&htim7, &mc);
  HAL_TIM_Base_Start(&htim7);
  HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2, (uint32_t *)analog_lut,
                    analog_len, DAC_ALIGN_12B_R);
}

void modulator_analog_stop(void)
{
  HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_2);
  HAL_TIM_Base_Stop(&htim7);
}
#endif /* ANALOG_MOD */
