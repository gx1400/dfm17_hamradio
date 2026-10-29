# Host-side build of the firmware APRS encoder (dfm17/Core/Src/aprs.c).
#   make test   - build, then decode every mode with Dire Wolf's atest
CC      = gcc
CORE    = ../../dfm17/Core
# -iquote so Core/Inc/string.h does not shadow the system <string.h>
CFLAGS  = -Wall -O1 -iquote stubs -iquote $(CORE)/Inc -include stubs/GNSS.h
OUT     = out

aprs_harness: aprs_harness.c $(CORE)/Src/aprs.c $(CORE)/Src/string.c stubs/*.h
	$(CC) $(CFLAGS) -o $@ aprs_harness.c $(CORE)/Src/aprs.c $(CORE)/Src/string.c -lm

afsk_harness: afsk_harness.c $(CORE)/Src/aprs.c $(CORE)/Src/string.c stubs/*.h
	$(CC) $(CFLAGS) -o $@ afsk_harness.c $(CORE)/Src/aprs.c $(CORE)/Src/string.c -lm

# square wave vs sine AFSK through an FM receiver with and without de-emphasis
afsk-test: afsk_harness
	@mkdir -p $(OUT)
	@for m in square sine; do for d in '' --deemph; do \
		echo "=== $$m $$d ==="; \
		./afsk_harness $$m | python3 afsk_sim.py $$d -o $(OUT)/afsk_$$m$$d.wav; \
		atest $(OUT)/afsk_$$m$$d.wav 2>&1 | grep -E "packets decoded"; \
	done; done

test: aprs_harness
	@mkdir -p $(OUT)
	./aprs_harness fix > $(OUT)/bits_fix.txt
	@echo "=== GPS fix, ideal ==="
	@python3 aprs_sim.py --mode ideal -o $(OUT)/fix.wav < $(OUT)/bits_fix.txt
	@atest $(OUT)/fix.wav 2>&1 | grep -E "packets"
	./aprs_harness > $(OUT)/bits.txt
	@for m in ideal firmware si-sync; do \
		echo "=== $$m ==="; \
		python3 aprs_sim.py --mode $$m -o $(OUT)/$$m.wav < $(OUT)/bits.txt; \
		atest $(OUT)/$$m.wav 2>&1 | grep -E "^\[|decoded|packets"; \
	done

# pass/fail version of test and afsk-test, for CI: every packet must decode
# (si-sync is left out, it is the known-bad mode the firmware doesn't use)
check: aprs_harness afsk_harness
	@mkdir -p $(OUT)
	@set -e; \
	decodes() { atest $$1 2>&1 | grep -q "1 packets decoded" || { echo "FAIL: $$1 did not decode"; exit 1; }; }; \
	for fix in fix nofix; do \
		./aprs_harness $$fix > $(OUT)/bits_$$fix.txt; \
		for m in ideal firmware; do \
			python3 aprs_sim.py --mode $$m -o $(OUT)/$$fix-$$m.wav < $(OUT)/bits_$$fix.txt | grep -q "FCS GOOD" \
				|| { echo "FAIL: $$fix $$m frame check"; exit 1; }; \
			decodes $(OUT)/$$fix-$$m.wav; \
		done; \
	done; \
	for m in square sine; do for d in '' --deemph; do \
		./afsk_harness $$m | python3 afsk_sim.py $$d -o $(OUT)/afsk_$$m$$d.wav > /dev/null; \
		decodes $(OUT)/afsk_$$m$$d.wav; \
	done; done; \
	echo "aprs_sim: all packets decoded"

clean:
	rm -rf aprs_harness afsk_harness $(OUT)

.PHONY: test afsk-test check clean
