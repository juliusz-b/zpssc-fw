/**
 * tuning.c
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#include "tuning.h"
#include "config.h"
#ifdef ANALOG_MOD
#include "modulator.h"
#endif

DAC_HandleTypeDef hdac1;
TIM_HandleTypeDef htim6;
DMA_HandleTypeDef hdma_dac1;

static uint16_t sweep_lut[TUNING_SWEEP_POINTS];
static uint8_t  sweeping;

static void dac_config_channel(uint32_t trigger)
{
  DAC_ChannelConfTypeDef sc = {0};
  sc.DAC_HighFrequency           = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
  sc.DAC_DMADoubleDataMode       = DISABLE;
  sc.DAC_SignedFormat            = DISABLE;
  sc.DAC_SampleAndHold           = DAC_SAMPLEANDHOLD_DISABLE;
  sc.DAC_Trigger                 = trigger;
  sc.DAC_Trigger2                = DAC_TRIGGER_NONE;
  sc.DAC_OutputBuffer            = DAC_OUTPUTBUFFER_ENABLE;
  sc.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
  sc.DAC_UserTrimming            = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sc, DAC_CHANNEL_1) != HAL_OK) {
    Error_Handler();
  }
}

void tuning_init(void)
{
  __HAL_RCC_TIM6_CLK_ENABLE();   /* zegar timera wyzwalajacego sweep DAC */
  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK) {
    Error_Handler();
  }
  dac_config_channel(DAC_TRIGGER_NONE);
  tuning_set_level(TUNING_DEFAULT_LEVEL);
}

void tuning_set_level(uint16_t level12)
{
  if (sweeping) {
    tuning_sweep_stop();
  }
  if (level12 > 4095u) {
    level12 = 4095u;
  }
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, level12);
  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
}

static void build_lut(uint16_t amp12, uint8_t shape)
{
  uint32_t n = TUNING_SWEEP_POINTS;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t v;
    if (shape == TUNING_SHAPE_SAW) {
      v = (amp12 * i) / (n - 1);
    } else { /* trojkat */
      uint32_t half = n / 2;
      v = (i < half) ? (amp12 * i) / half
                     : (amp12 * (n - i)) / half;
    }
    sweep_lut[i] = (uint16_t)(v & 0x0FFFu);
  }
}

int tuning_sweep_start(uint32_t period_ms, uint16_t amp12, uint8_t shape)
{
  if (amp12 > 4095u || period_ms == 0u) {
    return -1;
  }
  if (sweeping) {
    tuning_sweep_stop();
  }
  build_lut(amp12, shape);

  /* czestotliwosc probek LUT = points / okres */
  uint32_t sample_hz = (TUNING_SWEEP_POINTS * 1000u) / period_ms;
  if (sample_hz == 0u) {
    sample_hz = 1u;
  }
  uint32_t timclk = HAL_RCC_GetPCLK1Freq(); /* APB1 = 170 MHz */
  uint32_t arr = (timclk / sample_hz);
  if (arr == 0u) {
    arr = 1u;
  }

  htim6.Instance               = TIM6;
  htim6.Init.Prescaler         = 0;
  htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim6.Init.Period            = arr - 1u;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
    return -1;
  }
  TIM_MasterConfigTypeDef mc = {0};
  mc.MasterOutputTrigger = TIM_TRGO_UPDATE;
  mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(&htim6, &mc);

  HAL_DAC_Stop(&hdac1, DAC_CHANNEL_1);
  dac_config_channel(DAC_TRIGGER_T6_TRGO);

  if (HAL_TIM_Base_Start(&htim6) != HAL_OK) {
    return -1;
  }
  if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)sweep_lut,
                        TUNING_SWEEP_POINTS, DAC_ALIGN_12B_R) != HAL_OK) {
    return -1;
  }
  sweeping = 1;
  return 0;
}

void tuning_sweep_stop(void)
{
  if (!sweeping) {
    return;
  }
  HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
  HAL_TIM_Base_Stop(&htim6);
  sweeping = 0;
  dac_config_channel(DAC_TRIGGER_NONE);
}

uint8_t tuning_is_sweeping(void) { return sweeping; }

/* ----------------------- MSP: DAC1 (+ DAC2 opcja) -------------------- */
void HAL_DAC_MspInit(DAC_HandleTypeDef *hdac)
{
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  if (hdac->Instance == DAC1) {
    __HAL_RCC_DAC1_CLK_ENABLE();
    /* PA4 = DAC1_OUT1 (analog) */
    g.Pin  = GPIO_PIN_4;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    hdma_dac1.Instance                 = DMA1_Channel3;
    hdma_dac1.Init.Request             = DMA_REQUEST_DAC1_CHANNEL1;
    hdma_dac1.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_dac1.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_dac1.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_dac1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_dac1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_dac1.Init.Mode                = DMA_CIRCULAR;
    hdma_dac1.Init.Priority            = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_dac1) != HAL_OK) {
      Error_Handler();
    }
    __HAL_LINKDMA(hdac, DMA_Handle1, hdma_dac1);

#ifdef ANALOG_MOD
    /* DAC1 kanal 2 = DAC1_OUT2 (PA5) dla modulacji analogowej.
       PA5 = LD2, wiec w tym trybie dioda stanu jest niedostepna. */
    g.Pin  = GPIO_PIN_5;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);

    hdma_dac1_ch2.Instance                 = DMA1_Channel4;
    hdma_dac1_ch2.Init.Request             = DMA_REQUEST_DAC1_CHANNEL2;
    hdma_dac1_ch2.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_dac1_ch2.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_dac1_ch2.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_dac1_ch2.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_dac1_ch2.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_dac1_ch2.Init.Mode                = DMA_CIRCULAR;
    hdma_dac1_ch2.Init.Priority            = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_dac1_ch2) != HAL_OK) {
      Error_Handler();
    }
    __HAL_LINKDMA(hdac, DMA_Handle2, hdma_dac1_ch2);
#endif
  }
}
