/**
 * acquisition.c
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#include "acquisition.h"

ADC_HandleTypeDef hadc1;
TIM_HandleTypeDef htim2;
DMA_HandleTypeDef hdma_adc1;

#define ADC_BUF_LEN  (2u * WINDOW_SAMPLES)

static uint16_t adc_buf[ADC_BUF_LEN];
static volatile const uint16_t *win_ptr;
static volatile uint8_t  win_ready;
static uint32_t adc_sample_hz;
static uint8_t  running;

static void tim2_config(uint32_t sample_hz)
{
  uint32_t timclk = HAL_RCC_GetPCLK1Freq();   /* APB1 timer clk = 170 MHz */
  uint32_t arr = (sample_hz != 0u) ? (timclk / sample_hz) : timclk;
  if (arr == 0u) {
    arr = 1u;
  }
  htim2.Instance               = TIM2;
  htim2.Init.Prescaler         = 0;
  htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim2.Init.Period            = arr - 1u;
  htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK) {
    Error_Handler();
  }
  TIM_MasterConfigTypeDef mc = {0};
  mc.MasterOutputTrigger = TIM_TRGO_UPDATE;   /* TRGO -> wyzwala ADC */
  mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(&htim2, &mc);
  adc_sample_hz = sample_hz;
}

void acquisition_init(void)
{
  ADC_ChannelConfTypeDef sc = {0};

  __HAL_RCC_TIM2_CLK_ENABLE();
  tim2_config((uint32_t)SAMPLES_PER_CHIP * CHIP_RATE_HZ);

  hadc1.Instance                   = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4; /* 170/4 = 42,5 MHz */
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.GainCompensation      = 0;
  hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait      = DISABLE;
  hadc1.Init.ContinuousConvMode    = DISABLE;
  hadc1.Init.NbrOfConversion       = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIG_T2_TRGO;
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
  hadc1.Init.OversamplingMode      = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) {
    Error_Handler();
  }

  sc.Channel      = ADC_CHANNEL_1;       /* PA0 = ADC1_IN1 */
  sc.Rank         = ADC_REGULAR_RANK_1;
  sc.SamplingTime = ADC_SAMPLETIME_24CYCLES_5;
  sc.SingleDiff   = ADC_SINGLE_ENDED;
  sc.OffsetNumber = ADC_OFFSET_NONE;
  sc.Offset       = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sc) != HAL_OK) {
    Error_Handler();
  }

  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
    Error_Handler();
  }
}

int acquisition_set_rate(uint32_t sample_hz)
{
  if (sample_hz == 0u) {
    return -1;
  }
  uint8_t was = running;
  if (running) {
    acquisition_stop();
  }
  tim2_config(sample_hz);
  if (was) {
    acquisition_start();
  }
  return 0;
}

uint32_t acquisition_get_rate(void) { return adc_sample_hz; }

void acquisition_start(void)
{
  if (running) {
    return;
  }
  win_ready = 0;
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_LEN) != HAL_OK) {
    Error_Handler();
  }
  if (HAL_TIM_Base_Start(&htim2) != HAL_OK) {
    Error_Handler();
  }
  running = 1;
}

void acquisition_stop(void)
{
  if (!running) {
    return;
  }
  HAL_TIM_Base_Stop(&htim2);
  HAL_ADC_Stop_DMA(&hadc1);
  running = 0;
}

uint8_t acquisition_window_ready(void) { return win_ready; }

const uint16_t *acquisition_take_window(uint16_t *len)
{
  win_ready = 0;
  if (len) {
    *len = WINDOW_SAMPLES;
  }
  return (const uint16_t *)win_ptr;
}

/* ======================================================================
 * Tryb ETS (equivalent time sampling) - ZACZATEK
 * ----------------------------------------------------------------------
 * Idea: kod jest okresowy (modulator emituje go w petli circular). ADC
 * probkuje raz na okres kodu, a punkt probkowania przesuwa sie o maly krok
 * fazy z kazdym okresem. Po phase_steps okresach rekonstruuje sie przebieg
 * o efektywnym kroku czasowym T_chip / phase_steps, czyli pasmie znacznie
 * powyzej 4 MS/s ADC.
 *
 * Co trzeba dostroic na sprzecie (TODO):
 *  1. Wspolny timer nadrzedny dla zegara chipow (SPI/TIM) i wyzwalania ADC,
 *     albo sztywny zwiazek fazowy przez ITR/TRGO, zeby przesuniecie bylo
 *     deterministyczne (jitter < krok fazy).
 *  2. Programowalne opoznienie wyzwolenia ADC (np. TIMx CCR jako one-pulse)
 *     zmieniane o T_chip/phase_steps na kazdy krok.
 *  3. Kalibracja na oscyloskopie: zmierzyc realny krok fazy i poprawic.
 *  4. Skladanie probek z kolejnych krokow w jeden wektor o dlugosci
 *     phase_steps * (probki na okres) z wlasciwym przeplotem.
 *  5. periods_per_step > 1 do usredniania szumu na danym kroku fazy.
 *
 * Ponizsze funkcje to szkielet - swiadomie nie ruszaja sprzetu, dopoki
 * zwiazek fazowy nie zostanie ustalony pomiarowo.
 * ====================================================================== */
static uint16_t ets_phase_steps;
static uint16_t ets_periods_per_step;

void acquisition_ets_begin(uint16_t phase_steps, uint16_t periods_per_step)
{
  ets_phase_steps      = phase_steps;
  ets_periods_per_step = periods_per_step;
  /* TODO(HW): konfiguracja wspolnego timera i opoznienia wyzwolenia ADC. */
}

int acquisition_ets_collect(uint16_t *out, uint16_t out_len)
{
  (void)out;
  (void)out_len;
  (void)ets_phase_steps;
  (void)ets_periods_per_step;
  /* TODO(HW): zebranie i przeplot probek z kolejnych krokow fazy. */
  return -1;   /* niezrealizowane - wymaga dostrojenia na sprzecie */
}

/* ----------------------- callbacki DMA ADC ----------------------- */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *h)
{
  if (h->Instance == ADC1) {
    win_ptr   = &adc_buf[0];
    win_ready = 1;
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)
{
  if (h->Instance == ADC1) {
    win_ptr   = &adc_buf[WINDOW_SAMPLES];
    win_ready = 1;
  }
}

/* ----------------------- MSP: ADC1 ----------------------- */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
  GPIO_InitTypeDef g = {0};
  if (hadc->Instance != ADC1) {
    return;
  }
  __HAL_RCC_ADC12_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* PA0 = ADC1_IN1 (analog) */
  g.Pin  = GPIO_PIN_0;
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);

  hdma_adc1.Instance                 = DMA1_Channel1;
  hdma_adc1.Init.Request             = DMA_REQUEST_ADC1;
  hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode                = DMA_CIRCULAR;
  hdma_adc1.Init.Priority            = DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) {
    Error_Handler();
  }
  __HAL_LINKDMA(hadc, DMA_Handle, hdma_adc1);

  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}
