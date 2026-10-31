"""dfmgui.py against fakeboard.py: connect, the version check, editing, apply
and save. Needs a display (CI runs it under xvfb-run); skipped without one."""

import os
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import dfmcfg as d
import fakeboard

try:
    import tkinter as tk

    import dfmgui

    ROOT = tk.Tk()
    ROOT.withdraw()
except Exception:  # no tkinter, or no display
    if os.environ.get("CI"):
        raise  # CI runs it under xvfb-run: a skip there would hide a broken set-up
    ROOT = None


@unittest.skipIf(ROOT is None, "no display for Tk")
class Gui(unittest.TestCase):
    def setUp(self):
        self.win = tk.Toplevel(ROOT)
        self.app = dfmgui.App(self.win, simulate=True)

    def tearDown(self):
        self.win.destroy()  # not App.close(): no "unsaved" questions

    def wait(self):
        """run Tk until the worker has nothing left to do"""
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            ROOT.update()
            if not self.app.worker.busy and self.app.worker.results.empty():
                ROOT.update()
                return
            time.sleep(0.01)
        self.fail("the worker didn't finish")

    def connect(self, fw_version=None):
        if fw_version:
            real = fakeboard.FakeLink
            self.addCleanup(setattr, fakeboard, "FakeLink", real)
            fakeboard.FakeLink = lambda: real(fw_version=fw_version)
        self.app.toggle_connect()
        self.wait()
        self.assertIsNotNone(self.app.board)

    def state(self, key):
        return str(self.app.buttons[key].cget("state"))

    def test_connect_shows_board(self):
        self.connect()
        self.assertIn("DFMFW", self.app.info["firmware"].cget("text"))
        self.assertEqual(self.app.info["match"].cget("text"), "matches")
        self.assertIn("0x420", self.app.info["mcu"].cget("text"))
        self.assertEqual(self.app.vars["callsign"].get(), "N0CALL")
        self.assertEqual(self.state("apply"), "disabled")  # nothing changed
        self.assertEqual(self.state("save"), "normal")

    def test_mismatch_is_read_only(self):
        self.connect("DFMFW v0.0.1 tracker")
        self.assertIn("v0.0.1", self.app.info["match"].cget("text"))
        self.assertEqual(str(self.app.w_callsign.cget("state")), "disabled")
        self.assertEqual(self.state("save"), "disabled")
        self.app.allow.set(True)
        self.app.update_state()
        self.assertEqual(str(self.app.w_callsign.cget("state")), "normal")
        self.assertEqual(self.state("save"), "normal")

    def test_edit_check_apply_save(self):
        self.connect()
        self.app.vars["tracker_settle_s"].set("500")
        self.assertIn("tracker timing", self.app.message.cget("text"))
        self.assertEqual(self.state("apply"), "disabled")
        self.app.vars["tracker_settle_s"].set("10")
        self.app.vars["callsign"].set("nz0p")
        self.app.vars["freq"].set("432.1MHz")
        self.assertEqual(self.state("apply"), "normal")
        self.assertEqual(self.app.marks["callsign"].cget("text"), "●")

        self.app.apply(save=False)
        self.wait()
        self.assertEqual(self.app.vars["callsign"].get(), "NZ0P")
        self.assertEqual(self.app.marks["callsign"].cget("text"), " ")
        self.assertIn("not saved", self.app.info["source"].cget("text"))
        self.assertTrue(self.app.unsaved())
        self.assertEqual(self.app.info["tx"].cget("text"), "allowed")

        self.app.command("save")
        self.wait()
        self.assertFalse(self.app.unsaved())
        link = self.app.board.link
        self.assertEqual(d.unpack(link.eeprom)["freq_hz"], 432_100_000)

    def test_bad_value_named(self):
        self.connect()
        self.app.vars["ssid"].set("16")
        self.assertIn("ssid", self.app.message.cget("text"))
        self.assertEqual(self.state("apply_save"), "disabled")


if __name__ == "__main__":
    unittest.main()
