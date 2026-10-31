"""fakeboard.py: a simulated DFM-17 behind the dfmcfg link interface (read,
write, close), for the tests and for `dfmgui.py --simulate`.

Flash holds a version string and the SWD descriptor; RAM holds the settings
and the mailbox, and a write to settings_cmd runs the command at once as
settings_store.c's run() does (the EEPROM is a copy of the saved block).
"""

import struct

import dfmcfg as d

RAM_AT = {
    "cfg": 0x20000100,
    "settings_edit": 0x20000140,
    "settings_cmd": 0x20000180,
    "settings_result": 0x20000181,
    "settings_source": 0x20000182,
    "settings_error": 0x20000183,
    "settings_done": 0x20000184,
    "settings_repairs": 0x20000188,
    "si4060_tx_refused": 0x2000018C,
}
FW_AT = 0x08005000


class FakeLink:
    name = "simulated"

    def __init__(self, fw_version=None, swd_at=0x08009A40, settings=None, info=None):
        self.flash = bytearray(b"\xff" * 0x10000)
        self.ram = bytearray(0x2000)
        self.closed = False
        fw = fw_version or f"DFMFW {d.tool_version()} tracker"
        self._put(FW_AT, fw.encode() + b"\0")
        fields = {
            "magic": d.SWD_INFO_MAGIC,
            "version": d.SWD_INFO_VERSION,
            "size": d.SWD_INFO_SIZE,
            "settings_version": d.VERSION,
            "settings_size": d.SIZE,
            "fw_version": FW_AT,
            **RAM_AT,
        }
        fields.update(info or {})
        if swd_at is not None:
            self._put(swd_at, struct.pack(d.SWD_INFO_FMT, *(fields[n] for n, _ in d.SWD_INFO)))
        s = settings or d.defaults()
        self.eeprom = bytes(d.pack(d.seal(dict(s))))
        self._put(RAM_AT["cfg"], self.eeprom)
        self._put(RAM_AT["settings_source"], bytes([1]))  # loaded from the EEPROM

    # ---- memory -------------------------------------------------------------

    def _region(self, addr):
        if 0x08000000 <= addr < 0x08010000:
            return self.flash, addr - 0x08000000
        if 0x20000000 <= addr < 0x20002000:
            return self.ram, addr - 0x20000000
        return None, 0

    def _put(self, addr, raw):
        mem, off = self._region(addr)
        mem[off : off + len(raw)] = raw

    def read(self, addr, n):
        if addr == d.FLASH_SIZE_REG:
            return struct.pack("<H", 64)[:n]
        if addr == d.IDCODE_REG:
            return struct.pack("<I", 0x10016420)[:n]
        if addr == d.UID_REG:
            return bytes(range(0x30, 0x3C))[:n]
        mem, off = self._region(addr)
        if mem is None:
            raise d.SettingsError(f"SWD read failed at 0x{addr:08X}")
        return bytes(mem[off : off + n])

    def write(self, addr, raw):
        self._put(addr, raw)
        if addr == RAM_AT["settings_cmd"]:
            self._command(raw[0])

    def close(self):
        self.closed = True

    # ---- the firmware's mailbox ----------------------------------------------

    def _get(self, name):
        return self.read(RAM_AT[name], d.TARGET_SIZES[name])

    def _apply(self, s, source):
        err = d.check(s)
        self._put(RAM_AT["settings_error"], bytes([d.ERRORS.index(err)]))
        if err != "OK":
            return 2
        self._put(RAM_AT["cfg"], bytes(d.pack(s)))
        self._put(RAM_AT["settings_source"], bytes([source]))
        return 1

    def _command(self, cmd):
        if cmd == 1:  # EDIT
            result = 1
            self._put(RAM_AT["settings_edit"], self._get("cfg"))
        elif cmd == 2:  # APPLY: the firmware seals what the editor wrote
            result = self._apply(d.seal(d.unpack(self._get("settings_edit"))), 2)
        elif cmd == 3:  # SAVE
            self.eeprom = self._get("cfg")
            self._put(RAM_AT["settings_source"], bytes([1]))
            result = 1
        elif cmd == 4:  # RELOAD
            result = self._apply(d.unpack(self.eeprom), 1)
        elif cmd == 5:  # DEFAULTS
            result = self._apply(d.defaults(), 0)
        else:
            result = 4
        done = struct.unpack("<I", self._get("settings_done"))[0] + 1
        self._put(RAM_AT["settings_result"], bytes([result]))
        self._put(RAM_AT["settings_done"], struct.pack("<I", done))
        self._put(RAM_AT["settings_cmd"], b"\0")
