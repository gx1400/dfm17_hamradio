"""Unit tests for dfmcfg.py: the checks, the field model, the SWD descriptor and
the board commands against fakeboard.py (run by make -C tools/tests)."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import dfmcfg as d
from fakeboard import FakeLink


def defaults():
    s = d.unpack(bytes(d.SIZE))
    s.update(
        magic=b"DF",
        version=1,
        length=64,
        callsign=b"NZ0P\0\0",
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
    return d.seal(s)


class Checks(unittest.TestCase):
    def test_crc_matches_firmware(self):
        # same known answer as test_settings.c (crc_matches_python)
        self.assertEqual(d.crc(d.unpack(b"123456789" + bytes(55))), 0x2CBE)

    def test_defaults_ok(self):
        self.assertEqual(d.check(defaults()), "OK")

    def test_pack_roundtrip(self):
        s = defaults()
        self.assertEqual(d.unpack(bytes(d.pack(s))), s)

    def test_freq_parsing(self):
        self.assertEqual(d.parse_freq("446175000"), 446175000)
        self.assertEqual(d.parse_freq("446.175MHz"), 446175000)
        self.assertEqual(d.parse_freq("432500 kHz"), 432500000)
        for bad in ("446.175", "4461750", "144.39MHz", "446,175", "fast"):
            with self.assertRaises(d.SettingsError, msg=bad):
                d.parse_freq(bad)

    def test_assignments_rejected(self):
        for bad in (
            "callsign=N0CALL",
            "callsign=NZ0P-12",
            "callsign=TOOLONG1",
            "callsign=NZOP",
            "ssid=16",
            "ssid=1.5",
            "symbol=O",
            "path=WIDE3-3",
            "mode=beacon",
            "power=128",
            "cw_wpm=50",
            "deviation=500",
            "start_paused=maybe",
            "gps_power=on",
            "gps_power=2",
            "colour=red",
            "freq",
        ):
            with self.assertRaises(d.SettingsError, msg=bad):
                d.apply_assignment(defaults(), bad)

    def test_gps_power(self):
        s = defaults()
        self.assertIn("default (duty_cycle)", d.describe(s))
        for name, value in (("always_on", 1), ("DUTY_CYCLE", 0), ("default", 0xFF)):
            d.apply_assignment(s, f"gps_power={name}")
            self.assertEqual(s["gps_power"], value)
            self.assertEqual(d.check(d.seal(s)), "OK")
        d.apply_assignment(s, "gps_power=always_on")
        self.assertIn("gps_power     always_on", d.describe(s))

    def test_lowercase_callsign_uppercased(self):
        s = defaults()
        d.apply_assignment(s, "callsign=ke0abc")
        self.assertEqual(s["callsign"], b"KE0ABC")

    def test_block_rules(self):
        cases = [
            ({"tracker_fix_timeout_s": 300}, "TRACKER_TIMING"),
            ({"tracker_settle_s": 120}, "TRACKER_TIMING"),
            ({"fox_tone_ms": 23000}, "FOX_TIMING"),
            ({"symbol_table": b"a"}, "SYMBOL"),
            ({"path": 0x12}, "PATH"),
            ({"freq_hz": 450000001}, "FREQ"),
            ({"gps_power": 2}, "GPS_POWER"),
        ]
        for change, want in cases:
            self.assertEqual(d.check(d.seal(dict(defaults(), **change))), want, change)
        bad_crc = defaults()
        bad_crc["ssid"] = 1
        self.assertEqual(d.check(bad_crc), "CRC")


class Fields(unittest.TestCase):
    def test_every_field_has_help(self):
        helps = d.field_help()
        for name, _, _ in d.FIELDS:
            self.assertTrue(helps.get(name), name)

    def test_field_values_round_trip(self):
        s = defaults()
        s.update(flags=d.FLAG_START_PAUSED, gps_power=1, freq_hz=432_100_000, path=0x22)
        for name, _, choices in d.FIELDS:
            text = d.field_value(s, name)
            if choices:
                self.assertIn(text, choices, name)
            t = dict(s)
            d.apply_assignment(t, f"{name}={text}")
            self.assertEqual(t, s, name)

    def test_defaults_match_firmware_defaults(self):
        # the defaults in the test above are the firmware's, with a callsign
        want = defaults()
        want["callsign"] = b"N0CALL"
        self.assertEqual(d.defaults(), d.seal(want))


class Versions(unittest.TestCase):
    def test_parse(self):
        self.assertEqual(
            d.parse_fw_version("DFMFW v0.4.0-3-gabc1234 tracker"), ("v0.4.0-3-gabc1234", "tracker")
        )
        self.assertEqual(d.parse_fw_version("garbage"), (None, None))

    def test_match(self):
        self.assertEqual(d.version_match("DFMFW v0.4.0 fox", "v0.4.0"), (True, ""))
        ok, why = d.version_match("DFMFW v0.3.0 tracker", "v0.4.0")
        self.assertFalse(ok)
        self.assertIn("v0.3.0", why)
        self.assertFalse(d.version_match("DFMFW unknown cubeide", "v0.4.0")[0])
        self.assertFalse(d.version_match("DFMFW v0.4.0 tracker", "unknown")[0])
        self.assertFalse(d.version_match("hello", "v0.4.0")[0])


class Descriptor(unittest.TestCase):
    def test_size_matches_c(self):
        # 8 + 4 bytes, then 12 pointers (10 in version 1): swd_info_t on the Cortex-M3
        self.assertEqual(d.SWD_INFO_SIZE, 12 + 12 * 4)
        self.assertEqual(d.SWD_INFO_V1_SIZE, 12 + 10 * 4)

    def test_version_1_still_works_without_send(self):
        b = d.Board(FakeLink(info={"version": 1, "size": d.SWD_INFO_V1_SIZE}))
        self.assertEqual(b.sym["settings_done"], (0x20000184, 4))
        self.assertNotIn("aprs_send_now", b.sym)
        with self.assertRaisesRegex(d.SettingsError, "no send-now trigger"):
            b.send()

    def test_found_anywhere_aligned(self):
        for at in (0x08000000, 0x08001FF0, 0x08003FFC, 0x0800FFC0):
            b = d.Board(FakeLink(swd_at=at))
            self.assertEqual(b.sym["cfg"], (0x20000100, 64), hex(at))

    def test_unaligned_magic_skipped(self):
        link = FakeLink(swd_at=0x08004000)
        link._put(0x08001001, d.SWD_INFO_MAGIC)  # e.g. inside other data
        self.assertEqual(d.Board(link).fw_version.split()[0], "DFMFW")

    def test_missing(self):
        with self.assertRaisesRegex(d.SettingsError, "no SWD descriptor"):
            d.Board(FakeLink(swd_at=None))

    def test_other_settings_layout_refused(self):
        with self.assertRaisesRegex(d.SettingsError, "settings layout is version 2"):
            d.Board(FakeLink(info={"settings_version": 2}))
        with self.assertRaisesRegex(d.SettingsError, "settings layout"):
            d.Board(FakeLink(info={"settings_size": 72}))

    def test_newer_descriptor_accepted(self):
        b = d.Board(FakeLink(info={"version": d.SWD_INFO_VERSION + 1, "size": d.SWD_INFO_SIZE + 8}))
        self.assertEqual(b.sym["settings_done"], (0x20000184, 4))

    def test_bad_pointer_refused(self):
        with self.assertRaisesRegex(d.SettingsError, "isn't in RAM"):
            d.Board(FakeLink(info={"cfg": 0x08000000}))


class BoardCommands(unittest.TestCase):
    def setUp(self):
        self.link = FakeLink(fw_version="DFMFW v9.9.9 fox")
        self.board = d.Board(self.link)

    def test_versions(self):
        self.assertEqual(self.board.fw_version, "DFMFW v9.9.9 fox")
        dev, rev, uid = self.board.mcu()
        self.assertEqual((dev, rev), (0x420, 0x1001))
        self.assertEqual(len(uid), 24)
        self.assertEqual(self.board.flash_kb, 64)

    def test_status_of_defaults(self):
        st = self.board.status()
        self.assertEqual(st["source"], "EEPROM")
        self.assertEqual(st["error"], "OK")
        self.assertFalse(st["tx_allowed"])  # N0CALL

    def test_apply_then_save(self):
        s = self.board.settings()
        d.apply_assignment(s, "callsign=NZ0P")
        d.apply_assignment(s, "freq=432.1MHz")
        self.board.apply(s)
        st = self.board.status()
        self.assertEqual(st["source"], "SWD (not saved)")
        self.assertTrue(st["tx_allowed"])
        self.assertEqual(st["settings"]["freq_hz"], 432_100_000)
        self.assertEqual(d.unpack(self.link.eeprom)["callsign"], b"N0CALL")
        self.board.run("save")
        self.assertEqual(self.board.status()["source"], "EEPROM")
        self.assertEqual(d.unpack(self.link.eeprom)["callsign"], b"NZ0P\0\0")

    def test_apply_and_save_together(self):
        s = self.board.settings()
        d.apply_assignment(s, "callsign=NZ0P")
        self.board.apply(s, save=True)
        self.assertEqual(self.board.status()["source"], "EEPROM")

    def test_invalid_never_sent(self):
        s = self.board.settings()
        s["tracker_settle_s"] = 500
        done = self.board.u32("settings_done")
        with self.assertRaisesRegex(d.SettingsError, "tracker timing"):
            self.board.apply(s)
        self.assertEqual(self.board.u32("settings_done"), done)

    def test_reload_and_defaults(self):
        s = self.board.settings()
        d.apply_assignment(s, "callsign=NZ0P")
        self.board.apply(s)
        self.board.run("reload")
        self.assertEqual(self.board.status()["source"], "EEPROM")
        self.assertEqual(d.callsign_str(self.board.settings()), "N0CALL")
        self.board.run("defaults")
        self.assertEqual(self.board.status()["source"], "built-in defaults")

    def test_send_now(self):
        s = self.board.settings()
        d.apply_assignment(s, "callsign=NZ0P")
        self.board.apply(s)
        self.assertEqual(self.board.send(), (1, 0))
        self.assertEqual(self.board.send(5), (5, 0))
        self.assertEqual(self.board.u32("aprs_packets_sent"), 6)
        self.assertEqual(self.board.u8("aprs_send_now"), 0)

    def test_send_now_refused_without_callsign(self):
        self.assertEqual(self.board.send(2), (0, 2))  # defaults: N0CALL
        self.assertEqual(self.board.u32("si4060_tx_refused"), 2)

    def test_send_count_checked(self):
        for n in (0, 256):
            with self.assertRaisesRegex(d.SettingsError, "1 to 255"):
                self.board.send(n)


if __name__ == "__main__":
    unittest.main()
