# Host unit tests: firmware sources from dfm17/Core built on a PC against a
# fake HAL (fakehal/, fake_hal.c). `make` builds and runs them all.
CC       ?= gcc
CORE     = ../../dfm17/Core
BUILD    = build
# trap on undefined behaviour and out-of-bounds array indexing, without
# needing the sanitizer runtime libraries
SANITIZE ?= -fsanitize=undefined,bounds-strict -fsanitize-trap=all
# -iquote: Core/Inc/string.h must not shadow the system <string.h>
CFLAGS   = -std=gnu11 -Wall -Werror -g -O1 $(SANITIZE) -iquote . -iquote fakehal -iquote $(CORE)/Inc

TESTS = aprs tracker gps gnss si4063 fox diag analog

# firmware sources linked into each test
SRC_aprs    = aprs.c string.c
SRC_tracker = tracker.c
SRC_gps     = gps.c
SRC_gnss    = GNSS.c gps.c
SRC_si4063  = si4063.c
SRC_fox     = fox.c
SRC_diag    = diag.c
SRC_analog  = analog.c

BINS = $(TESTS:%=$(BUILD)/test_%)

all: $(BINS)
	@failed=""; \
	for t in $(BINS); do \
		$$t > $$t.log 2>&1 || failed="$$failed $${t##*/}"; \
		grep -E "^(FAIL|  FAIL)|^[a-z0-9]+: [0-9]+ checks" $$t.log; \
	done; \
	if [ -n "$$failed" ]; then \
		echo "FAILED:$$failed (logs in $(BUILD)/)"; exit 1; \
	fi; \
	echo "all host tests passed"

.SECONDEXPANSION:
$(BUILD)/test_%: test_%.c fake_hal.c test.h fake_hal.h fakehal/stm32f1xx_hal.h \
		$$(addprefix $(CORE)/Src/,$$(SRC_$$*)) $(wildcard $(CORE)/Inc/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_$*.c fake_hal.c $(addprefix $(CORE)/Src/,$(SRC_$*)) -lm

clean:
	rm -rf $(BUILD)

.PHONY: all clean
