# shared helpers: locate the STM32CubeIDE toolchain and CubeProgrammer CLI,
# and read target RAM over SWD without halting the MCU (hot-plug)
IDE=$(ls -d /opt/st/stm32cubeide_* 2>/dev/null | sort | tail -1)
NM=$(ls "$IDE"/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-nm 2>/dev/null | tail -1)
PROG=$(ls "$IDE"/plugins/*cubeprogrammer.linux64*/tools/bin/STM32_Programmer_CLI 2>/dev/null | tail -1)
[ -x "$NM" ] && [ -x "$PROG" ] || { echo "STM32CubeIDE toolchain/programmer not found under /opt/st" >&2; exit 1; }

# sym ELF NAME -> hex address
sym() { "$NM" "$1" | awk -v n="$2" '$3==n {print $1}'; }

# readmem ADDR LEN -> space-separated hex bytes
readmem() {
	timeout 30 "$PROG" -c port=SWD mode=HOTPLUG -r8 "0x$1" "$2" 2>&1 \
		| sed 's/\x1b\[[0-9;]*m//g' | grep '^0x' | cut -d: -f2 | tr -s ' \n' ' '
}
