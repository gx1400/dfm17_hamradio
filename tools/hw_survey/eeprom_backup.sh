#!/bin/bash
# eeprom_backup.sh [--yes] [OUTDIR] [NOTE]: full backup of a DFM-17's NFC EEPROM
# (M24LR04E-R): all 512 bytes of user memory, including Graw's factory data,
# and every readable byte of the system area.
#
# It flashes the nfc_tool build over SWD (ST-Link): that REPLACES THE FIRMWARE
# on the board. The tool build never transmits. Flash the firmware you want
# afterwards. OUTDIR defaults to docs/data; NOTE is added to the file header,
# e.g. "board 2, label 2005 0131". Writes eeprom-M24LR04E-<UID>-<date>.txt and
# .bin; please commit them (or open an issue with them) so the Graw data of
# many boards is kept.
cd "$(dirname "$0")/../.." || exit 1
YES=0
[ "$1" = "--yes" ] && { YES=1; shift; }
OUT=${1:-docs/data}
NOTE=$2
source tools/hw_survey/common.sh
ELF=build/nfc_tool/dfm17.elf
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

if [ $YES -ne 1 ]; then
	echo "This flashes the EEPROM tool build onto the board connected to the ST-Link,"
	echo "replacing its firmware. Continue? [y/N]"
	read -r a
	[ "$a" = y ] || [ "$a" = Y ] || exit 1
fi

make -s "$ELF" || exit 1
BACKUP=$(sym "$ELF" nfc_backup)
TOOL=$(sym "$ELF" nfc_tool)
LEN=604		# sizeof(nfc_backup): 12 + 512 + 16 + 16 + 48

echo "flashing the nfc_tool build"
for i in 1 2 3 4; do
	timeout 60 "$PROG" -c port=SWD mode=HOTPLUG -d "$ELF" -v -rst 2>&1 \
		| sed 's/\x1b\[[0-9;]*m//g' | grep -q 'Download verified successfully' && break
	[ $i -eq 4 ] && { echo "flashing failed" >&2; exit 1; }
	echo "retry $i"
done

# the tool takes a backup at start-up, after the GPS and radio init
echo "waiting for the first read"
for _ in $(seq 30); do
	readmem "$BACKUP" "$LEN" > "$TMP/a"
	[ "$(awk '{print $4$3$2$1}' "$TMP/a")" = 4B434142 ] && break
	sleep 1
done
[ "$(awk '{print $4$3$2$1}' "$TMP/a")" = 4B434142 ] || { echo "no backup from the tool" >&2; exit 1; }

# a second, independent read of the chip; both must match
done0=$(readmem "$TOOL" 12 | awk '{print $12$11$10$9}')
timeout 30 "$PROG" -c port=SWD mode=HOTPLUG -w8 "0x$TOOL" 0x04 >/dev/null 2>&1
for _ in $(seq 15); do
	[ "$(readmem "$TOOL" 12 | awk '{print $12$11$10$9}')" != "$done0" ] && break
done
readmem "$BACKUP" "$LEN" > "$TMP/b"
# compare everything after the 8-byte magic/count header
if [ "$(cut -d' ' -f10- "$TMP/a")" != "$(cut -d' ' -f10- "$TMP/b")" ]; then
	echo "the two reads differ; check the connection and run again" >&2
	exit 1
fi

mkdir -p "$OUT"
txt=$(python3 -I tools/hw_survey/eeprom_format.py "$TMP/b" "$OUT" "$NOTE") || exit 1
echo "backup written: $txt and ${txt%.txt}.bin (two reads matched)"
