#!/bin/sh
# Frequency sweep of the DFM-17 transmitter with a HackRF.
# Needs a build with RF_TEST_SWD defined (main.c) running on the board, an
# ST-Link on SWD, and the HackRF near the board (dummy load on the DFM-17!).
#   usage: sweep.sh dfm17.elf [power 0x00-0x7f] [freq_hz ...]
# Prints: target MHz, measured MHz, offset Hz, offset ppm, level dB
. "$(dirname "$0")/../hw_survey/common.sh"
ELF=${1:?usage: $0 dfm17.elf [power] [freq_hz ...]}
POWER=${2:-0x00}
shift 2 2>/dev/null
FREQS=${*:-"420000000 422500000 425000000 427500000 430000000 432500000 435000000 437500000 440000000 442500000 445000000 446175000 447500000 450000000"}
FA=$(sym "$ELF" rf_test_freq_hz)
PA=$(sym "$ELF" rf_test_power)
[ -n "$FA" ] || { echo "rf_test_freq_hz not in $ELF (build with RF_TEST_SWD)" >&2; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
echo "target_MHz measured_MHz offset_Hz ppm level_dB   (PA_PWR_LVL $POWER)"
for f in $FREQS; do
	# tune 100 kHz below the carrier to keep it off the HackRF's DC spike;
	# capture with the carrier on, then off, to tell it from HackRF spurs
	for state in on off; do
		[ $state = on ] && v=$f || v=0
		timeout 30 "$PROG" -c port=SWD mode=HOTPLUG -w8 "0x$PA" "$POWER" -w32 "0x$FA" "$v" >/dev/null 2>&1
		sleep 1
		timeout 20 hackrf_transfer -r "$TMP/$state.iq" -f $((f - 100000)) -s 2000000 -a 0 -l 32 -g 30 -n 1000000 >/dev/null 2>&1
	done
	python3 -I "$(dirname "$0")/measure.py" "$TMP/on.iq" "$TMP/off.iq" $((f - 100000)) "$f"
done
timeout 30 "$PROG" -c port=SWD mode=HOTPLUG -w32 "0x$FA" 0 >/dev/null 2>&1
