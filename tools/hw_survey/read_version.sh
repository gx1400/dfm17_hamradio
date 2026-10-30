#!/bin/bash
# read_version.sh: which firmware is on the board connected to the ST-Link?
# Reads the 64 KB flash over SWD (hot-plug, the board keeps running) and finds
# the version string (version.c, "DFMFW <git describe> <variant>"), so it works
# without knowing which build was flashed. Firmware from before the version
# stamp has no such string.
cd "$(dirname "$0")/../.." || exit 1
source tools/hw_survey/common.sh
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

timeout 60 "$PROG" -c port=SWD mode=HOTPLUG -u 0x08000000 0x10000 "$TMP/flash.bin" >/dev/null 2>&1
[ -s "$TMP/flash.bin" ] || { echo "could not read the flash over SWD" >&2; exit 1; }
v=$(strings -a "$TMP/flash.bin" | grep -m1 '^DFMFW ')
if [ -z "$v" ]; then
	echo "no version string: firmware from before the version stamp (or not ours)"
	exit 2
fi
echo "${v#DFMFW }"
