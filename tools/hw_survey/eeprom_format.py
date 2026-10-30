"""eeprom_format.py RAW OUTDIR [NOTE]

Turn a raw read of the nfc_tool build's nfc_backup struct (space-separated hex
bytes, as tools/hw_survey/common.sh readmem prints them) into a backup of the
M24LR04E-R EEPROM:

  OUTDIR/eeprom-M24LR04E-<UID>-<YYYYMMDD>.txt   annotated hex dump, both areas
  OUTDIR/eeprom-M24LR04E-<UID>-<YYYYMMDD>.bin   user memory, 512 bytes raw

Prints the .txt path. Used by eeprom_backup.sh.
"""
import datetime
import os
import struct
import sys

MAGIC = 0x4B434142  # "BACK"
LAYOUT = [("hdr", 12), ("user", 512), ("sss", 16), ("lock", 16), ("info", 48)]


def parse(raw):
    b = bytes(int(x, 16) for x in raw.split())
    need = sum(n for _, n in LAYOUT)
    if len(b) < need:
        sys.exit(f"short read: {len(b)} of {need} bytes")
    out, i = {}, 0
    for name, n in LAYOUT:
        out[name] = b[i:i + n]
        i += n
    magic, _count = struct.unpack_from("<II", out["hdr"])
    if magic != MAGIC:
        sys.exit(f"no backup in RAM (magic {magic:08x}); is the nfc_tool build running?")
    if out["hdr"][8] != 1:
        sys.exit("the firmware reported an I2C read failure; not writing a backup")
    return out


def hexdump(data, base, ascii_col=True):
    lines = []
    for o in range(0, len(data), 16):
        row = data[o:o + 16]
        h = " ".join(f"{x:02x}" for x in row)
        a = "".join(chr(x) if 32 <= x < 127 else "." for x in row)
        lines.append(f"{base + o:04x}: {h}" + (f"  |{a}|" if ascii_col else ""))
    return lines


def sss_text(v):
    if not v & 1:
        return "unlocked: RF read/write"
    rw = (v >> 1) & 3
    pw = (v >> 3) & 3
    with_pw = ["read/write", "read/write", "read/write", "read only"][rw]
    without = ["read only", "read/write", "no access", "no access"][rw]
    return f"locked, password {pw or 'none'}: {with_pw} with it, {without} without"


def main():
    with open(sys.argv[1]) as f:
        raw = f.read()
    outdir = sys.argv[2]
    note = sys.argv[3] if len(sys.argv) > 3 else ""
    d = parse(raw)
    info, user = d["info"], d["user"]
    uid = info[0x14:0x1C][::-1].hex().upper()
    serial = struct.unpack_from("<I", user, 0x88)[0]
    case_no = struct.unpack_from("<I", user, 0x38)[0]
    today = datetime.date.today()
    stem = os.path.join(outdir, f"eeprom-M24LR04E-{uid}-{today:%Y%m%d}")

    t = [
        "# DFM-17 NFC EEPROM (M24LR04E-R, U12) full backup over I2C (PB6 SCL / PB7 SDA)",
        f"# UID {uid}, read {today} by tools/hw_survey/eeprom_backup.sh (nfc_tool build)",
    ]
    if note:
        t.append(f"# {note}")
    t += [
        "#",
        f"# board serial (0x0088, uint32 LE): {serial}"
        + ("  (erased)" if serial == 0xFFFFFFFF else ""),
        f"# value at 0x0038 (uint32 LE, possibly the sonde serial): {case_no}"
        + ("  (erased)" if case_no == 0xFFFFFFFF else ""),
        "#",
        "# user memory: I2C address 0x53, 0x0000-0x01FF, 4 sectors x 128 bytes.",
        "# Graw factory data in 0x0000-0x015F (config bytes, IEEE-754 double",
        "# calibration coefficients from 0x00B8); keep this file.",
        "",
    ]
    t += hexdump(user, 0)
    t += [
        "",
        "# system area: I2C address 0x57",
        "# 0x0000: sector security status (SSS), sectors 0-3",
    ]
    t += hexdump(d["sss"], 0x0000, False)
    for i in range(4):
        t.append(f"#   SSS[{i}] = {d['sss'][i]:02x}: {sss_text(d['sss'][i])}")
    t += ["# 0x0800: I2C_Write_Lock (bits 3:0 = sectors 3-0, 1 = I2C write-protected)"]
    t += hexdump(d["lock"], 0x0800, False)
    t += [
        "# 0x0900: I2C password (reads FF, but reads back its value while an I2C",
        "#         password session is open), 0x0904-0x090F RF passwords 1-3 (read FF),",
        "#         0x0910 configuration byte, 0x0912 AFI, 0x0913 DSFID, 0x0914 UID",
        "#         (LSB first), 0x091C IC ref, 0x091D mem size, 0x0920 control register",
    ]
    t += hexdump(info, 0x0900, False)
    cfg = info[0x10]
    t += [
        f"#   configuration {cfg:02x}: RF WIP/BUSY pin in "
        + ("RF WIP mode (low while an RF write is programmed)" if cfg & 8
           else "RF BUSY mode (low during any RF command)"),
        (
            f"#   AFI {info[0x12]:02x}, DSFID {info[0x13]:02x}, IC ref {info[0x1C]:02x}, "
            f"mem size {info[0x1D]:02x} {info[0x1E]:02x} "
            f"({info[0x1D] + 1} blocks of {info[0x1E] + 1} bytes), control {info[0x20]:02x}"
        ),
        "",
    ]
    with open(stem + ".txt", "w") as f:
        f.write("\n".join(t))
    with open(stem + ".bin", "wb") as f:
        f.write(user)
    print(stem + ".txt")


if __name__ == "__main__":
    main()
