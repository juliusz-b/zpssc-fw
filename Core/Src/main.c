/**
 * main.c - spiecie modulow i super-petla
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * main.c jedynie laczy moduly: inicjalizacja peryferiow, zbudowanie kodu,
 * a w petli glownej obsluga komend i przetwarzanie gotowych okien ADC.
 * Logika znajduje sie w modulach (board / code_gen / modulator / tuning /
 * acquisition / correlation / comms).
 */
#include "main.h"
#include "config.h"
#include "board.h"
#include "code_gen.h"
#include "modulator.h"
#include "tuning.h"
#include "acquisition.h"
#include "correlation.h"
#include "comms.h"

/* ----------------------- stan aplikacji ----------------------- */
static code_t        g_code;
static corr_result_t g_last;
static uint8_t       g_have_result;
static int           g_mode    = OP_MODE;
static int           g_stream;
static uint8_t       g_running;
static int           g_code_type = CODE_TYPE;
static uint32_t      g_chip_rate;

/* ----------------------- API dla comms ----------------------- */
void app_rebuild_code(int type)
{
  uint8_t was = g_running;
  if (was) {
    modulator_stop();
  }
  if (code_gen_build((uint8_t)type, CODE_LENGTH, &g_code) != 0) {
    comms_print("ERR generacja kodu\r\n");
    return;
  }
  g_code_type = type;
  modulator_set_code(&g_code);
  correlation_set_reference(&g_code);
  if (was) {
    modulator_start();
  }
}

int app_set_chip_rate(uint32_t chip_hz)
{
  modulator_set_chip_rate(chip_hz);
  g_chip_rate = modulator_actual_chip_rate();
  /* ADC probkuje SAMPLES_PER_CHIP razy na chip */
  acquisition_set_rate((uint32_t)SAMPLES_PER_CHIP * g_chip_rate);
  return 0;
}

void app_start(void)
{
  modulator_start();
  acquisition_start();
  g_running = 1;
}

void app_stop(void)
{
  acquisition_stop();
  modulator_stop();
  g_running = 0;
}

void app_set_mode(int mode)
{
  g_mode = mode;
  if (mode == MODE_ETS) {
    comms_print("MODE ETS: zaczatek, wymaga dostrojenia na sprzecie\r\n");
  }
}

void app_set_stream(int on) { g_stream = on ? 1 : 0; }

static const char *code_type_name(int t)
{
  return (t == CODE_MSEQ) ? "MSEQ" : (t == CODE_GOLD) ? "GOLD" : "KASAMI";
}

void app_print_status(void)
{
  comms_print("STATUS run=");
  comms_print_u32(g_running);
  comms_print(" mode=");
  comms_print(g_mode == MODE_ETS ? "ETS" : "DIRECT");
  comms_print(" code=");
  comms_print(code_type_name(g_code_type));
  comms_print(" len=");
  comms_print_u32(CODE_LENGTH);
  comms_print(" chip_hz=");
  comms_print_u32(g_chip_rate);
  comms_print(" adc_hz=");
  comms_print_u32(acquisition_get_rate());
  comms_print(" engine=");
  comms_print(correlation_engine_name());
  comms_print("\r\n");
}

void app_print_last(void)
{
  if (g_have_result) {
    comms_report_corr(&g_last);
  } else {
    comms_print("CORR brak danych\r\n");
  }
}

/* ----------------------- petla glowna ----------------------- */
int main(void)
{
  HAL_Init();
  board_clock_config();
  board_gpio_init();

  comms_init();
  tuning_init();
  modulator_init();
  acquisition_init();
  correlation_init();

  /* zbudowanie kodu, ustawienie chip rate, gotowosc */
  app_rebuild_code(CODE_TYPE);
  app_set_chip_rate(CHIP_RATE_HZ);

  comms_print("\r\nzpssc-fw start (HELP po liste komend)\r\n");
  app_print_status();

  /* automatyczny start emisji i akwizycji */
  app_start();

  while (1) {
    comms_task();

    if (g_running && g_mode == MODE_DIRECT && acquisition_window_ready()) {
      uint16_t len;
      const uint16_t *w = acquisition_take_window(&len);
      correlation_run(w, len, &g_last);
      g_have_result = 1;
      if (g_last.n_peaks > 0u) {
        board_led_toggle();
      }
      if (g_stream) {
        comms_report_corr(&g_last);
      }
    }
    /* tryb ETS: rekonstrukcja realizowana po stronie acquisition (zaczatek) */
  }
}

/* ----------------------- obsluga bledu ----------------------- */
void Error_Handler(void)
{
  __disable_irq();
  while (1) {
    /* szybkie miganie LED jako sygnal bledu (no-op przy ANALOG_MOD) */
    for (volatile uint32_t d = 0; d < 400000u; d++) {
      __NOP();
    }
    board_led_toggle();
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  Error_Handler();
}
#endif
