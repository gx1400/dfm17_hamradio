# Host-side build of the firmware APRS encoder (dfm17/Core/Src/aprs.c).
#   make test   - build, then decode every mode with Dire Wolf's atest
CC      = gcc
CORE    = ../../dfm17/Core
# -iquote so Core/Inc/string.h does not shadow the system <string.h>
CFLAGS  = -Wall -O1 -iquote stubs -iquote $(CORE)/Inc -include stubs/GNSS.h
OUT     = out

aprs_harness: aprs_harness.c $(CORE)/Src/aprs.c $(CORE)/Src/string.c stubs/*.h
	$(CC) $(CFLAGS) -o $@ aprs_harness.c $(CORE)/Src/aprs.c $(CORE)/Src/string.c -lm

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

clean:
	rm -rf aprs_harness $(OUT)

.PHONY: test clean
