# zpssc-fw

Firmware prototypowy dla płytki **NUCLEO-G431RB** (STM32G431RBT6, Cortex-M4F
170 MHz, FPU, FMAC, DAC, ADC 12-bit). Steruje makietą światłowodowej sieci
czujnikowej FBG z multipleksacją kodową (CDM): zadaje napięcie strojenia lasera
MEMS-VCSEL, moduluje laser sekwencją kodową i liczy korelację sygnału odbitego
od siatek Bragga.

Autor: Juliusz Bojarczuk, Politechnika Warszawska.

## Funkcje

- Trzy interfejsy sprzętowe:
  1. **Strojenie HCG** (wyjście 1): DAC1_OUT1 (PA4), poziom stały lub wolny
     sweep trójkątny/piłokształtny przez DMA wyzwalany TIM6.
  2. **Modulacja kodem** (wyjście 2): sekwencja Gold/Kasami/m-sequence na
     SPI1 MOSI (PA7) przez DMA, emisja ciągła i okresowa. Opcjonalnie poziomy
     DAC (`ANALOG_MOD`).
  3. **Akwizycja i korelacja** (wejście): ADC1_IN1 (PA0) wyzwalany TIM2,
     transfer DMA do bufora double-buffer, korelacja Q15 i detekcja pików.
- Zegar 170 MHz z HSI16 (bez kwarcu), prefetch/ART i FPU włączone.
- Cała transmisja na DMA, super-pętla bez RTOS.
- Trzy silniki korelacji przełączane w `config.h`: sprzętowy **FMAC**,
  **CMSIS-DSP** (`arm_correlate_q15`) i referencyjna pętla **C** w Q15.
- Generator kodów: m-sequence, Gold i Kasami, długość 127/255/511.
- Telemetria i komendy ASCII przez USART2 (wirtualny COM po ST-Link), 921600.
- Dwa tryby: `MODE_DIRECT` (gotowy do uruchomienia) i `MODE_ETS` (zaczątek
  próbkowania ekwiwalentnego, do dostrojenia na sprzęcie).

Szczegóły decyzji projektowych: [DESIGN.md](DESIGN.md).

## Mapa pinów

| Funkcja | Pin | Peryferium | Złącze Nucleo |
|---|---|---|---|
| Strojenie HCG (Wy1) | PA4 | DAC1_OUT1 | CN7 / A2 |
| Kod cyfrowy (Wy2) | PA7 | SPI1_MOSI (AF5) | CN5-4 / D11 |
| Kod analogowy (opcja `ANALOG_MOD`) | PA5 | DAC1_OUT2 | D13 (wyklucza LED) |
| Akwizycja (We) | PA0 | ADC1_IN1 | CN8-1 / A0 |
| Telemetria TX | PA2 | USART2_TX (AF7) | VCP ST-Link |
| Komendy RX | PA3 | USART2_RX (AF7) | VCP ST-Link |
| LED stanu | PA5 | GPIO (LD2) | D13 |

Uwagi: PA5 pełni rolę LED tylko bez `ANALOG_MOD`. SPI pracuje na samej linii
MOSI (zegar SPI nie jest wyprowadzany), dlatego PA5 zostaje wolne. G431RB nie ma
DAC2, więc modulacja analogowa korzysta z DAC1_OUT2 (PA5).

## Budowa i wgranie w STM32CubeIDE

1. `File > Open Projects from File System...` i wskaż katalog repozytorium
   (albo `File > Import > Existing Projects into Workspace`).
2. Projekt zaimportuje się jako `zpssc-fw` z gotową konfiguracją (`.project`,
   `.cproject`). Sterowniki HAL, CMSIS i CMSIS-DSP są w `Drivers/`, więc projekt
   buduje się od razu, bez pobierania pakietów.
3. `Project > Build Project` (Ctrl+B). Wynik trafia do `Debug/zpssc-fw.elf`.
4. Podłącz płytkę i kliknij zielony **Run** (lub Debug). CubeIDE wgra firmware
   przez wbudowany ST-Link. Przy pierwszym uruchomieniu IDE samo utworzy
   konfigurację `STM32 C/C++ Application` dla NUCLEO-G431RB.

Plik `zpssc-fw.ioc` odzwierciedla konfigurację peryferiów i można go otworzyć w
CubeMX jako punkt wyjścia do dalszego rozwoju. Inicjalizacja peryferiów jest
jednak utrzymywana ręcznie w modułach (MSP w plikach modułów), więc ponowna
generacja kodu z `.ioc` nie jest wymagana do budowy.

## Budowa przez make

Alternatywa bez IDE, z `arm-none-eabi-gcc`:

```
make                  # Release, -O2 (correlation.c z -Ofast)
make ENGINE=CMSIS     # silnik korelacji CMSIS-DSP
make ENGINE=PLAIN     # referencyjny silnik C
make ANALOG_MOD=1     # ścieżka modulacji analogowej (DAC1_OUT2)
make clean
```

Jeśli toolchain nie jest w PATH, wskaż go (np. ten z CubeIDE):

```
make GCC_PATH="C:/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin"
```

Wynik: `build/zpssc-fw.elf`, `.hex`, `.bin`. Wgranie z linii poleceń np.
`STM32_Programmer_CLI -c port=SWD -w build/zpssc-fw.bin 0x08000000 -rst`.

## Komendy USART (921600 8N1)

Każda komenda zakończona znakiem nowej linii. Odpowiedzi `OK` / `ERR ...`.

| Komenda | Opis |
|---|---|
| `PING` | test łączności |
| `ID` | identyfikacja i aktywny silnik korelacji |
| `STATUS` | tryb, stan, typ kodu, chip rate, f_ADC, silnik |
| `HELP` | lista komend |
| `START` / `STOP` | start/stop modulacji i akwizycji |
| `LVL <0-4095>` | poziom stały DAC strojenia |
| `SWEEP <okres_ms> <amp> [TRI\|SAW]` | sweep strojenia; `SWEEP OFF` wyłącza |
| `RATE <chip_hz>` | docelowy chip rate (zgłaszana wartość rzeczywista) |
| `CODE <MSEQ\|GOLD\|KASAMI>` | typ sekwencji (przebudowa w locie) |
| `LEN <127\|255\|511>` | długość kodu (kompilacyjnie, w `config.h`) |
| `MODE <DIRECT\|ETS>` | tryb pracy |
| `CORR` | wypis ostatniego wyniku korelacji |
| `STREAM <ON\|OFF>` | automatyczny wypis wyniku po każdym oknie |

Format wyniku korelacji:

```
CORR lags=<n> max=<amp> peaks=<k>
 P <lag> <amplituda>
 ...
```

## Konfiguracja (`Core/Inc/config.h`)

Najważniejsze `#define`: `OP_MODE`, `CODE_TYPE`, `CODE_LENGTH`, `CHIP_RATE_HZ`,
`CHIP_OVERSAMPLE`, `SAMPLES_PER_CHIP`, `WINDOW_CHIPS`, `CORR_ENGINE`, `MAX_PEAKS`,
`PEAK_THRESH_FRAC`, `UART_BAUD`, `ANALOG_MOD`. Długość kodu jest kompilacyjna,
bo od niej zależą rozmiary buforów. Część parametrów (chip rate, typ kodu, tryb,
poziom/sweep) zmienia się też w locie komendami USART.

## Struktura

```
Core/Inc, Core/Src   moduły aplikacji + config.h + HAL conf
  board              zegar 170 MHz, mapa pinów, LED
  code_gen           generacja m-sequence / Gold / Kasami
  modulator          kod na SPI1 MOSI + DMA (opcja DAC)
  tuning             DAC1: poziom + sweep
  acquisition        ADC1 + TIM2 trigger + DMA double-buffer + zaczątek ETS
  correlation        korelacja Q15 (FMAC / CMSIS-DSP / C) + detekcja pików
  comms              USART2: telemetria + parser komend
Core/Startup         startup_stm32g431xx.s
Drivers              HAL, CMSIS, CMSIS-DSP (zwendowane)
STM32G431RBTX_FLASH.ld   skrypt linkera
Makefile             budowa z linii poleceń
```

## Jak rozszerzać

- **Kolejny kod / rodzina Gold**: dodaj wielomiany w `code_gen.c` (tablice par
  pierwotnych) i obsłuż w `code_gen_build`. Reszta toru korzysta z `code_t`.
- **Drugi kanał modulacji**: dołóż instancję na SPI2/SPI3 obok `modulator`,
  trzymając ten sam interfejs (set_code / start / stop).
- **Tryb ETS**: dokończ `acquisition_ets_begin` / `acquisition_ets_collect`
  według komentarza w `acquisition.c` (wspólny timer, opóźnienie wyzwolenia,
  przeplot próbek), po kalibracji fazy na oscyloskopie.
- **Inny silnik korelacji** (np. FFT): dołóż gałąź w `correlation.c` i wartość
  `CORR_ENGINE`.
- **Łączność (np. LoRa) w przyszłości**: osobny moduł obok `comms`, bez ruszania
  toru sygnałowego. Prototyp celowo nie zawiera LoRa/USB/RTOS.

## Powiązane projekty

- Symulator sieci czujnikowej i sekwencji kodowych: **zpssc**,
  https://github.com/juliusz-b/zpssc (DOI 10.5281/zenodo.15089768). Firmware
  odtwarza na sprzęcie część łańcucha modelowanego w symulatorze.

## Licencja

GNU GPL v3.0 (zgodnie z repozytorium `zpssc`). Patrz [LICENSE](LICENSE).
Copyright (C) Juliusz Bojarczuk, Politechnika Warszawska.
