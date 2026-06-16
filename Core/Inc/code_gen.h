/**
 * code_gen.h - generacja sekwencji kodowych (m-sequence / Gold / Kasami)
 * Juliusz Bojarczuk, Politechnika Warszawska
 */
#ifndef CODE_GEN_H
#define CODE_GEN_H

#include <stdint.h>
#include "config.h"

#define CODE_MAXLEN 511

typedef struct {
  uint16_t length;                 /* dlugosc kodu w chipach */
  uint8_t  bits[CODE_MAXLEN];      /* chipy 0/1 (do modulacji) */
  int16_t  ref_q15[CODE_MAXLEN];   /* referencja bipolarna Q15: 1->+1, 0->-1 */
} code_t;

/*
 * Buduje kod zadanego typu i dlugosci.
 * type:   CODE_MSEQ / CODE_GOLD / CODE_KASAMI
 * length: 127 / 255 / 511
 * Zwraca 0 przy sukcesie, -1 przy nieobslugiwanej kombinacji.
 * Dla GOLD dostepne dlugosci 127 i 511 (n nieparzyste). Dla 255 (n=8)
 * GOLD nie jest standardowy, uzyj KASAMI; funkcja zwroci wtedy m-sequence.
 */
int code_gen_build(uint8_t type, uint16_t length, code_t *out);

#endif /* CODE_GEN_H */
