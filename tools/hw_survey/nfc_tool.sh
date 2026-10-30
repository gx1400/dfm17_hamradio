#!/bin/bash
# nfc_tool.sh: drive the NFC EEPROM tool build (make variant nfc_tool, issue #8)
# over SWD while it runs.
#
#   nfc_tool.sh show              read the system area and print it
#   nfc_tool.sh present [PW]      present the I2C password (hex, default 00000000)
#   nfc_tool.sh sss3 VALUE        write sector 3's security status (hex), read back
#
# The firmware only ever writes SSS[3]. Values are passed to the programmer in
# hex: STM32_Programmer_CLI misreads decimal values.
cd "$(dirname "$0")/../.." || exit 1
source tools/hw_survey/common.sh
ELF=build/nfc_tool/dfm17.elf
[ -f "$ELF" ] || { echo "build it first: make firmware" >&2; exit 1; }
BASE=$(sym "$ELF" nfc_tool)
[ -n "$BASE" ] || { echo "nfc_tool not found in $ELF" >&2; exit 1; }

w() {	# w8|w32 OFFSET VALUE
	timeout 30 "$PROG" -c port=SWD mode=HOTPLUG -"$1" "$(printf '0x%08X' $((0x$BASE + $2)))" "$3" >/dev/null 2>&1
}
le32() { printf '%d' "0x$4$3$2$1"; }

dump() {
	read -ra b <<< "$(readmem "$BASE" 40)"
	[ ${#b[@]} -eq 40 ] || { echo "SWD read failed" >&2; return 1; }
	local status=(none OK NACK TIMEOUT BAD_CMD)
	echo "commands done  $(le32 "${b[@]:8:4}"), last status ${status[0x${b[12]}]:-0x${b[12]}}, poll ${b[13]}h ms"
	echo "SSS 0-3        ${b[14]} ${b[15]} ${b[16]} ${b[17]}"
	echo "I2C_Write_Lock ${b[18]} ${b[19]}"
	echo "config         ${b[20]} (bit 3 = 1: RF WIP mode, 0: RF BUSY mode)   control ${b[21]}"
	echo "UID            ${b[29]}${b[28]}${b[27]}${b[26]}${b[25]}${b[24]}${b[23]}${b[22]}"
	echo "read ok        ${b[30]}, PC13 level ${b[31]}, PC13 falling edges $(le32 "${b[@]:32:4}"), last at $(le32 "${b[@]:36:4}") ms"
}

run() {	# run CMD: start a command and wait until the done count moves
	local before after
	before=$(readmem "$BASE" 12 | awk '{print $12$11$10$9}')
	w w8 0 "$(printf '0x%02X' "$1")"
	for _ in $(seq 15); do
		after=$(readmem "$BASE" 12 | awk '{print $12$11$10$9}')
		[ "$after" != "$before" ] && return 0
	done
	echo "command $1 didn't complete" >&2
	return 1
}

case "$1" in
show)
	run 1 && dump ;;
present)
	pw=${2:-00000000}
	w w32 4 "0x$pw" && run 2 && dump ;;
sss3)
	[ -n "$2" ] || { echo "usage: $0 sss3 VALUE (hex)" >&2; exit 1; }
	w w8 1 "0x$2" && run 3 && dump ;;
*)
	sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
