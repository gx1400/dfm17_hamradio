#!/bin/sh
# Print the firmware's analog_last (supply voltage/current, VDDA, MCU temp) once
# a second, for calibrating against a bench supply and meter.
#   usage: read_analog.sh path/to/dfm17.elf   (the ELF that is on the board)
. "$(dirname "$0")/common.sh"
ELF=${1:?usage: $0 dfm17.elf}
A=$(sym "$ELF" analog_last)
[ -n "$A" ] || { echo "analog_last not in $ELF" >&2; exit 1; }
echo "vin_raw iin_raw vref_raw  VDDA_mV  Vin_mV  Iin_mA  temp_C usb_raw  USB_mV  power"
while :; do
	readmem "$A" 20 | python3 -I -c '
import sys, struct
b = bytes(int(h, 16) for h in sys.stdin.read().split())
vin, iin, vref, vdda, vmv, ima, t, ur, umv, on = struct.unpack("<6Hh2HB", b[:19])
print(f"{vin:7d} {iin:7d} {vref:8d}  {vdda:7d}  {vmv:6d}  {ima:6d}  {t:6d} {ur:7d}  {umv:6d}  " + ("USB" if on else "BAT"))'
	sleep 1
done
