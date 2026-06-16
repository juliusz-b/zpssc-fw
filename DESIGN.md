# DESIGN.md

Dokument decyzji projektowych firmware `zpssc-fw` dla płytki NUCLEO-G431RB
(MCU STM32G431RBT6, Cortex-M4F 170 MHz, FPU, CORDIC, FMAC, DAC, ADC 12-bit).

Autor: Juliusz Bojarczuk, Politechnika Warszawska.

## 1. Cel

Prototyp steruje makietą światłowodowej sieci czujnikowej FBG z multipleksacją
kodową (CDM). Płytka realizuje trzy interfejsy:

1. Strojenie lasera MEMS-VCSEL napięciem podawanym na element HCG (high-contrast
   grating). Wolny sygnał analogowy 0..3,3 V z DAC, dalej skalowany op-ampem na
   standzie do zakresu 0..-12 V.
2. Modulacja lasera sekwencją kodową (Gold / Kasami / m-sequence). To jest sygnał
   referencyjny dla korelacji.
3. Akwizycja sygnału odbitego od siatek Bragga przez ADC i obliczenie korelacji
   wzajemnej z lokalnym kodem, z detekcją pików (lag, amplituda).

Założenia nadrzędne: ma działać szybko (cała transmisja na DMA, korelacja w Q15
ze sprzętowym FMAC) i ma być łatwo rozwijalne oraz łatwe do wgrania (import do
STM32CubeIDE, flash przez wbudowany ST-Link).

## 2. Zegar i rdzeń

- Źródło: HSI16 (16 MHz, wewnętrzny RC), bez zewnętrznego kwarcu, żeby firmware
  był uniwersalny dla dowolnego egzemplarza Nucleo.
- PLL: M=4 (16/4 = 4 MHz na wejściu VCO), N=85 (VCO = 340 MHz), R=2
  (SYSCLK = 170 MHz). To jedyna kombinacja w granicach VCO 96..344 MHz dająca
  równe 170 MHz z 16 MHz HSI.
- Zasilanie rdzenia: Range 1 boost (warunek pracy powyżej 150 MHz).
- Flash: 4 cykle oczekiwania (wait states) dla 170 MHz w Range 1 boost.
- ART/prefetch: włączony (instrukcyjny cache i prefetch Flash).
- FPU: włączona w `SystemInit` (CP10/CP11 full access).
- APB1 = APB2 = 170 MHz (prescalery 1), więc timery liczą z 170 MHz.

## 3. Mapa pinów

Zweryfikowana względem datasheet STM32G431RBT6 i UM2505 (Nucleo-64).

| Funkcja | Pin | Peryferium / AF | Złącze / nazwa | Uwagi |
|---|---|---|---|---|
| Wy1: strojenie HCG | PA4 | DAC1_OUT1 | CN7 / A2 | 0..3,3 V, poziom stały lub sweep |
| Wy2: kod cyfrowy (główna) | PA7 | SPI1_MOSI (AF5) | CN5-4 / D11 | bitstream kodu, DMA |
| Wy2: kod analogowo (opcja) | PA5 | DAC1_OUT2 | D13 | tylko `ANALOG_MOD`, wyklucza LED |
| We: akwizycja | PA0 | ADC1_IN1 | CN8-1 / A0 | sygnał z detektora |
| Telemetria TX | PA2 | USART2_TX (AF7) | VCP przez ST-Link | 921600 8N1 |
| Komendy RX | PA3 | USART2_RX (AF7) | VCP przez ST-Link | |
| LED stanu | PA5 | GPIO out | LD2 / D13 | heartbeat i sygnalizacja |

Rozwiązane kolizje:

- PA5 to jednocześnie LD2, DAC1_OUT2 i SPI1_SCK. W trybie podstawowym używam
  DAC1_OUT1 (PA4), a nie OUT2, a SPI pracuje tylko na linii MOSI (zegar SPI
  generowany wewnętrznie nie jest wyprowadzany na pin). Dzięki temu PA5 zostaje
  wolne jako dioda LD2.
- G431RB nie ma DAC2 (są DAC1 i wewnętrzny DAC3 bez wyprowadzeń). Opcjonalna
  modulacja analogowa korzysta więc z DAC1_OUT2 (PA5), co wyklucza LD2 w tym
  trybie. Wybór świadomy, bo to ścieżka testowa kompilowana warunkowo.
- SPI1_NSS (PA4) nie jest używane (tryb software NSS), więc PA4 zostaje dla DAC1.

## 4. Przepływ danych

```
[tuning]  DAC1_OUT1 (PA4) ----> op-amp ----> napięcie HCG ----> długość fali VCSEL
                                                                      |
[modulator] SPI1_MOSI (PA7) --> sterownik prądu lasera --> intensywność (kod CDM)
                                                                      |
                                                            tor światłowodowy + FBG
                                                                      |
[acquisition] detektor --> PA0 (ADC1_IN1) --[TIM trigger]--> DMA --> bufor okna
                                                                      |
[correlation] okno x kod referencyjny (Q15, FMAC/CMSIS/C) --> piki (lag, amp)
                                                                      |
[comms] USART2 (PA2/PA3) <----------- telemetria + parser komend ------
```

Modulacja i strojenie są niezależne (różne peryferia, różne DMA). Akwizycja jest
wyzwalana timerem, więc próbki mają równe odstępy niezależnie od obciążenia CPU.
Korelacja rusza dopiero po zgłoszeniu gotowego okna przez DMA (half/full), nie ma
busy-waitu na ścieżce gorącej.

## 5. Wyjście 1: strojenie HCG (`tuning`)

DAC1 kanał 1 (PA4). Dwa tryby:

- Poziom stały: jedna wartość 12-bit zadawana komendą, `HAL_DAC_SetValue`.
- Sweep: timer TIM6 wyzwala DMA z bufora LUT (trójkąt lub piła) do rejestru DAC.
  Okres i amplituda konfigurowalne. Sweep jest wolny (rzędy Hz..kHz), bo
  odpowiada powolnemu przestrajaniu długości fali, nie modulacji.

LUT generowany w RAM przy starcie/zmianie parametrów. TIM6 jako trigger DAC
(TRGO = update) odciąża CPU całkowicie.

## 6. Wyjście 2: modulacja kodem (`modulator`) - wybór SPI vs timer/GPIO

Wybrano SPI1 MOSI + DMA. Uzasadnienie:

- SPI ma własny generator bodu z PCLK2 (170 MHz), maksymalnie 85 Mbit/s
  (prescaler 2). Bity wychodzą z rejestru przesuwnego z dokładnym, sprzętowym
  taktowaniem, bez jittera programowego.
- DMA dostarcza bajty z pamięci; przy 170 MHz AHB FIFO SPI nie głodzi się nawet
  przy maksymalnym bodzie, więc bitstream jest ciągły (brak przerw między bajtami).
- Wariant timer + DMA do GPIO BSRR daje niższy i mniej stabilny chip rate:
  każdy chip to osobny request DMA, częstotliwość ogranicza przepustowość DMA i
  rośnie jitter; dodatkowo BSRR przełącza cały port (ryzyko efektów ubocznych).
- SPI wymaga jednego pinu (MOSI), nie blokuje innych linii portu, a kod łatwo
  rozszerzyć o drugi kanał na SPI2/SPI3.

Chip rate ustawiany przez prescaler SPI oraz współczynnik nadpróbkowania OSF
(liczba bitów na chip). Efektywny chip rate = f_SPI / OSF. OSF pozwala zejść do
niskich chip rate w trybie DIRECT (żeby ADC zdążył próbkować na żywo) i zapewnia
całkowitą liczbę próbek ADC na chip.

Pakowanie: chipy 0/1 rozszerzane o OSF i pakowane MSB-first do bajtów. Bufor
bajtów to bezpośrednie źródło DMA dla SPI.

Opcja `ANALOG_MOD`: te same chipy podawane jako poziomy DAC1_OUT2 (PA5),
wyzwalane TIM7 przez DMA (jeden poziom na chip). Do testów toru analogowego,
kompilowane warunkowo. Ponieważ PA5 to także LD2, w tym trybie dioda stanu jest
wyłączona.

## 7. Wejście: akwizycja (`acquisition`)

- ADC1, kanał IN1 (PA0), rozdzielczość 12-bit, próbkowanie wyzwalane TIM (TRGO).
- DMA w trybie circular do bufora double-buffer (dwie połówki). Przerwania
  half-transfer i transfer-complete sygnalizują gotową połówkę okna.
- Częstotliwość próbkowania ustawiana okresem timera; f_ADC = SAMPLES_PER_CHIP *
  chip_rate, żeby na każdy chip przypadała całkowita liczba próbek.
- Po zebraniu okna próbki są decymowane do rozdzielczości chipa (uśrednianie
  SAMPLES_PER_CHIP próbek), co redukuje szum i ogranicza długość filtra
  dopasowanego do długości kodu (mieści się w FMAC).

Ograniczenie fizyczne: ADC G431 osiąga ok. 4 MS/s (12-bit, tryb szybki). Dlatego
chipów rzędu dziesiątek Mchip/s nie da się złapać na żywo. Stąd dwa tryby pracy
(sekcja 9).

## 8. Korelacja Q15 (`correlation`) - FMAC vs CMSIS-DSP vs C

Korelacja wzajemna sygnału `s` z kodem `c`: `r[k] = sum_n s[n+k] * c[n]`. To filtr
dopasowany: FIR o współczynnikach `h[n] = c[N-1-n]` (kod odwrócony w czasie) daje
na wyjściu korelację. Implementacja w trzech wariantach, przełączanych w
`config.h` przez `CORR_ENGINE`:

- `CORR_ENGINE_FMAC` (domyślny, najszybszy): sprzętowy FMAC w konfiguracji FIR
  Q1.15. Współczynniki to odwrócony kod, próbki strumieniowane przez bufor
  kołowy. FMAC liczy MAC w tle, CPU zostaje wolne na komunikację. FMAC ma 256
  słów 16-bit pamięci dzielonej (coeff + input ring + output), więc filtr
  dopasowany mieści się dla długości kodu do ok. 127 przy rozdzielczości chipa.
- `CORR_ENGINE_CMSIS`: `arm_correlate_q15` z CMSIS-DSP. Wektoryzowana (`__SMLALD`
  liczy 2 MAC/cykl), czytelna referencja, dowolna długość. Blokuje CPU na czas
  liczenia.
- `CORR_ENGINE_PLAIN`: prosta pętla Q15 w C. Najwolniejsza, ale niezależna od
  bibliotek i peryferiów; fallback i wzorzec do weryfikacji pozostałych.

Zgrubne porównanie kosztu dla filtra N-tap nad oknem M próbek:

| Wariant | Koszt CPU | Uwaga |
|---|---|---|
| FMAC | ~0 (offload) | przepustowość ~1 MAC/cykl w tle, CPU wolne |
| CMSIS `arm_correlate_q15` | ~N*M/2 cykli | SIMD 2 MAC/cykl, blokuje CPU |
| C Q15 | ~N*M cykli + narzut | referencja, dowolna długość |

Dla kodu 127 i okna kilkuset próbek różnica między FMAC a C to rząd wielkości w
zajętości CPU. FMAC wybrany jako domyślny, bo zwalnia rdzeń na obsługę telemetrii
i kolejnych okien.

Detekcja pików: próg względem maksimum (np. 0,5 * max) plus warunek maksimum
lokalnego, zwraca listę (lag, amplituda). Liczba pików ograniczona `MAX_PEAKS`.

## 9. Tryby DIRECT i ETS

- `MODE_DIRECT` (domyślny, do pierwszego uruchomienia): niski chip rate,
  próbkowanie na żywo (f_ADC = SAMPLES_PER_CHIP * chip_rate, w granicach 4 MS/s),
  pełny tor przetestowany end-to-end. Z tym trybem firmware jest gotowy do
  uruchomienia na stole.
- `MODE_ETS` (equivalent time sampling, zaczątek): kod jest okresowy, ADC próbkuje
  raz na okres kodu z narastającym przesunięciem fazy. Po zebraniu wielu okresów
  rekonstruuje się przebieg o efektywnym paśmie znacznie wyższym niż 4 MS/s.
  Wymaga sztywnego, deterministycznego związku fazowego między zegarem chipów
  (SPI/TIM) a wyzwalaniem ADC (wspólny timer nadrzędny, programowalne opóźnienie,
  kalibracja na oscyloskopie). W firmware zostawiony jako szkielet z jasnym
  oznaczeniem co dostroić na sprzęcie (patrz `acquisition.c`, sekcja ETS).

## 10. Moduły i pliki

Logika aplikacji oddzielona od kodu HAL. `main.c` tylko spina (inicjalizacja
peryferiów w stylu CubeMX + super-pętla), moduły realizują funkcje:

| Moduł | Plik | Odpowiedzialność |
|---|---|---|
| board | `board.[ch]` | zegar 170 MHz, mapa pinów, init GPIO/LED |
| code_gen | `code_gen.[ch]` | generacja Gold/Kasami/m-sequence, bufor chipów + referencja Q15 |
| modulator | `modulator.[ch]` | wystawienie kodu na SPI1 MOSI + DMA (opcja DAC2) |
| tuning | `tuning.[ch]` | DAC1: poziom stały + sweep (TIM6 + DMA) |
| acquisition | `acquisition.[ch]` | ADC1 + TIM trigger + DMA double-buffer |
| correlation | `correlation.[ch]` | korelacja Q15 (FMAC/CMSIS/C) + detekcja pików |
| comms | `comms.[ch]` | USART2: telemetria + parser komend ASCII |
| config | `config.h` | parametry kompilacji (chip rate, długość kodu, tryb, silnik) |

Konfiguracja w `config.h`: `CHIP_RATE_HZ`, `CODE_TYPE`, `CODE_LENGTH`,
`SAMPLES_PER_CHIP`, `WINDOW_SAMPLES`, `OP_MODE`, `CORR_ENGINE`, `ANALOG_MOD`.

## 11. Plan wydajności

- Cała transmisja (DAC sweep, modulacja, akwizycja) na DMA, bez busy-wait na
  ścieżce gorącej. Super-pętla obsługuje tylko zdarzenia (gotowe okno, komenda).
- Korelacja w stałoprzecinkowym Q15, domyślnie na FMAC (offload sprzętowy).
- Kompilacja `-O2` całości, moduł korelacji `-Ofast`.
- Funkcje krytyczne korelacji w RAM przez `__attribute__((section(".RamFunc")))`,
  żeby uniknąć cykli oczekiwania Flash.
- HAL na inicjalizacji, ścieżki gorące nie wołają HAL w pętli (DMA i FMAC robią
  pracę sprzętowo). LL można dołożyć później na SPI/ADC jeśli pomiar wykaże potrzebę.

## 12. Co wymaga sprzętu do dostrojenia

- Dokładne częstotliwości: chip rate vs f_ADC, prescalery timerów dobrane pod
  rzeczywisty detektor i pasmo toru.
- Tryb ETS: związek fazowy SPI vs ADC, krok przesunięcia, liczba okresów. Do
  kalibracji na oscyloskopie z realnym sygnałem.
- Poziomy DAC strojenia vs charakterystyka op-ampu i zakres przestrajania VCSEL.
- Próg detekcji pików dobrany pod realny stosunek sygnału do szumu.
- Długość i typ kodu vs liczba siatek i tłumienie toru.

## 13. Powiązanie

Symulator toru i sekwencji kodowych: `zpssc`
(https://github.com/juliusz-b/zpssc, DOI 10.5281/zenodo.15089768). Firmware
odtwarza na sprzęcie część łańcucha modelowanego w symulatorze (generacja kodu,
modulacja, korelacja, detekcja pików).
