#!/usr/bin/env python3
"""dfmcfg.py: read and change a DFM-17's station settings over SWD (ST-Link).

    dfmcfg.py show
    dfmcfg.py set callsign=NZ0P ssid=12 freq=446.175MHz [--save]
    dfmcfg.py save | reload | defaults
    dfmcfg.py check callsign=NZ0P freq=446175000 ...   (offline, no board)
    dfmcfg.py fields                                   (what can be set)

Settings live in the NFC EEPROM (docs/settings.md); the firmware loads them at
boot. A board with no settings uses the defaults: callsign N0CALL, and it
doesn't transmit until a real callsign is set and saved.

Every value is checked here first, against the same limits as the firmware
(settings_check in dfm17/Core/Src/settings.c), with a message naming the field.
The firmware checks again and refuses anything invalid without changing what
it runs, so a mistyped value can't take a board out of band or off the air.
`set` applies the change at once; it survives a reset only after `save`
(or `set ... --save`).

The board must run the build given with --elf (default build/tracker/dfm17.elf):
the script compares its version string with the board's flash first.
"""

import argparse
import binascii
import glob
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

SIZE = 64
VERSION = 1

# name, offset, struct format. Must match settings_t in dfm17/Core/Inc/settings.h:
# make -C tools/tests compares this with the offsets the C compiler uses.
LAYOUT = [
    ("magic", 0, "2s"),
    ("version", 2, "B"),
    ("length", 3, "B"),
    ("callsign", 4, "6s"),
    ("ssid", 10, "B"),
    ("symbol_table", 11, "c"),
    ("symbol_code", 12, "c"),
    ("path", 13, "B"),
    ("mode", 14, "B"),
    ("flags", 15, "B"),
    ("freq_hz", 16, "<I"),
    ("pa_level", 20, "B"),
    ("cw_wpm", 21, "B"),
    ("dev_hz", 22, "<H"),
    ("tracker_period_s", 24, "<H"),
    ("tracker_fix_timeout_s", 26, "<H"),
    ("tracker_settle_s", 28, "<H"),
    ("fox_period_s", 30, "<H"),
    ("fox_tone_ms", 32, "<H"),
    ("fox_aprs_every", 34, "B"),
    ("reserved", 35, "27s"),
    ("crc", 62, "<H"),
]

# enum settings_error / settings_source / result, in the firmware's order
ERRORS = [
    "OK",
    "MAGIC",
    "VERSION",
    "CRC",
    "CALLSIGN",
    "SSID",
    "SYMBOL",
    "PATH",
    "MODE",
    "FREQ",
    "POWER",
    "CW",
    "DEVIATION",
    "TRACKER_TIMING",
    "FOX_TIMING",
    "EEPROM",
]
SOURCES = ["built-in defaults", "EEPROM", "SWD (not saved)"]
RESULTS = ["none", "OK", "INVALID", "EEPROM", "BAD_CMD"]
CMD = {"edit": 1, "apply": 2, "save": 3, "reload": 4, "defaults": 5}
FLAG_START_PAUSED = 0x01
MODES = {"tracker": 0, "fox": 1}
PATHS = {"WIDE1-1": 0x11, "WIDE2-1": 0x21, "WIDE2-2": 0x22}


class SettingsError(Exception):
    pass


# ---- the settings block --------------------------------------------------------


def unpack(raw):
    s = {}
    for name, off, fmt in LAYOUT:
        (s[name],) = struct.unpack_from(fmt, raw, off)
    return s


def pack(s):
    raw = bytearray(SIZE)
    for name, off, fmt in LAYOUT:
        struct.pack_into(fmt, raw, off, s[name])
    return raw


def crc(s):
    """CRC-16/CCITT-FALSE of the bytes before the crc field, as settings_crc()"""
    return binascii.crc_hqx(bytes(pack(s)[: SIZE - 2]), 0xFFFF)


def seal(s):
    """magic, version, length and CRC, as settings_seal() does"""
    s.update(magic=b"DF", version=VERSION, length=SIZE)
    s["crc"] = crc(s)
    return s


def callsign_str(s):
    return s["callsign"].rstrip(b"\0").decode("ascii", "replace")


def callsign_ok(c):
    return (
        bool(re.fullmatch(r"[A-Z0-9]{3,6}", c)) and re.search(r"[A-Z]", c) and re.search(r"\d", c)
    )


def check(s):
    """the firmware's settings_check(): returns the error name, or 'OK'"""
    if s["magic"] != b"DF":
        return "MAGIC"
    if s["version"] != VERSION or s["length"] != SIZE:
        return "VERSION"
    if s["crc"] != crc(s):
        return "CRC"
    raw = s["callsign"]
    text = raw.rstrip(b"\0")
    if b"\0" in text or not callsign_ok(text.decode("ascii", "replace")):
        return "CALLSIGN"
    if s["ssid"] > 15:
        return "SSID"
    t, c = s["symbol_table"], s["symbol_code"]
    if not (t in (b"/", b"\\") or b"A" <= t <= b"Z") or not (b"!" <= c <= b"~"):
        return "SYMBOL"
    n, hops = s["path"] >> 4, s["path"] & 0x0F
    if not (1 <= n <= 2 and 1 <= hops <= n):
        return "PATH"
    if s["mode"] > 1:
        return "MODE"
    if not 420_000_000 <= s["freq_hz"] <= 450_000_000:
        return "FREQ"
    if s["pa_level"] > 0x7F:
        return "POWER"
    if not 5 <= s["cw_wpm"] <= 40:
        return "CW"
    if not 1000 <= s["dev_hz"] <= 5000:
        return "DEVIATION"
    if (
        s["tracker_period_s"] < 30
        or s["tracker_fix_timeout_s"] < 10
        or s["tracker_fix_timeout_s"] >= s["tracker_period_s"]
        or s["tracker_settle_s"] >= s["tracker_fix_timeout_s"]
    ):
        return "TRACKER_TIMING"
    if (
        s["fox_period_s"] < 10
        or s["fox_tone_ms"] > 60000
        or s["fox_tone_ms"] + 8000 > s["fox_period_s"] * 1000
    ):
        return "FOX_TIMING"
    return "OK"


WHY = {
    "CALLSIGN": "callsign: 3-6 of A-Z and 0-9, with at least one letter and one digit",
    "SSID": "ssid: 0-15",
    "SYMBOL": "symbol: table '/', '\\' or an overlay A-Z, then a printable code, e.g. /O",
    "PATH": "path: WIDE1-1, WIDE2-1 or WIDE2-2",
    "MODE": "mode: tracker or fox",
    "FREQ": "freq: 420-450 MHz (the 70 cm band; the radio's output divider covers it)",
    "POWER": "power: PA level 0-127 (0x00-0x7F)",
    "CW": "cw_wpm: 5-40",
    "DEVIATION": "deviation: 1000-5000 Hz",
    "TRACKER_TIMING": "tracker timing: period >= 30 s, 10 s <= fix timeout < period, "
    "settle < fix timeout",
    "FOX_TIMING": "fox timing: period >= 10 s, tone <= 60 s, and tone + 8 s must fit in the period",
}


def parse_int(name, v, lo, hi):
    try:
        n = int(v, 0)
    except ValueError:
        raise SettingsError(f"{name}: '{v}' isn't a whole number") from None
    if not lo <= n <= hi:
        raise SettingsError(f"{name}: {n} is outside {lo}-{hi}")
    return n


def parse_freq(v):
    m = re.fullmatch(r"(\d+(?:\.\d+)?)\s*(hz|khz|mhz)?", v.strip().lower())
    if not m:
        raise SettingsError(f"freq: '{v}' isn't a frequency, e.g. 446175000 or 446.175MHz")
    number, unit = m.groups()
    if unit is None:
        if "." in number:
            raise SettingsError(f"freq: '{v}' needs a unit (446.175MHz) or whole Hz (446175000)")
        unit = "hz"
    hz = round(float(number) * {"hz": 1, "khz": 1e3, "mhz": 1e6}[unit])
    if not 420_000_000 <= hz <= 450_000_000:
        raise SettingsError(f"freq: {hz} Hz is outside 420-450 MHz; check the digits and unit")
    return hz


def apply_assignment(s, item):
    """one name=value from the command line, checked on its own"""
    if "=" not in item:
        raise SettingsError(f"'{item}': expected name=value (see 'dfmcfg.py fields')")
    name, v = (x.strip() for x in item.split("=", 1))
    name = name.lower()
    if name == "callsign":
        c = v.upper()
        if "-" in c:
            raise SettingsError("callsign: give the SSID separately (ssid=12)")
        if not callsign_ok(c):
            raise SettingsError(f"callsign: '{v}' isn't valid; {WHY['CALLSIGN'].split(': ')[1]}")
        if c == "N0CALL":
            raise SettingsError("callsign: N0CALL is the 'not set up' placeholder")
        s["callsign"] = c.encode().ljust(6, b"\0")
    elif name == "ssid":
        s["ssid"] = parse_int(name, v, 0, 15)
    elif name == "symbol":
        if len(v) != 2:
            raise SettingsError(f"symbol: '{v}' should be two characters, table then code, e.g. /O")
        s["symbol_table"], s["symbol_code"] = v[0].encode(), v[1].encode()
    elif name == "path":
        if v.upper() not in PATHS:
            raise SettingsError(f"path: '{v}' isn't one of {', '.join(PATHS)}")
        s["path"] = PATHS[v.upper()]
    elif name == "mode":
        if v.lower() not in MODES:
            raise SettingsError(f"mode: '{v}' isn't tracker or fox")
        s["mode"] = MODES[v.lower()]
    elif name == "start_paused":
        on = {"yes": 1, "no": 0, "1": 1, "0": 0, "true": 1, "false": 0}.get(v.lower())
        if on is None:
            raise SettingsError(f"start_paused: '{v}' isn't yes or no")
        s["flags"] = (s["flags"] & ~FLAG_START_PAUSED) | (FLAG_START_PAUSED if on else 0)
    elif name in ("freq", "freq_hz"):
        s["freq_hz"] = parse_freq(v)
    elif name in ("power", "pa_level"):
        s["pa_level"] = parse_int(name, v, 0, 0x7F)
    elif name == "cw_wpm":
        s["cw_wpm"] = parse_int(name, v, 5, 40)
    elif name in ("deviation", "dev_hz"):
        s["dev_hz"] = parse_int(name, v, 1000, 5000)
    elif name in ("tracker_period_s", "tracker_fix_timeout_s", "tracker_settle_s", "fox_period_s"):
        s[name] = parse_int(name, v, 0, 65535)
    elif name == "fox_tone_ms":
        s[name] = parse_int(name, v, 0, 60000)
    elif name == "fox_aprs_every":
        s[name] = parse_int(name, v, 0, 255)
    else:
        raise SettingsError(f"'{name}' isn't a setting (see 'dfmcfg.py fields')")


def describe(s):
    path = next((k for k, v in PATHS.items() if v == s["path"]), f"0x{s['path']:02x}?")
    mode = next((k for k, v in MODES.items() if v == s["mode"]), f"{s['mode']}?")
    sym = (s["symbol_table"] + s["symbol_code"]).decode("ascii", "replace")
    return "\n".join(
        [
            f"  callsign      {callsign_str(s)}-{s['ssid']}",
            f"  symbol        {sym}",
            f"  path          {path}",
            f"  mode          {mode}"
            + ("  (starts paused)" if s["flags"] & FLAG_START_PAUSED else ""),
            f"  freq          {s['freq_hz']} Hz ({s['freq_hz'] / 1e6:.4f} MHz)",
            f"  power         {s['pa_level']} (0x{s['pa_level']:02X})",
            f"  deviation     {s['dev_hz']} Hz",
            f"  cw_wpm        {s['cw_wpm']}",
            (
                f"  tracker       period {s['tracker_period_s']} s, fix timeout "
                f"{s['tracker_fix_timeout_s']} s, settle {s['tracker_settle_s']} s"
            ),
            (
                f"  fox           period {s['fox_period_s']} s, tone {s['fox_tone_ms']} ms, "
                f"APRS every {s['fox_aprs_every'] or 'never'}"
            ),
        ]
    )


FIELDS_HELP = """settings (name=value):
  callsign=NZ0P          3-6 of A-Z 0-9 (lowercase is uppercased), no SSID
  ssid=12                0-15
  symbol=/O              table then code: /O balloon, /> car, /[ jogger
  path=WIDE1-1           WIDE1-1, WIDE2-1 or WIDE2-2
  mode=tracker           tracker or fox
  start_paused=no        yes: no transmissions until a long button press
  freq=446.175MHz        420-450 MHz; whole Hz, or with a unit (kHz, MHz)
  power=32               PA level 0-127 (0x7F)
  deviation=2600         Hz, 1000-5000
  cw_wpm=15              5-40
  tracker_period_s=300   >= 30
  tracker_fix_timeout_s=120   >= 10 and < period
  tracker_settle_s=10    < fix timeout
  fox_period_s=30        >= 10
  fox_tone_ms=10000      <= 60000, and tone + 8 s must fit in the period
  fox_aprs_every=10      APRS after every Nth fox cycle, 0 = never"""


# ---- SWD -----------------------------------------------------------------------


def find_tool(names, pattern):
    for n in names:
        if shutil.which(n):
            return shutil.which(n)
    hits = sorted(glob.glob(pattern))
    if hits:
        return hits[-1]
    raise SettingsError(f"can't find {names[0]} (STM32CubeIDE under /opt/st, or on PATH)")


class Board:
    def __init__(self, elf):
        self.elf = elf
        self.prog = find_tool(
            ["STM32_Programmer_CLI"],
            "/opt/st/stm32cubeide_*/plugins/*cubeprogrammer.linux64*/tools/bin/STM32_Programmer_CLI",
        )
        nm = find_tool(
            ["arm-none-eabi-nm"],
            "/opt/st/stm32cubeide_*/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-nm",
        )
        out = subprocess.run([nm, "-S", elf], capture_output=True, text=True, check=True).stdout
        self.sym = {}
        for line in out.splitlines():
            f = line.split()
            if len(f) == 4:
                self.sym[f[3]] = (int(f[0], 16), int(f[1], 16))
        for need in ("cfg", "settings_edit", "settings_cmd", "settings_result", "settings_done"):
            if need not in self.sym:
                raise SettingsError(f"{elf} has no '{need}': a build without EEPROM settings?")

    def _run(self, *args):
        r = subprocess.run(
            [self.prog, "-c", "port=SWD", "mode=HOTPLUG", *args],
            check=False,  # failures show up as missing data; read() retries
            capture_output=True,
            text=True,
            timeout=60,
        )
        return re.sub(r"\x1b\[[0-9;]*m", "", r.stdout)

    def read(self, addr, n):
        for _ in range(3):
            out = self._run("-r8", hex(addr), str(n))
            data = []
            for line in out.splitlines():
                if line.startswith("0x") and ":" in line:
                    data += [int(x, 16) for x in line.split(":", 1)[1].split()]
            if len(data) >= n:
                return bytes(data[:n])
        raise SettingsError("SWD read failed: is the ST-Link connected and the board powered?")

    def read_sym(self, name):
        addr, size = self.sym[name]
        return self.read(addr, size)

    def write_words(self, addr, raw):
        words = struct.unpack(f"<{len(raw) // 4}I", raw)
        self._run("-w32", hex(addr), *(f"0x{w:08X}" for w in words))

    def write_byte(self, addr, value):
        self._run("-w8", hex(addr), f"0x{value:02X}")

    def check_build(self):
        """the board must run this ELF: compare its version string with flash"""
        if "fw_version" not in self.sym:
            return
        addr, size = self.sym["fw_version"]
        with tempfile.TemporaryDirectory() as d:
            objcopy = self.elf_bytes(addr, size, d)
        if objcopy != self.read(addr, size):
            raise SettingsError(
                f"the board isn't running {self.elf} (its version string differs). "
                "Pass --elf for the build that's on it, or flash this one."
            )

    def elf_bytes(self, addr, size, tmpdir):
        objcopy = find_tool(
            ["arm-none-eabi-objcopy"],
            "/opt/st/stm32cubeide_*/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-objcopy",
        )
        out = os.path.join(tmpdir, "flash.bin")
        subprocess.run([objcopy, "-O", "binary", self.elf, out], check=True)
        with open(out, "rb") as f:
            data = f.read()
        return data[addr - 0x08000000 : addr - 0x08000000 + size]

    def u32(self, name):
        if name not in self.sym:
            return None
        return struct.unpack("<I", self.read_sym(name)[:4])[0]

    def u8(self, name):
        return self.read_sym(name)[0] if name in self.sym else None

    def command(self, name):
        done = self.u32("settings_done")
        self.write_byte(self.sym["settings_cmd"][0], CMD[name])
        for _ in range(20):
            if self.u32("settings_done") != done:
                return RESULTS[self.u8("settings_result")]
        raise SettingsError("the firmware didn't run the command (still booting? try again)")


def status(board):
    s = unpack(board.read_sym("cfg"))
    src = SOURCES[board.u8("settings_source")]
    err = ERRORS[board.u8("settings_error")]
    ok = check(s) == "OK" and callsign_str(s) != "N0CALL"
    print(f"settings from {src}; last load/apply check: {err}")
    print(describe(s))
    print(f"  transmit      {'allowed' if ok else 'DISABLED (no callsign set up)'}")
    refused, repairs = board.u32("si4060_tx_refused"), board.u32("settings_repairs")
    if refused or repairs:
        print(f"  key-ups refused {refused}, live settings restored {repairs} times")
    return s


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--elf", default="build/tracker/dfm17.elf", help="the build on the board")
    ap.add_argument("--save", action="store_true", help="with set: also save to the EEPROM")
    ap.add_argument("--layout", action="store_true", help="print the field offsets and exit")
    ap.add_argument(
        "command",
        nargs="?",
        choices=["show", "set", "save", "reload", "defaults", "check", "fields"],
    )
    ap.add_argument("values", nargs="*", help="name=value, for set and check")
    a = ap.parse_args()

    if a.layout:
        for name, off, fmt in LAYOUT:
            print(name, off, struct.calcsize(fmt))
        return 0
    if a.command in (None, "fields"):
        print(FIELDS_HELP)
        return 0
    try:
        if a.command == "check":
            # offline: the defaults with these changes
            s = unpack(bytes(SIZE))
            s.update(
                magic=b"DF",
                version=VERSION,
                length=SIZE,
                callsign=b"N0CALL",
                ssid=12,
                symbol_table=b"/",
                symbol_code=b"O",
                path=0x11,
                mode=0,
                flags=0,
                freq_hz=446175000,
                pa_level=0x20,
                cw_wpm=15,
                dev_hz=2600,
                tracker_period_s=300,
                tracker_fix_timeout_s=120,
                tracker_settle_s=10,
                fox_period_s=30,
                fox_tone_ms=10000,
                fox_aprs_every=10,
                reserved=b"\xff" * 27,
            )
            for item in a.values:
                apply_assignment(s, item)
            err = check(seal(s))
            if err != "OK":
                raise SettingsError(WHY.get(err, err))
            print(describe(s))
            return 0

        board = Board(a.elf)
        board.check_build()
        if a.command == "show":
            status(board)
        elif a.command == "set":
            if not a.values:
                raise SettingsError("set what? e.g. set callsign=NZ0P ssid=12")
            if board.command("edit") != "OK":
                raise SettingsError("couldn't read the live settings")
            s = unpack(board.read_sym("settings_edit"))
            for item in a.values:
                apply_assignment(s, item)
            err = check(seal(s))
            if err != "OK":
                raise SettingsError(f"not applied: {WHY.get(err, err)}")
            board.write_words(board.sym["settings_edit"][0], bytes(pack(s)))
            result = board.command("apply")
            if result != "OK":
                why = ERRORS[board.u8("settings_error")]
                raise SettingsError(f"the firmware refused it ({why}): {WHY.get(why, why)}")
            print("applied")
            if a.save:
                if board.command("save") != "OK":
                    raise SettingsError("applied, but saving to the EEPROM failed")
                print("saved to the EEPROM")
            else:
                print("not saved: lost at the next reset unless you run 'dfmcfg.py save'")
            status(board)
        else:
            result = board.command(a.command)
            if result != "OK":
                why = ERRORS[board.u8("settings_error")]
                raise SettingsError(f"{a.command} failed: {result} ({why})")
            print(f"{a.command}: OK")
            status(board)
    except SettingsError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
