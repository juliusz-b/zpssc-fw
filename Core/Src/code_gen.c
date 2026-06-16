/**
 * code_gen.c
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * Generatory LFSR (postac Galois, wyjscie = bit najmlodszy). Maski
 * wielomianow zweryfikowane jako pierwotne (okres 2^n-1):
 *   n=7 (127): 0x60, 0x48
 *   n=8 (255): 0xB8
 *   n=9 (511): 0x110, 0x108
 * Gold = XOR dwoch m-sequence z pary. Kasami (zbior maly) = u XOR decymacja(u).
 */
#include "code_gen.h"
#include <string.h>

/* Galois LFSR, n bitow, zwraca period; wypelnia out[] dlugoscia 2^n-1 */
static uint32_t mseq_galois(uint8_t n, uint32_t poly, uint32_t seed, uint8_t *out)
{
  uint32_t mask = (1u << n) - 1u;
  uint32_t len  = mask;
  uint32_t st   = seed & mask;
  if (st == 0u) {
    st = 1u;
  }
  for (uint32_t i = 0; i < len; i++) {
    uint8_t o = (uint8_t)(st & 1u);
    out[i] = o;
    st >>= 1;
    if (o) {
      st ^= poly;
    }
    st &= mask;
  }
  return len;
}

/* Stopien n dla zadanej dlugosci */
static uint8_t length_to_n(uint16_t length)
{
  switch (length) {
    case 127: return 7;
    case 255: return 8;
    case 511: return 9;
    default:  return 0;
  }
}

/* Para wielomianow pierwotnych (poly_a, poly_b) dla Gold */
static int gold_polys(uint8_t n, uint32_t *a, uint32_t *b)
{
  switch (n) {
    case 7: *a = 0x60u;  *b = 0x48u;  return 0;
    case 9: *a = 0x110u; *b = 0x108u; return 0;
    default: return -1;   /* n=8: Gold niestandardowy */
  }
}

static void make_reference(code_t *out)
{
  for (uint16_t i = 0; i < out->length; i++) {
    out->ref_q15[i] = out->bits[i] ? (int16_t)32767 : (int16_t)(-32767);
  }
}

int code_gen_build(uint8_t type, uint16_t length, code_t *out)
{
  uint8_t n = length_to_n(length);
  if (n == 0u || out == NULL) {
    return -1;
  }
  out->length = length;

  if (type == CODE_MSEQ) {
    uint32_t poly = (n == 7) ? 0x60u : (n == 8) ? 0xB8u : 0x110u;
    mseq_galois(n, poly, 1u, out->bits);
  }
  else if (type == CODE_GOLD) {
    uint32_t pa, pb;
    if (gold_polys(n, &pa, &pb) != 0) {
      /* brak pary pierwotnej (n=8) - degraduj do m-sequence */
      mseq_galois(n, 0xB8u, 1u, out->bits);
    } else {
      static uint8_t u[CODE_MAXLEN];
      static uint8_t v[CODE_MAXLEN];
      mseq_galois(n, pa, 1u, u);
      mseq_galois(n, pb, 1u, v);
      for (uint16_t i = 0; i < length; i++) {
        out->bits[i] = u[i] ^ v[i];          /* czlon rodziny dla przesuniecia 0 */
      }
    }
  }
  else if (type == CODE_KASAMI) {
    /* Zbior maly Kasami dla n parzystego (tu n=8). Dla n nieparzystego
       degraduj do m-sequence. */
    if (n != 8) {
      uint32_t poly = (n == 7) ? 0x60u : 0x110u;
      mseq_galois(n, poly, 1u, out->bits);
    } else {
      static uint8_t u[CODE_MAXLEN];
      mseq_galois(8, 0xB8u, 1u, u);
      uint32_t q = (1u << (n / 2)) + 1u;     /* decymacja 17 */
      uint16_t sub = (uint16_t)((1u << (n / 2)) - 1u); /* okres w = 15 */
      static uint8_t w[CODE_MAXLEN];
      for (uint16_t i = 0; i < length; i++) {
        w[i] = u[(uint16_t)((uint32_t)i * q % length)];
      }
      (void)sub;
      for (uint16_t i = 0; i < length; i++) {
        out->bits[i] = u[i] ^ w[i];          /* czlon zbioru dla przesuniecia 0 */
      }
    }
  }
  else {
    return -1;
  }

  make_reference(out);
  return 0;
}
