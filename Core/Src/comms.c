/**
 * comms.c
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#include "comms.h"
#include "config.h"
#include "tuning.h"
#include <string.h>
#include <stdlib.h>

/* funkcje koordynujace z main.c */
extern void        app_rebuild_code(int type);
extern int         app_set_chip_rate(uint32_t chip_hz);
extern void        app_start(void);
extern void        app_stop(void);
extern void        app_set_mode(int mode);
extern void        app_set_stream(int on);
extern void        app_print_status(void);
extern void        app_print_last(void);

UART_HandleTypeDef huart2;

#define RX_LINE_MAX 96
static char    rx_line[RX_LINE_MAX];
static volatile uint16_t rx_idx;
static volatile uint8_t  rx_ready;
static uint8_t rx_byte;

/* ----------------------- wyjscie tekstowe ----------------------- */
void comms_print(const char *s)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

void comms_print_u32(uint32_t v)
{
  char b[11];
  int i = 10;
  b[i--] = '\0';
  if (v == 0u) {
    b[i--] = '0';
  }
  while (v > 0u && i >= 0) {
    b[i--] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  comms_print(&b[i + 1]);
}

void comms_print_i32(int32_t v)
{
  if (v < 0) {
    comms_print("-");
    comms_print_u32((uint32_t)(-v));
  } else {
    comms_print_u32((uint32_t)v);
  }
}

/* ----------------------- inicjalizacja UART --------------------- */
void comms_init(void)
{
  huart2.Instance          = USART2;
  huart2.Init.BaudRate     = UART_BAUD;
  huart2.Init.WordLength   = UART_WORDLENGTH_8B;
  huart2.Init.StopBits     = UART_STOPBITS_1;
  huart2.Init.Parity       = UART_PARITY_NONE;
  huart2.Init.Mode         = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK) {
    Error_Handler();
  }
  rx_idx   = 0;
  rx_ready = 0;
  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

/* ----------------------- parser komend -------------------------- */
static void to_upper(char *s)
{
  for (; *s; s++) {
    if (*s >= 'a' && *s <= 'z') {
      *s = (char)(*s - 32);
    }
  }
}

static void cmd_help(void)
{
  comms_print(
    "Komendy:\r\n"
    " PING | ID | STATUS | HELP\r\n"
    " START | STOP\r\n"
    " LVL <0-4095>\r\n"
    " SWEEP OFF | SWEEP <okres_ms> <amp> [TRI|SAW]\r\n"
    " RATE <chip_hz>\r\n"
    " CODE <MSEQ|GOLD|KASAMI>\r\n"
    " LEN <127|255|511>   (kompilacyjnie)\r\n"
    " MODE <DIRECT|ETS>\r\n"
    " CORR | STREAM <ON|OFF>\r\n");
}

static void handle_line(char *line)
{
  to_upper(line);
  char *tok = strtok(line, " \t");
  if (tok == NULL) {
    return;
  }

  if (strcmp(tok, "PING") == 0) {
    comms_print("OK\r\n");
  }
  else if (strcmp(tok, "ID") == 0 || strcmp(tok, "ID?") == 0) {
    comms_print("zpssc-fw 0.1 STM32G431RB engine=");
    comms_print(correlation_engine_name());
    comms_print("\r\n");
  }
  else if (strcmp(tok, "HELP") == 0) {
    cmd_help();
  }
  else if (strcmp(tok, "STATUS") == 0) {
    app_print_status();
  }
  else if (strcmp(tok, "START") == 0) {
    app_start();
    comms_print("OK\r\n");
  }
  else if (strcmp(tok, "STOP") == 0) {
    app_stop();
    comms_print("OK\r\n");
  }
  else if (strcmp(tok, "LVL") == 0) {
    char *a = strtok(NULL, " \t");
    if (a) {
      tuning_set_level((uint16_t)strtoul(a, NULL, 10));
      comms_print("OK\r\n");
    } else {
      comms_print("ERR arg\r\n");
    }
  }
  else if (strcmp(tok, "SWEEP") == 0) {
    char *a = strtok(NULL, " \t");
    if (a && strcmp(a, "OFF") == 0) {
      tuning_sweep_stop();
      comms_print("OK\r\n");
    } else if (a) {
      char *b = strtok(NULL, " \t");
      char *c = strtok(NULL, " \t");
      uint32_t per = strtoul(a, NULL, 10);
      uint16_t amp = b ? (uint16_t)strtoul(b, NULL, 10) : 2048u;
      uint8_t shape = (c && strcmp(c, "SAW") == 0) ? TUNING_SHAPE_SAW
                                                   : TUNING_SHAPE_TRIANGLE;
      comms_print(tuning_sweep_start(per, amp, shape) == 0 ? "OK\r\n" : "ERR\r\n");
    } else {
      comms_print("ERR arg\r\n");
    }
  }
  else if (strcmp(tok, "RATE") == 0) {
    char *a = strtok(NULL, " \t");
    if (a) {
      app_set_chip_rate(strtoul(a, NULL, 10));
      comms_print("OK\r\n");
    } else {
      comms_print("ERR arg\r\n");
    }
  }
  else if (strcmp(tok, "CODE") == 0) {
    char *a = strtok(NULL, " \t");
    if (a && strcmp(a, "MSEQ") == 0)        { app_rebuild_code(CODE_MSEQ);   comms_print("OK\r\n"); }
    else if (a && strcmp(a, "GOLD") == 0)   { app_rebuild_code(CODE_GOLD);   comms_print("OK\r\n"); }
    else if (a && strcmp(a, "KASAMI") == 0) { app_rebuild_code(CODE_KASAMI); comms_print("OK\r\n"); }
    else                                    { comms_print("ERR arg\r\n"); }
  }
  else if (strcmp(tok, "LEN") == 0) {
    char *a = strtok(NULL, " \t");
    uint32_t n = a ? strtoul(a, NULL, 10) : 0u;
    if (n == CODE_LENGTH) {
      comms_print("OK\r\n");
    } else {
      comms_print("ERR dlugosc kompilacyjna (config.h CODE_LENGTH)\r\n");
    }
  }
  else if (strcmp(tok, "MODE") == 0) {
    char *a = strtok(NULL, " \t");
    if (a && strcmp(a, "DIRECT") == 0)   { app_set_mode(MODE_DIRECT); comms_print("OK\r\n"); }
    else if (a && strcmp(a, "ETS") == 0) { app_set_mode(MODE_ETS);    comms_print("OK\r\n"); }
    else                                 { comms_print("ERR arg\r\n"); }
  }
  else if (strcmp(tok, "CORR") == 0 || strcmp(tok, "CORR?") == 0) {
    app_print_last();
  }
  else if (strcmp(tok, "STREAM") == 0) {
    char *a = strtok(NULL, " \t");
    app_set_stream(a && strcmp(a, "ON") == 0);
    comms_print("OK\r\n");
  }
  else {
    comms_print("ERR nieznana komenda (HELP)\r\n");
  }
}

void comms_task(void)
{
  if (!rx_ready) {
    return;
  }
  handle_line(rx_line);
  rx_idx   = 0;
  rx_ready = 0;
}

void comms_report_corr(const corr_result_t *res)
{
  comms_print("CORR lags=");
  comms_print_u32(res->n_lags);
  comms_print(" max=");
  comms_print_i32(res->max_amp);
  comms_print(" peaks=");
  comms_print_u32(res->n_peaks);
  comms_print("\r\n");
  for (uint8_t i = 0; i < res->n_peaks; i++) {
    comms_print(" P ");
    comms_print_u32(res->peaks[i].lag);
    comms_print(" ");
    comms_print_i32(res->peaks[i].amp);
    comms_print("\r\n");
  }
}

/* ----------------------- callback RX UART ----------------------- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART2) {
    return;
  }
  char c = (char)rx_byte;
  if (c == '\r' || c == '\n') {
    if (!rx_ready && rx_idx > 0u) {
      rx_line[rx_idx] = '\0';
      rx_ready = 1;
    }
  } else if (!rx_ready && rx_idx < (RX_LINE_MAX - 1u)) {
    rx_line[rx_idx++] = c;
  }
  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
}

/* ----------------------- MSP: USART2 ----------------------- */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef g = {0};
  if (huart->Instance != USART2) {
    return;
  }
  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* PA2 = USART2_TX, PA3 = USART2_RX (AF7) */
  g.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
  g.Mode      = GPIO_MODE_AF_PP;
  g.Pull      = GPIO_PULLUP;
  g.Speed     = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &g);

  HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
}
