"""Unit tests for dfmcfg.py's checks (run by make -C tools/tests)."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import dfmcfg as d


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


if __name__ == "__main__":
    unittest.main()
