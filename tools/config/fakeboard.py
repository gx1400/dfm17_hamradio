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
    "aprs_send_now": 0x20000190,
    "aprs_packets_sent": 0x20000194,
    "cal": 0x200001A0,
    "cal_edit": 0x200001C0,
    "cal_source": 0x200001E0,
    "cal_error": 0x200001E1,
    "analog_last": 0x200001E4,
}
# a board on USB: 4.98 V in, 57 mA, 4.99 V VBUS, with the default calibration
ANALOG_LAST = {
    "vin_raw": 2956,
    "iin_raw": 66,
    "vref_raw": 1486,
    "vdda_mv": 3306,
    "vin_mv": 4980,
    "iin_ma": 57,
    "temp_c": 30,
    "usb_raw": 3060,
    "usb_mv": 4990,
    "on_usb": 1,
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
        # the calibration block: erased, so the defaults (MAGIC)
        self.cal_eeprom = b"\xff" * d.CAL_SIZE
        self._put(RAM_AT["cal"], bytes(d.cal_pack(d.cal_defaults())))
        self._put(RAM_AT["cal_error"], bytes([d.CAL_ERRORS.index("MAGIC")]))
        a = struct.pack(d.ANALOG_FMT, *(ANALOG_LAST[n] for n, _ in d.ANALOG))
        self._put(RAM_AT["analog_last"], a.ljust(d.ANALOG_SIZE, b"\0"))

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
        elif addr == RAM_AT["aprs_send_now"]:
            self._send_now(raw[0])

    def close(self):
        self.closed = True

    # ---- the send-now trigger (#51) --------------------------------------------

    def _send_now(self, n):
        """the main loop sends n packets; the transmit gate refuses them all
        without a callsign set up, as si4060_start_tx() does"""
        s = d.unpack(self._get("cfg"))
        name = (
            "aprs_packets_sent"
            if d.check(s) == "OK" and d.callsign_str(s) != "N0CALL"
            else "si4060_tx_refused"
        )
        count = struct.unpack("<I", self._get(name))[0] + n
        self._put(RAM_AT[name], struct.pack("<I", count))
        self._put(RAM_AT["aprs_send_now"], b"\0")

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

    def _cal_use(self, c, source):
        self._put(RAM_AT["cal"], bytes(d.cal_pack(c)))
        self._put(RAM_AT["cal_source"], bytes([source]))

    def _cal_command(self, cmd):
        """CMD_CAL_*, as settings_store.c's run_cal()"""
        if cmd == 6:  # CAL_EDIT
            self._put(RAM_AT["cal_edit"], self._get("cal"))
            return 1
        if cmd == 7:  # CAL_APPLY: refused whole if anything is wrong
            c = d.cal_seal(d.cal_unpack(self._get("cal_edit")))
            err = d.cal_check(c)
            self._put(RAM_AT["cal_error"], bytes([d.CAL_ERRORS.index(err)]))
            if err != "OK":
                return 2
            self._cal_use(c, 2)
            return 1
        if cmd == 8:  # CAL_SAVE
            self.cal_eeprom = bytes(d.cal_pack(d.cal_seal(d.cal_unpack(self._get("cal")))))
            self._put(RAM_AT["cal_source"], bytes([1]))
            return 1
        if cmd == 9:  # CAL_RELOAD: out-of-range channels get their defaults
            c = d.cal_unpack(self.cal_eeprom)
            err = d.cal_check(c)
            self._put(RAM_AT["cal_error"], bytes([d.CAL_ERRORS.index(err)]))
            if err not in ("OK", "RANGE"):
                return 2
            for ch in d.cal_bad_channels(c):
                c[f"{ch}_a"], c[f"{ch}_b"] = d.CAL_DEFAULT[ch]
                c["measured"] &= ~(1 << [n for n, _, _ in d.CAL_CHANNELS].index(ch))
            self._cal_use(d.cal_seal(c), 1)
            return 1
        # CAL_DEFAULTS
        self._cal_use(d.cal_defaults(), 0)
        self._put(RAM_AT["cal_error"], b"\0")
        return 1

    def _command(self, cmd):
        if 6 <= cmd <= 10:
            result = self._cal_command(cmd)
        elif cmd == 1:  # EDIT
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
