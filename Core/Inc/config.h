/**
 * config.h - parametry kompilacji firmware zpssc-fw
 * Juliusz Bojarczuk, Politechnika Warszawska
 *
 * Wszystkie nastawy zebrane w jednym miejscu. Po zmianie wymagana
 * rekompilacja. Czesc parametrow mozna tez zmienic w locie komenda USART
 * (patrz comms.c), wartosci ponizej to stan poczatkowy po resecie.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ============================ Tryb pracy ============================ */
#define MODE_DIRECT      0   /* probkowanie na zywo, jeden kod             */
#define MODE_ETS         1   /* equivalent time sampling (zaczatek)        */
#define MODE_SCAN        2   /* skan: bank kodow, kod na pasmo dlugosci fali */
#define MODE_SPECTRUM    3   /* analizator widma: laser CW + sweep DAC + odczyt mocy */
/* OP_MODE wybierany kompilacyjnie. MODE_SPECTRUM buduje osobny firmware
   (analizator), pozostale wartosci buduja tor CDM. Mozna nadpisac z make:
   `make SPECTRUM=1` -> -DOP_MODE=3. */
#ifndef OP_MODE
#define OP_MODE          MODE_DIRECT
#endif

/* ============================ Sekwencja kodowa ===================== */
#define CODE_MSEQ        0
#define CODE_GOLD        1
#define CODE_KASAMI      2
#ifndef CODE_TYPE
#define CODE_TYPE        CODE_GOLD
#endif
#ifndef CODE_LENGTH
#define CODE_LENGTH      127   /* 127 (n=7), 255 (n=8), 511 (n=9)        */
#endif

/* ====================== Modulacja - wyjscie 2 ===================== */
/* Efektywny chip rate = f_SPI / CHIP_OVERSAMPLE. Baud SPI jest dyskretny
   (PCLK2 / {2,4,...,256}), wiec rzeczywisty chip rate moze sie roznic od
   zadanego - modulator zglasza wartosc faktyczna. Domyslne nastawy dobrane
   tak, by f_ADC = SAMPLES_PER_CHIP * chip_rate miescil sie z zapasem w ADC. */
#define CHIP_RATE_HZ     166000UL  /* docelowy chip rate [chip/s]        */
#define CHIP_OVERSAMPLE  4U        /* liczba bitow SPI na chip (OSF)     */
/* #define ANALOG_MOD */           /* kod takze z DAC1_OUT2 (PA5); LED off */

/* ====================== Akwizycja - wejscie ====================== */
#define SAMPLES_PER_CHIP 4U
/* Okno = pelen kod + zakres przeszukiwania lagow. Liczba badanych lagow
   (n_lags) wynosi LAG_SEARCH_CHIPS+1 - tyle siatek/odbic mozna rozroznic. */
#define LAG_SEARCH_CHIPS 64U
#define WINDOW_CHIPS     (CODE_LENGTH + LAG_SEARCH_CHIPS)
#define WINDOW_SAMPLES   (WINDOW_CHIPS * SAMPLES_PER_CHIP)

/* ============================ Korelacja =========================== */
#define CORR_ENGINE_FMAC  0   /* sprzetowy FMAC (domyslny, najszybszy)   */
#define CORR_ENGINE_CMSIS 1   /* arm_correlate_q15 z CMSIS-DSP           */
#define CORR_ENGINE_PLAIN 2   /* prosta petla Q15 w C (referencja)       */
#ifndef CORR_ENGINE
#define CORR_ENGINE       CORR_ENGINE_FMAC
#endif
#define MAX_PEAKS         8
#define PEAK_THRESH_FRAC  0.5f /* prog detekcji = frac * maksimum         */

/* ====================== Strojenie - wyjscie 1 ==================== */
#define TUNING_DEFAULT_LEVEL  2048U   /* 12-bit (~1.65 V na PA4)          */
#define TUNING_SWEEP_POINTS   256U    /* dlugosc LUT sweepa               */

/* ================= Tryb skanu (bank kodow <-> dlugosc fali) ======== */
/* Kazde pasmo dlugosci fali (poziom DAC) ma przypisany kod Gold. Skan
   przechodzi po N poziomach, na kazdym emituje swoj kod i liczy korelacje
   z calym bankiem. Bank dziala dla kodow Gold (CODE_LENGTH 127 lub 511);
   przy 127 zajmuje CODE_BANK_SIZE*127*2 B referencji. */
#define CODE_BANK_SIZE    16U     /* liczba kodow/pasm */
#define SCAN_LEVEL_MIN    0U      /* poziom DAC pasma 0      (12-bit) */
#define SCAN_LEVEL_MAX    4095U   /* poziom DAC pasma N-1    (12-bit) */
#define SCAN_SETTLE_MS    2U      /* zwloka po zmianie poziomu/kodu   */

/* =============== Tryb widmowy (OP_MODE = MODE_SPECTRUM) ============ */
/* Laser CW (bez modulacji), sweep DAC po SPEC_POINTS poziomach, na kazdym
   usrednienie okna ADC = moc odbita. Probka k to zawsze ten sam poziom DAC,
   wiec indeks w UART jednoznacznie odpowiada dlugosci fali. */
#define SPEC_POINTS       256U    /* liczba punktow widma (rozdzielczosc) */
#define SPEC_LEVEL_MIN    0U      /* dolny poziom DAC sweepa (12-bit)     */
#define SPEC_LEVEL_MAX    4095U   /* gorny poziom DAC sweepa (12-bit)     */
#define SPEC_SETTLE_MS    1U      /* zwloka na ustalenie na punkt         */
#define SPEC_LASER_PA7    0U      /* poziom PA7 (linia modulacji) w CW: 0=niski (na razie) */

/* ============================ Telemetria ========================== */
#define UART_BAUD         921600UL

/* ===================== Sanity checks kompilacji =================== */
#if (CODE_LENGTH != 127) && (CODE_LENGTH != 255) && (CODE_LENGTH != 511)
#error "CODE_LENGTH musi byc 127, 255 lub 511"
#endif
#if (CORR_ENGINE == CORR_ENGINE_FMAC) && (CODE_LENGTH > 127)
#error "FMAC: filtr dopasowany do dlugosci <=127. Dla dluzszych uzyj CMSIS/PLAIN."
#endif
#if (CODE_BANK_SIZE * CODE_LENGTH) > 4096
#error "Bank kodow za duzy na RAM. Zmniejsz CODE_BANK_SIZE lub CODE_LENGTH."
#endif

#endif /* CONFIG_H */
