# DFM-17 firmware build without STM32CubeIDE, and the host tests.
#
#   make firmware   build every firmware variant into build/<variant>/
#   make test       host unit tests (tools/tests) and APRS decode checks
#                   (tools/aprs_sim, needs python3 numpy/scipy and direwolf)
#   make size       flash/RAM per variant, the size budget and the soft-double
#                   check (tools/size/size_report.py); SIZE_BASE=<build dir>
#                   compares with another build, e.g. of main
#   make            both
#
# The ARM toolchain is arm-none-eabi-gcc on PATH, or else the one bundled with
# STM32CubeIDE under /opt/st. Override with ARM=/path/to/arm-none-eabi-
# The build matches the CubeIDE Debug configuration (-O0 -g3), with warnings
# as errors. Our sources get extra warnings (WARN); the vendor HAL is built once
# into build/hal/ with -Wall only, since it isn't ours to change.

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
# -Wdouble-promotion: float maths quietly promoted to double pulls in the
# software double routines (several KB of flash on this FPU-less part)
WARN    = -Wextra -Wshadow -Wdouble-promotion
SRCS    = $(wildcard $(FW)/Core/Src/*.c) $(wildcard $(FW)/Core/Startup/*.s)
# firmware version (version.c): git describe, e.g. v0.1.0-3-gabc1234-dirty
FW_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo unknown)
HAL     = $(FW)/Drivers/STM32F1xx_HAL_Driver/Src
HAL_OBJS = $(patsubst $(HAL)/%.c,$(BUILD)/hal/%.o,$(wildcard $(HAL)/*.c))

# build variants: the selectable modes in main.c. sine_test is the tracker
# with the experimental sine AFSK built in and on (aprs.h, #54); it's built
# here so it doesn't rot, but its packets don't decode yet
VARIANTS = tracker fox hw_survey rf_test nfc_tool sine_test
DEFS_tracker   =
DEFS_fox       = -DFOX
DEFS_hw_survey = -DHW_SURVEY
DEFS_rf_test   = -DRF_TEST_SWD
DEFS_nfc_tool  = -DHW_NFC
DEFS_sine_test = -DAPRS_AFSK_SINE=1

all: firmware test

firmware: $(VARIANTS:%=$(BUILD)/%/dfm17.elf)

$(BUILD)/hal/%.o: $(HAL)/%.c $(FW)/Core/Inc/stm32f1xx_hal_conf.h
	@test -x "$(CC)" || command -v "$(CC)" >/dev/null || \
		{ echo "arm-none-eabi-gcc not found, set ARM=/path/to/arm-none-eabi-"; exit 1; }
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -c $< -o $@

# rewritten only when the version changes, so a new commit (or uncommitted
# changes) rebuilds the firmware even if no source file changed
$(BUILD)/fw_version.txt: FORCE
	@mkdir -p $(@D)
	@echo '$(FW_VERSION)' | cmp -s - $@ || echo '$(FW_VERSION)' > $@

$(BUILD)/%/dfm17.elf: $(SRCS) $(HAL_OBJS) $(wildcard $(FW)/Core/Inc/*.h) $(BUILD)/fw_version.txt
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARN) $(DEFS_$*) '-DFW_VERSION="$(FW_VERSION)"' '-DFW_VARIANT="$*"' \
		$(SRCS) $(HAL_OBJS) $(LDFLAGS) -Wl,-Map=$(@D)/dfm17.map -o $@
	$(OBJCOPY) -O ihex $@ $(@D)/dfm17.hex
	$(OBJCOPY) -O binary $@ $(@D)/dfm17.bin
	@$(SIZE) $@

size: firmware
	python3 tools/size/size_report.py --head $(BUILD) --prefix "$(ARM)" \
		$(if $(SIZE_BASE),--base $(SIZE_BASE))

test:
	$(MAKE) -C tools/tests
	$(MAKE) -C tools/aprs_sim check

clean:
	rm -rf $(BUILD)
	$(MAKE) -C tools/tests clean
	$(MAKE) -C tools/aprs_sim clean

.PHONY: all firmware size test clean FORCE
FORCE:
