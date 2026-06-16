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

/* Wielomian pierwotny bazowej m-sequence (u) dla Gold. 0 = brak pary. */
static uint32_t gold_prim_poly(uint8_t n)
{
  switch (n) {
    case 7: return 0x60u;
    case 9: return 0x110u;
    default: return 0u;   /* n=8: Gold niestandardowy */
  }
}

/* Para preferowana przez decymacje: v = u decymowane o q = 2^((n+1)/2)+1.
   Daje ograniczona korelacje wzajemna kodow Gold (t(7)=17, t(9)=33). */
static int gold_uv(uint8_t n, uint16_t length, uint8_t *u, uint8_t *v)
{
  uint32_t poly = gold_prim_poly(n);
  if (poly == 0u) {
    return -1;
  }
  mseq_galois(n, poly, 1u, u);
  uint32_t q = (1u << ((n + 1u) / 2u)) + 1u;
  for (uint16_t i = 0; i < length; i++) {
    v[i] = u[(uint16_t)(((uint32_t)i * q) % length)];
  }
  return 0;
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
    static uint8_t u[CODE_MAXLEN];
    static uint8_t v[CODE_MAXLEN];
    if (gold_uv(n, length, u, v) != 0) {
      /* brak pary Gold (n=8) - degraduj do m-sequence */
      mseq_galois(n, 0xB8u, 1u, out->bits);
    } else {
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

int code_gen_gold_member(uint16_t length, uint16_t shift, code_t *out)
{
  uint8_t n = length_to_n(length);
  if (n == 0u || out == NULL) {
    return -1;
  }
  static uint8_t u[CODE_MAXLEN];
  static uint8_t v[CODE_MAXLEN];
  if (gold_uv(n, length, u, v) != 0) {
    return -1;   /* rodzina Gold tylko dla n nieparzystego (127, 511) */
  }
  out->length = length;
  for (uint16_t i = 0; i < length; i++) {
    out->bits[i] = u[i] ^ v[(uint16_t)((i + shift) % length)];
  }
  make_reference(out);
  return 0;
}
