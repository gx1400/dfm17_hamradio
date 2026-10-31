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

TESTS = aprs tracker gps gnss si4063 fox diag analog ppsdisc settings ppscap

# firmware sources linked into each test
SRC_aprs    = aprs.c string.c settings.c
SRC_tracker = tracker.c settings.c
SRC_gps     = gps.c
SRC_gnss    = GNSS.c gps.c
SRC_si4063  = si4063.c settings.c
SRC_fox     = fox.c settings.c
SRC_diag    = diag.c ppscap.c
SRC_analog  = analog.c
SRC_ppsdisc = ppsdisc.c settings.c
SRC_settings = settings.c settings_store.c
SRC_ppscap  = ppscap.c

BINS = $(TESTS:%=$(BUILD)/test_%)

all: $(BINS)
	@failed=""; \
	for t in $(BINS); do \
		SETTINGS_LAYOUT=$(BUILD)/settings_layout.txt $$t > $$t.log 2>&1 \
			|| failed="$$failed $${t##*/}"; \
		grep -E "^(FAIL|  FAIL)|^[a-z0-9]+: [0-9]+ checks" $$t.log; \
	done; \
	if [ -n "$$failed" ]; then \
		echo "FAILED:$$failed (logs in $(BUILD)/)"; exit 1; \
	fi; \
	echo "all host tests passed"; \
	python3 ../config/dfmcfg.py --layout | diff - $(BUILD)/settings_layout.txt \
		|| { echo "FAILED: dfmcfg.py LAYOUT doesn't match settings_t"; exit 1; }; \
	python3 ../config/test_dfmcfg.py 2>&1 | tail -1; \
	python3 ../config/test_dfmcfg.py >/dev/null 2>&1 \
		|| { echo "FAILED: tools/config/test_dfmcfg.py"; exit 1; }; \
	echo "settings layout and dfmcfg.py checks OK"

.SECONDEXPANSION:
$(BUILD)/test_%: test_%.c fake_hal.c test.h fake_hal.h fakehal/stm32f1xx_hal.h \
		$$(addprefix $(CORE)/Src/,$$(SRC_$$*)) $(wildcard $(CORE)/Inc/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_$*.c fake_hal.c $(addprefix $(CORE)/Src/,$(SRC_$*)) -lm

# coverage: the same tests built with --coverage into build-cov/ (no
# sanitizer: its checks add branches of their own), run, and reported with
# gcovr (tools/requirements.txt) for dfm17/Core/Src. Writes coverage.md (also
# shown in the CI job summary) and html/index.html, then fails under the
# floors below, or if a source is neither tested nor in not_host_tested.txt.
COV       = build-cov
TESTED    = $(sort $(foreach t,$(TESTS),$(SRC_$(t))))
NOT_TESTED = $(shell sed -n 's/^\([A-Za-z0-9_]*\.c\):.*/\1/p' not_host_tested.txt)
UNLISTED  = $(filter-out $(TESTED) $(NOT_TESTED),$(notdir $(wildcard $(CORE)/Src/*.c)))
GCOVR     = gcovr --root ../.. --filter '../../dfm17/Core/Src/' $(COV)
# a floor, not a target: new logic still gets tests, but one untested line no
# longer fails the build (#40 reached 100% of lines and functions)
FLOORS    = --fail-under-line 90 --fail-under-function 90 --fail-under-branch 90

coverage:
	rm -rf $(COV)
	$(MAKE) BUILD=$(COV) SANITIZE="--coverage -O0" all
	$(GCOVR) --txt-summary --txt $(COV)/coverage.txt
	@mkdir -p $(COV)/html
	$(GCOVR) --html-details $(COV)/html/index.html --markdown $(COV)/coverage.md \
		--markdown-title "Host test coverage of dfm17/Core/Src"
	@{ echo; echo "**Not host-tested, by design** (not_host_tested.txt):"; echo; \
		sed -n 's/^\([A-Za-z0-9_]*\.c\): \(.*\)/- `\1`: \2/p' not_host_tested.txt; \
		if [ -n "$(UNLISTED)" ]; then echo; \
			echo "**Not tested and not listed:** $(UNLISTED). Add a test, or a line with the reason to tools/tests/not_host_tested.txt."; \
		fi; } >> $(COV)/coverage.md
	@echo "report: $(COV)/coverage.md, $(COV)/html/index.html"
	@if [ -n "$(UNLISTED)" ]; then \
		echo "FAILED: not built into any host test and not in not_host_tested.txt: $(UNLISTED)"; exit 1; fi
	@$(GCOVR) $(FLOORS) > /dev/null || { echo "FAILED: coverage under $(FLOORS)"; exit 1; }
	@echo "coverage floors met: $(FLOORS)"

clean:
	rm -rf $(BUILD) $(COV)

.PHONY: all clean coverage
