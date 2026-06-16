/**
 * correlation.c
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * Tor: decymacja okna do rozdzielczosci chipa -> usuniecie skladowej stalej
 * i normalizacja do Q15 pelnej skali -> korelacja (filtr dopasowany) ->
 * detekcja pikow. Trzy silniki wybierane w config.h.
 */
#include "correlation.h"
#include <string.h>

#if (CORR_ENGINE == CORR_ENGINE_CMSIS)
#include "arm_math.h"
#endif

/* ------------------------- bufory robocze ------------------------- */
#define CORR_BUF_LEN  (2u * WINDOW_CHIPS)

static int16_t ref_q15[CODE_MAXLEN];     /* kod bipolarny Q15           */
static int16_t ref_rev[CODE_MAXLEN];     /* kod odwrocony (wsp. filtra) */
static uint16_t ref_len;

static int32_t chip_sum[WINDOW_CHIPS];   /* suma probek na chip         */
static int16_t sig_q15[WINDOW_CHIPS];    /* sygnal po normalizacji      */
static int32_t corr[CORR_BUF_LEN];       /* wektor korelacji            */

#if (CORR_ENGINE == CORR_ENGINE_FMAC)
FMAC_HandleTypeDef hfmac;
static int16_t fmac_out[WINDOW_CHIPS];
static uint8_t  fmac_ready;
#endif
#if (CORR_ENGINE == CORR_ENGINE_CMSIS)
static int16_t cmsis_out[CORR_BUF_LEN];
#endif

/* ============================ inicjalizacja ====================== */
void correlation_init(void)
{
#if (CORR_ENGINE == CORR_ENGINE_FMAC)
  __HAL_RCC_FMAC_CLK_ENABLE();
  hfmac.Instance = FMAC;
  if (HAL_FMAC_Init(&hfmac) != HAL_OK) {
    Error_Handler();
  }
  fmac_ready = 0;
#endif
}

void correlation_set_reference_q15(const int16_t *ref, uint16_t len)
{
  ref_len = len;
  for (uint16_t i = 0; i < len; i++) {
    ref_q15[i] = ref[i];
    ref_rev[i] = ref[len - 1u - i];  /* odwrocenie w czasie */
  }

#if (CORR_ENGINE == CORR_ENGINE_FMAC)
  /* Konfiguracja FMAC jako FIR. Pamiec 256 slow: coeff(N)+input(N)+output. */
  FMAC_FilterConfigTypeDef cfg = {0};
  uint16_t n = ref_len;
  uint16_t outsz = (uint16_t)(256u - 2u * n);
  if (outsz < 1u) {
    outsz = 1u;
  }
  cfg.InputBaseAddress  = (uint8_t)n;
  cfg.InputBufferSize   = (uint8_t)n;
  cfg.InputThreshold    = FMAC_THRESHOLD_1;
  cfg.CoeffBaseAddress  = 0;
  cfg.CoeffBufferSize   = (uint8_t)n;
  cfg.OutputBaseAddress = (uint8_t)(2u * n);
  cfg.OutputBufferSize  = (uint8_t)outsz;
  cfg.OutputThreshold   = FMAC_THRESHOLD_1;
  cfg.pCoeffA           = NULL;
  cfg.CoeffASize        = 0;
  cfg.pCoeffB           = ref_rev;
  cfg.CoeffBSize        = (uint8_t)n;
  cfg.InputAccess       = FMAC_BUFFER_ACCESS_POLLING;
  cfg.OutputAccess      = FMAC_BUFFER_ACCESS_POLLING;
  cfg.Clip              = FMAC_CLIP_ENABLED;
  cfg.Filter            = FMAC_FUNC_CONVO_FIR;
  cfg.P                 = (uint8_t)n;   /* liczba wspolczynnikow */
  cfg.Q                 = 0;
  cfg.R                 = 0;
  fmac_ready = (HAL_FMAC_FilterConfig(&hfmac, &cfg) == HAL_OK) ? 1u : 0u;
#endif
}

void correlation_set_reference(const code_t *code)
{
  correlation_set_reference_q15(code->ref_q15, code->length);
}

/* ===================== przygotowanie sygnalu ===================== */
/* Decymacja do chipow, usuniecie DC, normalizacja do Q15 pelnej skali.
   Zwraca liczbe chipow. */
static uint16_t preprocess(const uint16_t *window, uint16_t win_len)
{
  uint16_t chips = win_len / SAMPLES_PER_CHIP;
  if (chips > WINDOW_CHIPS) {
    chips = WINDOW_CHIPS;
  }
  int64_t total = 0;
  for (uint16_t j = 0; j < chips; j++) {
    int32_t s = 0;
    const uint16_t *p = &window[j * SAMPLES_PER_CHIP];
    for (uint16_t k = 0; k < SAMPLES_PER_CHIP; k++) {
      s += p[k];
    }
    chip_sum[j] = s;
    total += s;
  }
  int32_t mean = (int32_t)(total / chips);

  int32_t maxabs = 1;
  for (uint16_t j = 0; j < chips; j++) {
    int32_t c = chip_sum[j] - mean;
    int32_t a = (c < 0) ? -c : c;
    if (a > maxabs) {
      maxabs = a;
    }
  }
  for (uint16_t j = 0; j < chips; j++) {
    int32_t c = chip_sum[j] - mean;
    sig_q15[j] = (int16_t)(((int64_t)c * 32767) / maxabs);
  }
  return chips;
}

/* ===================== silnik: prosta petla C ==================== */
/* Uzywany przez silnik PLAIN oraz jako bezpieczny fallback FMAC. */
#if (CORR_ENGINE != CORR_ENGINE_CMSIS)
__attribute__((section(".RamFunc")))
static uint16_t corr_plain(uint16_t chips)
{
  uint16_t n = ref_len;
  uint16_t nlags = (chips >= n) ? (uint16_t)(chips - n + 1u) : 0u;
  for (uint16_t k = 0; k < nlags; k++) {
    int32_t acc = 0;
    const int16_t *s = &sig_q15[k];
    for (uint16_t i = 0; i < n; i++) {
      acc += ((int32_t)s[i] * (int32_t)ref_q15[i]) >> 15;
    }
    corr[k] = acc;
  }
  return nlags;
}
#endif /* CORR_ENGINE != CMSIS */

#if (CORR_ENGINE == CORR_ENGINE_FMAC)
/* ===================== silnik: sprzetowy FMAC =================== */
static uint16_t corr_fmac(uint16_t chips)
{
  uint16_t n = ref_len;
  uint16_t nlags = (chips >= n) ? (uint16_t)(chips - n + 1u) : 0u;
  if (!fmac_ready || nlags == 0u) {
    return corr_plain(chips);   /* bezpieczny fallback */
  }
  uint16_t out_size = nlags;
  if (HAL_FMAC_FilterStart(&hfmac, fmac_out, &out_size) != HAL_OK) {
    return corr_plain(chips);
  }
  /* strumieniowanie: dosylanie wejscia i oprozanianie wyjscia */
  uint16_t in_remaining = chips;
  int16_t *in_ptr = sig_q15;
  uint32_t guard = 0;
  while (in_remaining > 0u && guard < 8u) {
    uint16_t chunk = in_remaining;
    if (HAL_FMAC_AppendFilterData(&hfmac, in_ptr, &chunk) != HAL_OK) {
      break;
    }
    in_ptr      += chunk;
    in_remaining = (chunk <= in_remaining) ? (uint16_t)(in_remaining - chunk) : 0u;
    HAL_FMAC_PollFilterData(&hfmac, 10u);
    guard++;
  }
  HAL_FMAC_PollFilterData(&hfmac, 50u);
  HAL_FMAC_FilterStop(&hfmac);

  for (uint16_t k = 0; k < nlags; k++) {
    corr[k] = (int32_t)fmac_out[k];
  }
  return nlags;
}
#endif

#if (CORR_ENGINE == CORR_ENGINE_CMSIS)
/* ===================== silnik: CMSIS-DSP ======================== */
static uint16_t corr_cmsis(uint16_t chips)
{
  uint32_t out_len = 2u * ((chips > ref_len) ? chips : ref_len) - 1u;
  if (out_len > CORR_BUF_LEN) {
    out_len = CORR_BUF_LEN;
  }
  arm_correlate_q15(sig_q15, chips, ref_q15, ref_len, cmsis_out);
  for (uint16_t k = 0; k < out_len; k++) {
    corr[k] = (int32_t)cmsis_out[k];
  }
  return (uint16_t)out_len;
}
#endif

/* ===================== detekcja pikow =========================== */
static void detect_peaks(uint16_t nlags, corr_result_t *res)
{
  res->n_lags  = nlags;
  res->n_peaks = 0;
  res->max_amp = 0;
  if (nlags == 0u) {
    return;
  }
  int32_t mx = corr[0];
  for (uint16_t k = 1; k < nlags; k++) {
    if (corr[k] > mx) {
      mx = corr[k];
    }
  }
  res->max_amp = mx;
  if (mx <= 0) {
    return;   /* brak dodatniej korelacji -> brak pikow (bez pseudo-zer) */
  }
  int32_t thr = (int32_t)((float)mx * PEAK_THRESH_FRAC);

  for (uint16_t k = 0; k < nlags && res->n_peaks < MAX_PEAKS; k++) {
    int32_t v  = corr[k];
    int32_t vl = (k > 0) ? corr[k - 1] : (v - 1);
    int32_t vr = (k < nlags - 1u) ? corr[k + 1] : (v - 1);
    if (v >= thr && v >= vl && v > vr) {        /* maksimum lokalne nad progiem */
      res->peaks[res->n_peaks].lag = k;
      res->peaks[res->n_peaks].amp = v;
      res->n_peaks++;
    }
  }
}

/* ============================ wejscie ============================ */
void correlation_run(const uint16_t *window, uint16_t win_len, corr_result_t *res)
{
  uint16_t chips = preprocess(window, win_len);
  uint16_t nlags;
#if (CORR_ENGINE == CORR_ENGINE_FMAC)
  nlags = corr_fmac(chips);
#elif (CORR_ENGINE == CORR_ENGINE_CMSIS)
  nlags = corr_cmsis(chips);
#else
  nlags = corr_plain(chips);
#endif
  detect_peaks(nlags, res);
}

const char *correlation_engine_name(void)
{
#if (CORR_ENGINE == CORR_ENGINE_FMAC)
  return "FMAC";
#elif (CORR_ENGINE == CORR_ENGINE_CMSIS)
  return "CMSIS-DSP";
#else
  return "PLAIN-C";
#endif
}
