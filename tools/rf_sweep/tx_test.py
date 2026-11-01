#!/usr/bin/env python3
"""tx_test.py: run a transmit test on a board with the rf_test build.

    tx_test.py cw   [--text "VVV TEST"] [--wpm 20]
    tx_test.py cwid
    tx_test.py rtty [--text "RYRYRY TEST"] [--baud 45.45] [--shift 170] [--bits 5] [--stop 1.5]

Writes rf_test_power and the rf_test_tx block over SWD (STM32_Programmer_CLI
in hot-plug mode, as dfmcfg.py --link cube), then waits for rf_test_tx_done
to count up and prints the result. The board appends "DE <callsign>" to every
message, so each test identifies. Transmits on the settings' frequency at
--power (PA_PWR_LVL, default 0x00, the lowest): use a dummy load.

Addresses come from the ELF's symbol table (arm-none-eabi-nm), so pass the
build that's on the board with --elf. docs/tx-test-modes.md has the details.
"""

import argparse
import glob
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "config"))
from dfmcfg import CubeProgrammerLink, SettingsError

CMDS = {"cw": 1, "cwid": 2, "rtty": 3}
RESULTS = {
    1: "ok",
    2: "bad parameters",
    3: "refused (transmit gate, or the test carrier is on)",
    4: "unknown command",
}
TEXT_LEN = 48
# rtty_params_t (baud_x100, shift_hz, bits, stop_halves, reverse, reserved),
# wpm, 3 reserved, text, cmd: rf_test_tx_t in rf_test.c
LAYOUT = f"<HHBBBBB3x{TEXT_LEN}sI"
assert struct.calcsize(LAYOUT) == 64


def find_nm():
    hit = shutil.which("arm-none-eabi-nm")
    if hit:
        return hit
    for pattern in (
        "/opt/st/stm32cubeide_*/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-nm",
        "C:/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-nm.exe",
    ):
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[-1]
    raise SettingsError("can't find arm-none-eabi-nm (STM32CubeIDE, or on PATH)")


def symbols(elf, names):
    out = subprocess.run([find_nm(), elf], capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in names:
            found[parts[2]] = int(parts[0], 16)
    missing = set(names) - set(found)
    if missing:
        raise SettingsError(f"{', '.join(sorted(missing))} not in {elf}: is it the rf_test build?")
    return found


def stop_halves(stop):
    halves = round(float(stop) * 2)
    if halves not in (2, 3, 4):
        raise SettingsError("--stop is 1, 1.5 or 2")
    return halves


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("mode", choices=sorted(CMDS))
    ap.add_argument("--elf", default="build/rf_test/dfm17.elf")
    ap.add_argument(
        "--power",
        type=lambda s: int(s, 0),
        default=0x00,
        help="PA_PWR_LVL 0x00-0x7f (default 0x00)",
    )
    ap.add_argument(
        "--text", default="RYRYRY TEST", help=f"message, up to {TEXT_LEN - 1} characters"
    )
    ap.add_argument("--wpm", type=int, default=0, help="CW speed, 0 = the settings' cw_wpm")
    ap.add_argument("--baud", type=float, default=45.45)
    ap.add_argument("--shift", type=int, default=170, help="Hz")
    ap.add_argument(
        "--bits", type=int, default=5, choices=(5, 7, 8), help="5 = ITA2 (Baudot), 7/8 = ASCII"
    )
    ap.add_argument("--stop", default="1.5", help="stop bits: 1, 1.5 or 2")
    ap.add_argument("--reverse", action="store_true", help="mark on the lower frequency")
    ap.add_argument("--timeout", type=float, default=180, help="s to wait for the board")
    a = ap.parse_args()

    try:
        text = a.text.encode("ascii")
        if len(text) >= TEXT_LEN:
            raise SettingsError(f"--text is at most {TEXT_LEN - 1} characters")
        if not 0 <= a.power <= 0x7F:
            raise SettingsError("--power is 0x00-0x7f")
        sym = symbols(
            a.elf, ["rf_test_tx", "rf_test_tx_done", "rf_test_tx_result", "rf_test_power"]
        )
        raw = struct.pack(
            LAYOUT,
            round(a.baud * 100),
            a.shift,
            a.bits,
            stop_halves(a.stop),
            int(a.reverse),
            0,
            a.wpm,
            text,
            CMDS[a.mode],
        )
        link = CubeProgrammerLink()
        done = struct.unpack("<I", link.read(sym["rf_test_tx_done"], 4))[0]
        link.write(sym["rf_test_power"], bytes([a.power]))
        t0 = time.monotonic()
        # one write; the command is the last word, so the board sees it last
        link.write(sym["rf_test_tx"], raw)
        while time.monotonic() - t0 < a.timeout:
            time.sleep(1)
            if struct.unpack("<I", link.read(sym["rf_test_tx_done"], 4))[0] != done:
                break
        else:
            raise SettingsError(f"no answer in {a.timeout:.0f} s: is the rf_test build running?")
        result = link.read(sym["rf_test_tx_result"], 1)[0]
        print(f"{a.mode}: {RESULTS.get(result, result)} ({time.monotonic() - t0:.1f} s)")
        return 0 if result == 1 else 1
    except SettingsError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
