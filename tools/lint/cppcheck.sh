#!/bin/bash
# cppcheck.sh: static analysis of our C sources (dfm17/Core). The vendor HAL
# and CMSIS headers are on the include path for types but never reported.
# Fails on any finding not in cppcheck-suppressions.txt.
cd "$(dirname "$0")/../.." || exit 1
command -v cppcheck >/dev/null || { echo "cppcheck not installed" >&2; exit 2; }
cppcheck --version
exec cppcheck --std=c11 --enable=warning,style,performance,portability \
	--inline-suppr --error-exitcode=1 -q \
	--suppressions-list=tools/lint/cppcheck-suppressions.txt \
	--suppress='*:dfm17/Drivers/*' \
	-DSTM32F100xB -DUSE_HAL_DRIVER -D__GNUC__ \
	-Idfm17/Core/Inc -Idfm17/Drivers/STM32F1xx_HAL_Driver/Inc \
	-Idfm17/Drivers/CMSIS/Device/ST/STM32F1xx/Include -Idfm17/Drivers/CMSIS/Include \
	--template='{file}:{line}: {severity}: {id}: {message}' \
	dfm17/Core/Src "$@"
