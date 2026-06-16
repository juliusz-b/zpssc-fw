/**
 * main.c - spiecie modulow i super-petla
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * main.c jedynie laczy moduly i prowadzi super-petle. Wybor trybu jest
 * kompilacyjny przez OP_MODE: MODE_SPECTRUM buduje analizator widma (laser CW,
 * sweep DAC, odczyt mocy odbitej), pozostale wartosci buduja tor CDM
 * (DIRECT/SCAN/ETS z modulacja kodem i korelacja).
 */
#include "main.h"
#include "config.h"
#include "board.h"
#include "tuning.h"
#include "acquisition.h"
#include "comms.h"
#if OP_MODE == MODE_SPECTRUM
#include "spectrum.h"
#else
#include "code_gen.h"
#include "modulator.h"
#include "correlation.h"
#endif

/* wspolne dla obu trybow */
static int g_stream;
void app_set_stream(int on) { g_stream = on ? 1 : 0; }

#if OP_MODE == MODE_SPECTRUM
/* ===================== tryb widmowy (analizator) ===================== */
static spec_result_t    g_spec;
static volatile uint8_t g_spec_request;

void app_request_spec(void)
{
  acquisition_start();
  g_spec_request = 1;
}

void app_print_status(void)
{
  comms_print("STATUS mode=SPECTRUM points=");
  comms_print_u32(SPEC_POINTS);
  comms_print(" range=");
  comms_print_u32(SPEC_LEVEL_MIN);
  comms_print("..");
  comms_print_u32(SPEC_LEVEL_MAX);
  comms_print(" adc_hz=");
  comms_print_u32(acquisition_get_rate());
  comms_print(" laser_pa7=");
  comms_print_u32(SPEC_LASER_PA7);
  comms_print("\r\n");
}

/* Wypis przebiegu: naglowek + CSV mocy w kolejnosci k=0..N-1 (kazda probka to
   ten sam poziom DAC = ta sama dlugosc fali wzgledem sweepa) + wykryte piki. */
static void app_spec_report(const spec_result_t *r)
{
  comms_print("SPEC points=");
  comms_print_u32(r->n_points);
  comms_print(" min=");
  comms_print_u32(SPEC_LEVEL_MIN);
  comms_print(" max=");
  comms_print_u32(SPEC_LEVEL_MAX);
  comms_print(" peaks=");
  comms_print_u32(r->n_peaks);
  comms_print("\r\n");
  for (uint16_t i = 0; i < r->n_points; i++) {
    comms_print_u32(r->trace[i]);
    comms_print((i + 1u < r->n_points) ? "," : "\r\n");
  }
  for (uint8_t p = 0; p < r->n_peaks; p++) {
    comms_print("P ");
    comms_print_u32(r->peaks[p].level);
    comms_print(" ");
    comms_print_u32(r->peaks[p].power);
    comms_print("\r\n");
  }
  comms_print("SPEC end\r\n");
}

int main(void)
{
  HAL_Init();
  board_clock_config();
  board_gpio_init();

  comms_init();
  tuning_init();
  acquisition_init();
  spectrum_init();

  comms_print("\r\nzpssc-fw SPECTRUM start (HELP po liste komend)\r\n");
  app_print_status();
  acquisition_start();

  while (1) {
    comms_task();
    if (g_spec_request) {
      spectrum_sweep(&g_spec);
      app_spec_report(&g_spec);
      g_spec_request = g_stream;   /* STREAM ON -> ciagly sweep */
    }
  }
}

#else /* ===================== tryb CDM (DIRECT / SCAN / ETS) ============= */
/* ----------------------- stan aplikacji ----------------------- */
static code_t        g_code;
static corr_result_t g_last;
static uint8_t       g_have_result;
static int           g_mode    = OP_MODE;
static uint8_t       g_running;
static int           g_code_type = CODE_TYPE;
static uint32_t      g_chip_rate;

/* bank kodow do trybu SCAN: referencja Q15 na pasmo dlugosci fali */
static int16_t          g_bank_ref[CODE_BANK_SIZE][CODE_LENGTH];
static uint8_t          g_bank_built;
static volatile uint8_t g_scan_request;

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

/* Buduje bank N kodow Gold (po jednym na pasmo dlugosci fali). */
static void app_build_bank(void)
{
  static code_t tmp;
  g_bank_built = 0;
  for (uint8_t k = 0; k < CODE_BANK_SIZE; k++) {
    if (code_gen_gold_member(CODE_LENGTH, k, &tmp) != 0) {
      return;   /* CODE_LENGTH bez pary Gold -> bank niedostepny */
    }
    for (uint16_t i = 0; i < CODE_LENGTH; i++) {
      g_bank_ref[k][i] = tmp.ref_q15[i];
    }
  }
  g_bank_built = 1;
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
  } else if (mode == MODE_SCAN) {
    acquisition_start();              /* okna ADC potrzebne w skanie */
  } else {                           /* MODE_DIRECT */
    app_rebuild_code(g_code_type);   /* przywroc pojedynczy kod */
  }
}

static const char *code_type_name(int t)
{
  return (t == CODE_MSEQ) ? "MSEQ" : (t == CODE_GOLD) ? "GOLD" : "KASAMI";
}

void app_print_status(void)
{
  comms_print("STATUS run=");
  comms_print_u32(g_running);
  comms_print(" mode=");
  comms_print(g_mode == MODE_ETS ? "ETS" : (g_mode == MODE_SCAN ? "SCAN" : "DIRECT"));
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

/* ----------------------- tryb SCAN ----------------------- */
/* Czeka na swieze okno ADC po zmianie poziomu/kodu (odrzuca jedno przejsciowe). */
static const uint16_t *take_fresh_window(uint16_t *len)
{
  uint32_t guard = 0;
  while (!acquisition_window_ready() && ++guard < 4000000UL) { }
  (void)acquisition_take_window(len);   /* odrzuc okno przejsciowe */
  guard = 0;
  while (!acquisition_window_ready() && ++guard < 4000000UL) { }
  return acquisition_take_window(len);
}

void app_request_scan(void)
{
  if (!g_bank_built) {
    comms_print("ERR bank niedostepny (CODE_LENGTH bez pary Gold?)\r\n");
    return;
  }
  g_mode = MODE_SCAN;
  acquisition_start();
  g_scan_request = 1;
}

/* Jeden pelny przebieg: N poziomow napiecia, na kazdym swoj kod, korelacja
   z calym bankiem. Przekatna (kod k przy poziomie k) to sygnal z danego
   pasma, maksimum z pozostalych kodow to wskaznik przesluchu. */
static void app_scan_once(void)
{
  corr_result_t res, diag;

  comms_print("SCAN bands=");
  comms_print_u32(CODE_BANK_SIZE);
  comms_print("\r\n");

  for (uint8_t k = 0; k < CODE_BANK_SIZE; k++) {
    uint16_t lvl = (uint16_t)(SCAN_LEVEL_MIN +
        (uint32_t)k * (SCAN_LEVEL_MAX - SCAN_LEVEL_MIN) / (CODE_BANK_SIZE - 1u));
    tuning_set_level(lvl);
    code_gen_gold_member(CODE_LENGTH, k, &g_code);
    modulator_stop();
    modulator_set_code(&g_code);
    modulator_start();
    HAL_Delay(SCAN_SETTLE_MS);

    uint16_t len;
    const uint16_t *w = take_fresh_window(&len);

    int32_t xtalk = 0;
    diag.n_peaks = 0;
    diag.max_amp = 0;
    for (uint8_t j = 0; j < CODE_BANK_SIZE; j++) {
      correlation_set_reference_q15(g_bank_ref[j], CODE_LENGTH);
      correlation_run(w, len, &res);
      if (j == k) {
        diag = res;                  /* sygnal z tego pasma */
      } else if (res.max_amp > xtalk) {
        xtalk = res.max_amp;         /* maks. przesluch z innych kodow */
      }
    }

    comms_print("B ");
    comms_print_u32(k);
    comms_print(" lvl=");
    comms_print_u32(lvl);
    comms_print(" peaks=");
    comms_print_u32(diag.n_peaks);
    for (uint8_t p = 0; p < diag.n_peaks; p++) {
      comms_print(" |");
      comms_print_u32(diag.peaks[p].lag);
      comms_print(",");
      comms_print_i32(diag.peaks[p].amp);
    }
    comms_print(" xtalk=");
    comms_print_i32(xtalk);
    comms_print("\r\n");
  }
  comms_print("SCAN end\r\n");
}

/* ----------------------- petla glowna (CDM) ----------------------- */
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

  /* zbudowanie kodu, banku kodow, ustawienie chip rate, gotowosc */
  app_rebuild_code(CODE_TYPE);
  app_build_bank();
  app_set_chip_rate(CHIP_RATE_HZ);

  comms_print("\r\nzpssc-fw start (HELP po liste komend)\r\n");
  app_print_status();

  /* automatyczny start emisji i akwizycji */
  app_start();

  while (1) {
    comms_task();

    if (g_mode == MODE_SCAN) {
      if (g_scan_request) {
        app_scan_once();
        g_scan_request = g_stream;   /* STREAM ON -> zapetlony skan */
      }
    } else if (g_running && g_mode == MODE_DIRECT && acquisition_window_ready()) {
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
#endif /* OP_MODE == MODE_SPECTRUM */

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
