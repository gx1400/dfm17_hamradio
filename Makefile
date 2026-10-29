# DFM-17 firmware build without STM32CubeIDE, and the host tests.
#
#   make firmware   build every firmware variant into build/<variant>/
#   make test       host unit tests (tools/tests) and APRS decode checks
#                   (tools/aprs_sim, needs python3 numpy/scipy and direwolf)
#   make            both
#
# The ARM toolchain is arm-none-eabi-gcc on PATH, or else the one bundled with
# STM32CubeIDE under /opt/st. Override with ARM=/path/to/arm-none-eabi-
# The build matches the CubeIDE Debug configuration (-O0 -g3), with warnings
# as errors.

ARM ?= $(shell command -v arm-none-eabi-gcc >/dev/null 2>&1 && echo arm-none-eabi- \
	|| ls -d /opt/st/stm32cubeide_*/plugins/*gnu-tools-for-stm32*/tools/bin 2>/dev/null \
		| sort | tail -1 | sed 's|$$|/arm-none-eabi-|')
CC      = $(ARM)gcc
OBJCOPY = $(ARM)objcopy
SIZE    = $(ARM)size

FW      = dfm17
BUILD   = build
CFLAGS  = -mcpu=cortex-m3 -mthumb -std=gnu11 -O0 -g3 -Wall -Werror \
	-DUSE_HAL_DRIVER -DSTM32F100xB -ffunction-sections -fdata-sections \
	-I$(FW)/Core/Inc -I$(FW)/Drivers/STM32F1xx_HAL_Driver/Inc \
	-I$(FW)/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy \
	-I$(FW)/Drivers/CMSIS/Device/ST/STM32F1xx/Include -I$(FW)/Drivers/CMSIS/Include
LDFLAGS = --specs=nano.specs -T$(FW)/STM32F100R8TX_FLASH.ld -Wl,--gc-sections \
	-Wl,--no-warn-rwx-segments -lm
SRCS    = $(wildcard $(FW)/Core/Src/*.c) $(wildcard $(FW)/Core/Startup/*.s) \
	$(wildcard $(FW)/Drivers/STM32F1xx_HAL_Driver/Src/*.c)

# build variants: the selectable modes in main.c
VARIANTS = tracker fox hw_survey rf_test
DEFS_tracker   =
DEFS_fox       = -DFOX
DEFS_hw_survey = -DHW_SURVEY
DEFS_rf_test   = -DRF_TEST_SWD

all: firmware test

firmware: $(VARIANTS:%=$(BUILD)/%/dfm17.elf)

$(BUILD)/%/dfm17.elf: $(SRCS) $(wildcard $(FW)/Core/Inc/*.h)
	@test -x "$(CC)" || command -v "$(CC)" >/dev/null || \
		{ echo "arm-none-eabi-gcc not found, set ARM=/path/to/arm-none-eabi-"; exit 1; }
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEFS_$*) $(SRCS) $(LDFLAGS) -Wl,-Map=$(@D)/dfm17.map -o $@
	$(OBJCOPY) -O ihex $@ $(@D)/dfm17.hex
	$(OBJCOPY) -O binary $@ $(@D)/dfm17.bin
	@$(SIZE) $@

test:
	$(MAKE) -C tools/tests
	$(MAKE) -C tools/aprs_sim check

clean:
	rm -rf $(BUILD)
	$(MAKE) -C tools/tests clean
	$(MAKE) -C tools/aprs_sim clean

.PHONY: all firmware test clean
