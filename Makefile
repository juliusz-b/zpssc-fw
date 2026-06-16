# Makefile - zpssc-fw (STM32G431RB, arm-none-eabi-gcc)
# Juliusz Bojarczuk, Politechnika Warszawska
#
# Budowa z linii polecen, alternatywa dla STM32CubeIDE.
#   make                  - budowa (Release, -O2; correlation.c -Ofast)
#   make USE_CMSIS_DSP=1  - dolacza arm_correlate_q15 (silnik CMSIS-DSP)
#   make ANALOG_MOD=1     - kompiluje sciezke modulacji analogowej (DAC2)
#   make clean
# Toolchain: domyslnie arm-none-eabi-* z PATH. Mozna wskazac:
#   make GCC_PATH="C:/ST/STM32CubeIDE_2.1.1/.../tools/bin"

TARGET    = zpssc-fw
BUILD_DIR = build
DEBUG     ?= 0
OPT       ?= -O2

######################################
# zrodla
######################################
C_SOURCES  = $(wildcard Core/Src/*.c)
C_SOURCES += $(wildcard Drivers/STM32G4xx_HAL_Driver/Src/*.c)
ASM_SOURCES = $(wildcard Core/Startup/*.s)

######################################
# toolchain
######################################
PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
SZ = $(GCC_PATH)/$(PREFIX)size
OC = $(GCC_PATH)/$(PREFIX)objcopy
else
CC = $(PREFIX)gcc
SZ = $(PREFIX)size
OC = $(PREFIX)objcopy
endif
HEX = $(OC) -O ihex
BIN = $(OC) -O binary -S

######################################
# rdzen / FPU
######################################
CPU = -mcpu=cortex-m4
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

######################################
# definicje i include
######################################
C_DEFS = -DUSE_HAL_DRIVER -DSTM32G431xx
AS_DEFS =

C_INCLUDES = \
-ICore/Inc \
-IDrivers/STM32G4xx_HAL_Driver/Inc \
-IDrivers/STM32G4xx_HAL_Driver/Inc/Legacy \
-IDrivers/CMSIS/Device/ST/STM32G4xx/Include \
-IDrivers/CMSIS/Include

# wybor silnika korelacji: ENGINE=FMAC|CMSIS|PLAIN (domyslnie z config.h)
ifeq ($(ENGINE),FMAC)
C_DEFS += -DCORR_ENGINE=0
endif
ifeq ($(ENGINE),CMSIS)
C_DEFS += -DCORR_ENGINE=1
USE_CMSIS_DSP := 1
endif
ifeq ($(ENGINE),PLAIN)
C_DEFS += -DCORR_ENGINE=2
endif

# opcjonalny silnik CMSIS-DSP (wymagany przez ENGINE=CMSIS)
ifeq ($(USE_CMSIS_DSP),1)
C_SOURCES  += Drivers/CMSIS/DSP/Source/FilteringFunctions/arm_correlate_q15.c
C_INCLUDES += -IDrivers/CMSIS/DSP/Include
C_DEFS     += -DARM_MATH_CM4 -D__FPU_PRESENT=1U -DARM_MATH_ROUNDING -DUSE_CMSIS_DSP
endif

# opcjonalna modulacja analogowa (DAC2)
ifeq ($(ANALOG_MOD),1)
C_DEFS += -DANALOG_MOD
endif

######################################
# flagi
######################################
ifeq ($(DEBUG),1)
OPT = -Og
endif

WFLAGS = -Wall -Wextra -fdata-sections -ffunction-sections
ASFLAGS = $(MCU) $(AS_DEFS) $(OPT) $(WFLAGS)
CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) $(WFLAGS)
ifeq ($(DEBUG),1)
CFLAGS += -g -gdwarf-2
endif
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

# modul korelacji budowany z -Ofast
$(BUILD_DIR)/correlation.o: OPT := -Ofast

######################################
# linker
######################################
LDSCRIPT = STM32G431RBTX_FLASH.ld
LIBS = -lc -lm
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) \
  -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

######################################
# obiekty i reguly
######################################
OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(CC) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $(BUILD_DIR)

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)
