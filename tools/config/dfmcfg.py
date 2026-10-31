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
import time

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
    ("gps_power", 35, "B"),
    ("reserved", 36, "26s"),
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
    "GPS_POWER",
]
SOURCES = ["built-in defaults", "EEPROM", "SWD (not saved)"]
RESULTS = ["none", "OK", "INVALID", "EEPROM", "BAD_CMD"]
CMD = {"edit": 1, "apply": 2, "save": 3, "reload": 4, "defaults": 5}
FLAG_START_PAUSED = 0x01
MODES = {"tracker": 0, "fox": 1}
PATHS = {"WIDE1-1": 0x11, "WIDE2-1": 0x21, "WIDE2-2": 0x22}
# 0xFF (erased, blocks saved before the field) is the default: duty cycle
GPS_POWER = {"duty_cycle": 0, "always_on": 1, "default": 0xFF}


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
    if s["gps_power"] not in GPS_POWER.values():
        return "GPS_POWER"
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
    "GPS_POWER": "gps_power: default, duty_cycle or always_on",
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
    elif name == "gps_power":
        if v.lower() not in GPS_POWER:
            raise SettingsError(f"gps_power: '{v}' isn't {', '.join(GPS_POWER)}")
        s[name] = GPS_POWER[v.lower()]
    else:
        raise SettingsError(f"'{name}' isn't a setting (see 'dfmcfg.py fields')")


def describe(s):
    path = next((k for k, v in PATHS.items() if v == s["path"]), f"0x{s['path']:02x}?")
    mode = next((k for k, v in MODES.items() if v == s["mode"]), f"{s['mode']}?")
    sym = (s["symbol_table"] + s["symbol_code"]).decode("ascii", "replace")
    gps = next((k for k, v in GPS_POWER.items() if v == s["gps_power"]), f"{s['gps_power']}?")
    if gps == "default":
        gps = "default (duty_cycle)"
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
            f"  gps_power     {gps}",
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
  fox_aprs_every=10      APRS after every Nth fox cycle, 0 = never
  gps_power=default      default (duty_cycle), duty_cycle: GPS in backup between
                         tracker fixes; always_on: never (1PPS kept, ~20-25 mA more)"""


# ---- fields, as the GUI shows them ------------------------------------------------
# (name, group, choices): name is what apply_assignment() takes, choices a
# list for a drop-down or None for free text. The help comes from FIELDS_HELP.
FIELDS = [
    ("callsign", "Station", None),
    ("ssid", "Station", None),
    ("symbol", "Station", None),
    ("path", "Station", list(PATHS)),
    ("mode", "Station", list(MODES)),
    ("start_paused", "Station", ["no", "yes"]),
    ("freq", "Radio", None),
    ("power", "Radio", None),
    ("deviation", "Radio", None),
    ("cw_wpm", "Radio", None),
    ("tracker_period_s", "Tracker", None),
    ("tracker_fix_timeout_s", "Tracker", None),
    ("tracker_settle_s", "Tracker", None),
    ("gps_power", "Tracker", list(GPS_POWER)),
    ("fox_period_s", "Fox", None),
    ("fox_tone_ms", "Fox", None),
    ("fox_aprs_every", "Fox", None),
]


def field_help():
    """name -> help text, from FIELDS_HELP (continuation lines included)"""
    out, name = {}, None
    for line in FIELDS_HELP.splitlines()[1:]:
        m = re.match(r"  (\w+)=\S*\s+(.*)", line)
        if m:
            name = m.group(1)
            out[name] = m.group(2)
        elif name:
            out[name] += " " + line.strip()
    return out


def field_value(s, name):
    """a setting as text that apply_assignment() takes back unchanged"""
    if name == "callsign":
        return callsign_str(s)
    if name == "symbol":
        return (s["symbol_table"] + s["symbol_code"]).decode("ascii", "replace")
    if name == "path":
        return next((k for k, v in PATHS.items() if v == s["path"]), f"0x{s['path']:02x}")
    if name == "mode":
        return next((k for k, v in MODES.items() if v == s["mode"]), str(s["mode"]))
    if name == "start_paused":
        return "yes" if s["flags"] & FLAG_START_PAUSED else "no"
    if name == "freq":
        return f"{s['freq_hz'] / 1e6:.6f}".rstrip("0").rstrip(".") + "MHz"
    if name == "power":
        return str(s["pa_level"])
    if name == "deviation":
        return str(s["dev_hz"])
    if name == "gps_power":
        return next((k for k, v in GPS_POWER.items() if v == s["gps_power"]), str(s["gps_power"]))
    return str(s[name])


# ---- versions ------------------------------------------------------------------
# The tools are released with the firmware and carry its version: a release
# build writes _version.py (tools/config/build_app.py); in a checkout it's
# `git describe`, the same command the Makefile stamps the firmware with.


def tool_version():
    try:
        from _version import FW_VERSION  # written by the release build

        return FW_VERSION
    except ImportError:
        pass
    try:
        r = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            check=False,
            cwd=os.path.dirname(os.path.abspath(__file__)),
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    return r.stdout.strip() if r.returncode == 0 and r.stdout.strip() else "unknown"


def parse_fw_version(text):
    """'DFMFW v0.4.0-3-gabc1234 tracker' -> ('v0.4.0-3-gabc1234', 'tracker')"""
    f = text.split()
    if len(f) != 3 or f[0] != "DFMFW":
        return None, None
    return f[1], f[2]


def version_match(board_fw, tool=None):
    """(True, '') when the board runs the firmware these tools belong to, else
    (False, why). 'unknown' (a CubeIDE build, or no git) never matches."""
    tool = tool or tool_version()
    version, _ = parse_fw_version(board_fw)
    if version is None:
        return False, f"the board's version string '{board_fw}' isn't a DFMFW one"
    if version == "unknown" or tool == "unknown":
        return False, f"can't compare versions (board {version}, tools {tool})"
    if version != tool:
        return False, f"the board runs firmware {version}, these tools are {tool}"
    return True, ""


# ---- SWD -----------------------------------------------------------------------

FLASH = 0x08000000
RAM = (0x20000000, 0x20002000)  # STM32F100R8: 8 KB
FLASH_SIZE_REG = 0x1FFFF7E0  # flash size in KB
UID_REG = 0x1FFFF7E8  # 96-bit unique ID
IDCODE_REG = 0xE0042000  # DBGMCU_IDCODE: device and revision

# swd_info_t (dfm17/Core/Inc/settings.h): name, struct format. Pointer fields
# are named after the symbol they point at (the C tx_refused field points at
# si4060_tx_refused); --check-elf compares them with the ELF's symbol table.
SWD_INFO_MAGIC = b"DFM-SWD\0"
# the newest layout these tools know; version 1 (no send-now) still works
SWD_INFO_VERSION = 2
SWD_INFO = [
    ("magic", "8s"),
    ("version", "B"),
    ("size", "B"),
    ("settings_version", "B"),
    ("settings_size", "B"),
    ("fw_version", "I"),
    ("cfg", "I"),
    ("settings_edit", "I"),
    ("settings_cmd", "I"),
    ("settings_result", "I"),
    ("settings_source", "I"),
    ("settings_error", "I"),
    ("settings_done", "I"),
    ("settings_repairs", "I"),
    ("si4060_tx_refused", "I"),
    # version 2: the send-now trigger (#51)
    ("aprs_send_now", "I"),
    ("aprs_packets_sent", "I"),
]
SWD_INFO_FMT = "<" + "".join(f for _, f in SWD_INFO)
SWD_INFO_SIZE = struct.calcsize(SWD_INFO_FMT)
# version 1 ends after si4060_tx_refused; later fields are optional
SWD_INFO_V1_FIELDS = 15
SWD_INFO_V1_SIZE = struct.calcsize("<" + "".join(f for _, f in SWD_INFO[:SWD_INFO_V1_FIELDS]))
# bytes at each pointer (fw_version is a string, read up to its NUL)
TARGET_SIZES = {"cfg": SIZE, "settings_edit": SIZE, "settings_done": 4, "settings_repairs": 4}
TARGET_SIZES["si4060_tx_refused"] = 4
for _n in ("settings_cmd", "settings_result", "settings_source", "settings_error"):
    TARGET_SIZES[_n] = 1
TARGET_SIZES["aprs_send_now"] = 1
TARGET_SIZES["aprs_packets_sent"] = 4


def parse_swd_info(raw):
    """the descriptor's fields, checked: SettingsError if these tools can't use it.
    Fields past the board's own size (an older firmware) are left out."""
    info = dict(zip((n for n, _ in SWD_INFO), struct.unpack_from(SWD_INFO_FMT, raw)))
    if info["magic"] != SWD_INFO_MAGIC:
        raise SettingsError("not an SWD descriptor")
    # fields are only appended: a newer layout still starts with these
    if info["version"] < 1 or info["size"] < SWD_INFO_V1_SIZE:
        raise SettingsError(f"SWD descriptor version {info['version']} is older than these tools")
    off = 0
    for name, fmt in SWD_INFO:
        off += struct.calcsize(fmt)
        if off > info["size"]:
            del info[name]
    if info["settings_version"] != VERSION or info["settings_size"] != SIZE:
        raise SettingsError(
            f"the board's settings layout is version {info['settings_version']} "
            f"({info['settings_size']} B), these tools know version {VERSION} ({SIZE} B): "
            "use the tools released with the board's firmware"
        )
    for name, size in TARGET_SIZES.items():
        if name in info and not RAM[0] <= info[name] <= RAM[1] - size:
            raise SettingsError(f"SWD descriptor: {name} at 0x{info[name]:08X} isn't in RAM")
    if not FLASH <= info["fw_version"] < FLASH + 0x20000:
        raise SettingsError("SWD descriptor: fw_version isn't in flash")
    return info


def find_swd_info(link, flash_kb):
    """scan flash for the descriptor (4-byte aligned: it holds pointers)"""
    chunk = 0x2000
    tail = b""
    for base in range(FLASH, FLASH + flash_kb * 1024, chunk):
        data = tail + link.read(base, chunk)
        start = base - len(tail)
        at = data.find(SWD_INFO_MAGIC)
        while at >= 0:
            if (start + at) % 4 == 0:
                raw = data[at : at + SWD_INFO_SIZE]
                if len(raw) < SWD_INFO_SIZE:
                    raw += link.read(start + at + len(raw), SWD_INFO_SIZE - len(raw))
                return parse_swd_info(raw)
            at = data.find(SWD_INFO_MAGIC, at + 1)
        tail = data[-(SWD_INFO_SIZE - 1) :]
    raise SettingsError(
        "no SWD descriptor in the board's flash: firmware from before the settings GUI? "
        "Flash a current build, or pass --elf with the build that's on it"
    )


def find_tool(names, pattern):
    for n in names:
        if shutil.which(n):
            return shutil.which(n)
    hits = sorted(glob.glob(pattern))
    if hits:
        return hits[-1]
    raise SettingsError(f"can't find {names[0]} (STM32CubeIDE under /opt/st, or on PATH)")


class PyOcdLink:
    """an ST-Link (or CMSIS-DAP probe) through pyOCD: one connection, attached
    without halting or resetting the board"""

    name = "pyocd"

    def __init__(self, unique_id=None):
        try:
            from pyocd.core.helpers import ConnectHelper
        except ImportError:
            raise SettingsError(
                "pyOCD isn't installed: pip install -r tools/requirements-config.txt"
            ) from None
        try:
            self.session = ConnectHelper.session_with_chosen_probe(
                blocking=False,
                return_first=True,
                unique_id=unique_id,
                options={
                    "target_override": "cortex_m",
                    "connect_mode": "attach",
                    "frequency": 1_000_000,
                },
            )
        except Exception as e:  # pyOCD raises many types; all mean "no connection"
            raise SettingsError(f"couldn't connect: {e}") from e
        if self.session is None:
            raise SettingsError("no ST-Link found: is it plugged in (and its USB driver set up)?")
        self.target = self.session.target

    @staticmethod
    def probes():
        """(unique_id, description) of each connected probe"""
        from pyocd.core.helpers import ConnectHelper

        found = ConnectHelper.get_all_connected_probes(blocking=False, print_wait_message=False)
        return [(p.unique_id, p.description) for p in found]

    def read(self, addr, n):
        try:
            return bytes(self.target.read_memory_block8(addr, n))
        except Exception as e:
            raise SettingsError(f"SWD read failed: {e}") from e

    def write(self, addr, raw):
        try:
            self.target.write_memory_block8(addr, list(raw))
        except Exception as e:
            raise SettingsError(f"SWD write failed: {e}") from e

    def close(self):
        self.session.close()


class CubeProgrammerLink:
    """STM32_Programmer_CLI in hot-plug mode, one process per access (slow)"""

    name = "cube"

    def __init__(self):
        self.prog = find_tool(
            ["STM32_Programmer_CLI"],
            "/opt/st/stm32cubeide_*/plugins/*cubeprogrammer.linux64*/tools/bin/STM32_Programmer_CLI",
        )

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

    def write(self, addr, raw):
        if addr % 4 == 0 and len(raw) % 4 == 0:
            words = struct.unpack(f"<{len(raw) // 4}I", raw)
            self._run("-w32", hex(addr), *(f"0x{w:08X}" for w in words))
        else:
            for i, b in enumerate(raw):
                self._run("-w8", hex(addr + i), f"0x{b:02X}")

    def close(self):
        pass


LINKS = {"pyocd": PyOcdLink, "cube": CubeProgrammerLink}


def elf_symbols(elf):
    nm = find_tool(
        ["arm-none-eabi-nm"],
        "/opt/st/stm32cubeide_*/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-nm",
    )
    out = subprocess.run([nm, "-S", elf], capture_output=True, text=True, check=True).stdout
    sym = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) == 4:
            sym[f[3]] = (int(f[0], 16), int(f[1], 16))
    return sym


def elf_flash(elf):
    """the ELF's flash image, as written to the board"""
    objcopy = find_tool(
        ["arm-none-eabi-objcopy"],
        "/opt/st/stm32cubeide_*/plugins/*gnu-tools-for-stm32*/tools/bin/arm-none-eabi-objcopy",
    )
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "flash.bin")
        subprocess.run([objcopy, "-O", "binary", elf, out], check=True)
        with open(out, "rb") as f:
            return f.read()


def check_elf(elf):
    """CI: the descriptor in a built ELF parses, and every pointer in it is the
    address of the symbol it's named after, with the size the tools expect"""
    sym = elf_symbols(elf)
    if "swd_info" not in sym:
        raise SettingsError(f"{elf}: no swd_info")
    addr, size = sym["swd_info"]
    if size != SWD_INFO_SIZE:
        raise SettingsError(f"{elf}: swd_info is {size} B, SWD_INFO says {SWD_INFO_SIZE}")
    image = elf_flash(elf)
    info = parse_swd_info(image[addr - FLASH : addr - FLASH + size])
    for name, _ in SWD_INFO[5:]:  # the pointers
        if name not in sym or sym[name][0] != info[name]:
            raise SettingsError(f"{elf}: swd_info.{name} doesn't point at {name}")
        want = TARGET_SIZES.get(name)
        if want is not None and sym[name][1] != want:
            raise SettingsError(f"{elf}: {name} is {sym[name][1]} B, the tools expect {want}")
    return info


class Board:
    """a DFM-17 over SWD: addresses from the descriptor in flash (no ELF
    needed) or, for older builds, from the ELF's symbol table"""

    def __init__(self, link, elf=None):
        self.link = link
        flash_kb = struct.unpack("<H", link.read(FLASH_SIZE_REG, 2))[0]
        self.flash_kb = flash_kb if 16 <= flash_kb <= 1024 else 64
        if elf:
            self.sym = elf_symbols(elf)
            for need in (
                "cfg",
                "settings_edit",
                "settings_cmd",
                "settings_result",
                "settings_done",
            ):
                if need not in self.sym:
                    raise SettingsError(f"{elf} has no '{need}': a build without EEPROM settings?")
            self.check_elf_matches(elf)
            fw_addr = self.sym["fw_version"][0] if "fw_version" in self.sym else None
        else:
            info = find_swd_info(link, self.flash_kb)
            self.sym = {n: (info[n], s) for n, s in TARGET_SIZES.items() if n in info}
            fw_addr = info["fw_version"]
        self.fw_version = self.read_string(fw_addr) if fw_addr else "unknown"

    def check_elf_matches(self, elf):
        """with --elf: the board must run this ELF (same version string in flash)"""
        if "fw_version" not in self.sym:
            return
        addr, size = self.sym["fw_version"]
        if elf_flash(elf)[addr - FLASH : addr - FLASH + size] != self.link.read(addr, size):
            raise SettingsError(
                f"the board isn't running {elf} (its version string differs). "
                "Pass --elf for the build that's on it, or flash this one."
            )

    def read_string(self, addr, limit=64):
        raw = self.link.read(addr, limit)
        return raw.split(b"\0", 1)[0].decode("ascii", "replace")

    def read_sym(self, name):
        addr, size = self.sym[name]
        return self.link.read(addr, size)

    def u32(self, name):
        if name not in self.sym:
            return None
        return struct.unpack("<I", self.read_sym(name)[:4])[0]

    def u8(self, name):
        return self.read_sym(name)[0] if name in self.sym else None

    def mcu(self):
        """MCU device and revision ID, and the unique ID as hex"""
        idcode = struct.unpack("<I", self.link.read(IDCODE_REG, 4))[0]
        return idcode & 0xFFF, idcode >> 16, self.link.read(UID_REG, 12).hex().upper()

    def settings(self):
        return unpack(self.read_sym("cfg"))

    def command(self, name):
        done = self.u32("settings_done")
        self.link.write(self.sym["settings_cmd"][0], bytes([CMD[name]]))
        # the main loop runs it, which can be busy for a fox tone or a packet
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self.u32("settings_done") != done:
                return RESULTS[self.u8("settings_result")]
            time.sleep(0.05)
        raise SettingsError("the firmware didn't run the command (still booting? try again)")

    def apply(self, s, save=False):
        """check s, send it to the board and apply it; then save if asked"""
        err = check(seal(s))
        if err != "OK":
            raise SettingsError(f"not applied: {WHY.get(err, err)}")
        if self.command("edit") != "OK":
            raise SettingsError("couldn't read the live settings")
        self.link.write(self.sym["settings_edit"][0], bytes(pack(s)))
        if self.command("apply") != "OK":
            why = ERRORS[self.u8("settings_error")]
            raise SettingsError(f"the firmware refused it ({why}): {WHY.get(why, why)}")
        if save and self.command("save") != "OK":
            raise SettingsError("applied, but saving to the EEPROM failed")

    def send(self, n=1):
        """send-now trigger (#51): n APRS packets, no test tone. Waits until n
        have been keyed or refused by the transmit gate; returns (sent, refused).
        A scheduled packet in the meantime counts as sent too."""
        if "aprs_send_now" not in self.sym or "aprs_packets_sent" not in self.sym:
            raise SettingsError("this firmware has no send-now trigger: flash a newer build")
        if not 1 <= n <= 255:
            raise SettingsError("send 1 to 255 packets")
        sent0, refused0 = self.u32("aprs_packets_sent"), self.u32("si4060_tx_refused")
        self.link.write(self.sym["aprs_send_now"][0], bytes([n]))
        # each packet takes ~1.5 s with the gap; a fox tone may be running first
        deadline = time.monotonic() + 70 + 3 * n
        while time.monotonic() < deadline:
            sent = self.u32("aprs_packets_sent") - sent0
            refused = self.u32("si4060_tx_refused") - refused0
            if sent + refused >= n:
                return sent, refused
            time.sleep(0.1)
        raise SettingsError(f"only {sent} of {n} sent ({refused} refused) before the timeout")

    def run(self, name):
        """save, reload or defaults"""
        result = self.command(name)
        if result != "OK":
            why = ERRORS[self.u8("settings_error")]
            raise SettingsError(f"{name} failed: {result} ({why})")

    def status(self):
        s = self.settings()
        return {
            "settings": s,
            "source": SOURCES[self.u8("settings_source")],
            "error": ERRORS[self.u8("settings_error")],
            "tx_allowed": check(s) == "OK" and callsign_str(s) != "N0CALL",
            "tx_refused": self.u32("si4060_tx_refused"),
            "repairs": self.u32("settings_repairs"),
        }


def defaults():
    """settings_defaults() in settings.c: N0CALL, so no transmitting"""
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
        gps_power=0xFF,
        reserved=b"\xff" * 26,
    )
    return seal(s)


def print_status(board):
    st = board.status()
    print(f"firmware {board.fw_version}")
    print(f"settings from {st['source']}; last load/apply check: {st['error']}")
    print(describe(st["settings"]))
    print(f"  transmit      {'allowed' if st['tx_allowed'] else 'DISABLED (no callsign set up)'}")
    if st["tx_refused"] or st["repairs"]:
        print(f"  key-ups refused {st['tx_refused']}, live settings restored {st['repairs']} times")


def connect(a):
    """the board, with the version check: SettingsError if writes aren't allowed"""
    link = LINKS[a.link]()
    board = Board(link, a.elf)
    if not a.elf:
        ok, why = version_match(board.fw_version)
        if not ok:
            msg = f"{why}. Use the tools released with the board's firmware, or flash this build"
            if a.command not in ("show", "send") and not a.allow_mismatch:
                link.close()
                raise SettingsError(msg + " (--allow-mismatch to go ahead anyway)")
            print(f"warning: {msg}", file=sys.stderr)
    return board


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--elf",
        help="the build on the board, for firmware without the SWD descriptor (older builds)",
    )
    ap.add_argument("--link", choices=sorted(LINKS), default="pyocd", help="SWD access (pyocd)")
    ap.add_argument(
        "--allow-mismatch",
        action="store_true",
        help="change settings although the board's firmware version differs from these tools",
    )
    ap.add_argument("--save", action="store_true", help="with set: also save to the EEPROM")
    ap.add_argument("--layout", action="store_true", help="print the field offsets and exit")
    ap.add_argument(
        "--check-elf",
        metavar="ELF",
        help="check the SWD descriptor in a built ELF against its symbols, and exit",
    )
    ap.add_argument("--version", action="store_true", help="print the tools' version and exit")
    ap.add_argument(
        "command",
        nargs="?",
        choices=["show", "set", "save", "reload", "defaults", "check", "fields", "send"],
    )
    ap.add_argument("values", nargs="*", help="name=value, for set and check; N for send")
    a = ap.parse_args()

    if a.layout:
        for name, off, fmt in LAYOUT:
            print(name, off, struct.calcsize(fmt))
        return 0
    if a.version:
        print(tool_version())
        return 0
    try:
        if a.check_elf:
            check_elf(a.check_elf)
            print(f"{a.check_elf}: SWD descriptor OK")
            return 0
        if a.command in (None, "fields"):
            print(FIELDS_HELP)
            return 0
        if a.command == "check":
            # offline: the defaults with these changes
            s = defaults()
            for item in a.values:
                apply_assignment(s, item)
            err = check(seal(s))
            if err != "OK":
                raise SettingsError(WHY.get(err, err))
            print(describe(s))
            return 0

        if a.command == "set" and not a.values:
            raise SettingsError("set what? e.g. set callsign=NZ0P ssid=12")
        if a.command == "send":
            if len(a.values) > 1 or (a.values and not a.values[0].isdigit()):
                raise SettingsError("send [N]: the number of packets, e.g. send 5")
            count = int(a.values[0]) if a.values else 1
        board = connect(a)
        try:
            if a.command == "send":
                sent, refused = board.send(count)
                print(f"sent {sent} APRS packet(s)" + (f", {refused} refused" if refused else ""))
                if refused:
                    print("refused by the transmit gate: no callsign set up, or invalid settings")
                return 0
            if a.command == "set":
                s = board.settings()
                for item in a.values:
                    apply_assignment(s, item)
                board.apply(s, save=a.save)
                print("applied")
                if a.save:
                    print("saved to the EEPROM")
                else:
                    print("not saved: lost at the next reset unless you run 'dfmcfg.py save'")
            elif a.command != "show":
                board.run(a.command)
                print(f"{a.command}: OK")
            print_status(board)
        finally:
            board.link.close()
    except SettingsError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
