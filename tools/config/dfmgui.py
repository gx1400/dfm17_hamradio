#!/usr/bin/env python3
"""dfmgui.py: a window for a DFM-17's versions and station settings over SWD.

    python3 tools/config/dfmgui.py [--simulate]
    dfmgui.py --version | --self-test OUT.json     (packaged app checks, no window)

The same checks and board access as dfmcfg.py (docs/config-gui.md): connect
through an ST-Link with pyOCD (or STM32CubeProgrammer), show the firmware and
MCU versions, read the settings, change them in the form, apply and save them
to the EEPROM. The board keeps running throughout.

The tools are released with the firmware: if the board runs another version,
the settings are shown read-only unless "allow changes anyway" is ticked.
"""

import contextlib
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import messagebox, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dfmcfg as d

TITLE = "DFM-17 settings"


class Worker:
    """runs board calls one at a time off the Tk thread, so a slow or lost
    probe never freezes the window; results come back through poll()"""

    def __init__(self, root):
        self.root = root
        self.jobs = queue.Queue()
        self.results = queue.Queue()
        self.busy = False
        threading.Thread(target=self._loop, daemon=True).start()
        self._poll()

    def run(self, fn, done, failed):
        self.busy = True
        self.jobs.put((fn, done, failed))

    def _loop(self):
        while True:
            fn, done, failed = self.jobs.get()
            try:
                self.results.put((done, fn()))
            except d.SettingsError as e:
                self.results.put((failed, str(e)))
            except Exception as e:  # noqa: BLE001 anything from a probe library: report it, keep going
                self.results.put((failed, f"{type(e).__name__}: {e}"))

    def _poll(self):
        while not self.results.empty():
            cb, value = self.results.get()
            self.busy = not self.jobs.empty()
            cb(value)
        self.root.after(50, self._poll)


class Tooltip:
    def __init__(self, widget, text):
        self.widget, self.text, self.tip = widget, text, None
        widget.bind("<Enter>", self.show)
        widget.bind("<Leave>", self.hide)

    def show(self, _):
        x = self.widget.winfo_rootx() + 20
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.tip = tk.Toplevel(self.widget)
        self.tip.wm_overrideredirect(True)
        self.tip.wm_geometry(f"+{x}+{y}")
        ttk.Label(self.tip, text=self.text, wraplength=360, relief="solid", padding=4).pack()

    def hide(self, _):
        if self.tip:
            self.tip.destroy()
            self.tip = None


class App:
    def __init__(self, root, simulate=False):
        self.root = root
        self.simulate = simulate
        self.valid = True
        self.worker = Worker(root)
        self.board = None
        self.loaded = None  # settings as last read from the board
        self.matched = False
        self.vars, self.marks = {}, {}
        root.title(TITLE)
        root.protocol("WM_DELETE_WINDOW", self.close)
        self._build()
        self.refresh_probes()
        self.update_state()

    # ---- layout -------------------------------------------------------------

    def _build(self):
        pad = {"padx": 6, "pady": 3}
        top = ttk.Frame(self.root, padding=6)
        top.pack(fill="x")
        ttk.Label(top, text="Link").pack(side="left")
        links = ["simulated"] if self.simulate else list(d.LINKS)
        self.link = tk.StringVar(value=links[0])
        link = ttk.Combobox(top, textvariable=self.link, values=links, width=9, state="readonly")
        link.pack(side="left", **pad)
        link.bind("<<ComboboxSelected>>", lambda _: self.refresh_probes())
        ttk.Label(top, text="Probe").pack(side="left")
        self.probe = tk.StringVar()
        self.probe_box = ttk.Combobox(top, textvariable=self.probe, width=34, state="readonly")
        self.probe_box.pack(side="left", **pad)
        ttk.Button(top, text="↻", width=3, command=self.refresh_probes).pack(side="left")
        self.connect_btn = ttk.Button(top, text="Connect", command=self.toggle_connect)
        self.connect_btn.pack(side="left", **pad)
        self.conn_label = ttk.Label(top, text="not connected")
        self.conn_label.pack(side="left", **pad)

        info = ttk.LabelFrame(self.root, text="Board", padding=6)
        info.pack(fill="x", padx=6)
        self.info = {}
        rows = [
            ("firmware", "Firmware"),
            ("tools", "These tools"),
            ("match", "Version check"),
            ("mcu", "MCU"),
            ("source", "Settings"),
            ("tx", "Transmit"),
            ("counters", "Counters"),
        ]
        for i, (key, label) in enumerate(rows):
            ttk.Label(info, text=label).grid(row=i, column=0, sticky="w", padx=(0, 12))
            self.info[key] = ttk.Label(info, text="-")
            self.info[key].grid(row=i, column=1, sticky="w")
        self.info["tools"].config(text=d.tool_version())
        self.allow = tk.BooleanVar(value=False)
        self.allow_box = ttk.Checkbutton(
            info,
            text="allow changes anyway",
            variable=self.allow,
            command=self.update_state,
        )

        form = ttk.Frame(self.root, padding=6)
        form.pack(fill="both", expand=True)
        helps = d.field_help()
        groups = {}
        for name, group, choices in d.FIELDS:
            if group not in groups:
                frame = ttk.LabelFrame(form, text=group, padding=6)
                frame.grid(row=len(groups) // 2, column=len(groups) % 2, sticky="nsew", **pad)
                groups[group] = frame
            frame = groups[group]
            row = frame.grid_size()[1]
            label = ttk.Label(frame, text=name)
            label.grid(row=row, column=0, sticky="w")
            var = tk.StringVar()
            if choices:
                w = ttk.Combobox(
                    frame, textvariable=var, values=choices, width=16, state="readonly"
                )
            else:
                w = ttk.Entry(frame, textvariable=var, width=18)
            w.grid(row=row, column=1, sticky="w", padx=6, pady=2)
            mark = ttk.Label(frame, text=" ", width=2)
            mark.grid(row=row, column=2)
            Tooltip(label, helps.get(name, ""))
            Tooltip(w, helps.get(name, ""))
            var.trace_add("write", lambda *_: self.validate())
            self.vars[name], self.marks[name] = var, mark
            setattr(self, f"w_{name}", w)
        form.columnconfigure(0, weight=1)
        form.columnconfigure(1, weight=1)

        self.message = ttk.Label(self.root, text="", padding=(8, 2), wraplength=640)
        self.message.pack(fill="x")
        bar = ttk.Frame(self.root, padding=6)
        bar.pack(fill="x")
        self.buttons = {}
        for key, text, cmd in [
            ("read", "Read", self.read),
            ("apply", "Apply", lambda: self.apply(save=False)),
            ("apply_save", "Apply + Save", lambda: self.apply(save=True)),
            ("save", "Save", lambda: self.command("save")),
            ("reload", "Reload from EEPROM", lambda: self.command("reload")),
            ("defaults", "Defaults", self.defaults),
        ]:
            b = ttk.Button(bar, text=text, command=cmd)
            b.pack(side="left", padx=3)
            self.buttons[key] = b

    # ---- state ----------------------------------------------------------------

    def say(self, text, error=False):
        self.message.config(text=text, foreground="#b00020" if error else "")

    def writable(self):
        return self.board is not None and (self.matched or self.allow.get())

    def changes(self):
        """{name: text} of the fields that differ from the board's settings"""
        if self.loaded is None:
            return {}
        return {
            n: v.get() for n, v in self.vars.items() if v.get() != d.field_value(self.loaded, n)
        }

    def edited(self):
        """the board's settings with the form's changes, or raise SettingsError"""
        s = dict(self.loaded)
        for name, text in self.changes().items():
            d.apply_assignment(s, f"{name}={text}")
        err = d.check(d.seal(s))
        if err != "OK":
            raise d.SettingsError(d.WHY.get(err, err))
        return s

    def validate(self):
        if self.loaded is None:
            return
        changed = self.changes()
        for name, mark in self.marks.items():
            mark.config(text="●" if name in changed else " ")
        try:
            self.edited()
            self.valid = True
            if changed:
                self.say(f"changed, not applied: {', '.join(changed)}")
            else:
                self.say("")
        except d.SettingsError as e:
            self.valid = False
            self.say(str(e), error=True)
        self.update_state()

    def update_state(self):
        busy = self.worker.busy
        connected = self.board is not None
        can_write = self.writable() and not busy
        changed = bool(self.changes())
        ok = getattr(self, "valid", True)
        self.connect_btn.config(text="Disconnect" if connected else "Connect")
        self.buttons["read"].config(state="normal" if connected and not busy else "disabled")
        for key in ("apply", "apply_save"):
            self.buttons[key].config(state="normal" if can_write and changed and ok else "disabled")
        for key in ("save", "reload", "defaults"):
            self.buttons[key].config(state="normal" if can_write else "disabled")
        field_state = "normal" if self.writable() else "disabled"
        for name, _, choices in d.FIELDS:
            w = getattr(self, f"w_{name}")
            w.config(
                state=("readonly" if choices else "normal")
                if field_state == "normal"
                else "disabled"
            )

    # ---- probe and connection -----------------------------------------------

    def refresh_probes(self):
        if self.link.get() != "pyocd":
            note = {"cube": "(STM32CubeProgrammer picks the probe)", "simulated": "(no hardware)"}
            self.probe_box.config(values=[note[self.link.get()]])
            self.probe_box.current(0)
            return
        try:
            found = d.PyOcdLink.probes()
        except Exception as e:  # noqa: BLE001 pyOCD missing, or its USB back end failing
            found = []
            self.say(f"can't list probes: {e}", error=True)
        self.probe_ids = [uid for uid, _ in found]
        names = [f"{desc} ({uid})" for uid, desc in found] or ["no probe found"]
        self.probe_box.config(values=names)
        self.probe_box.current(0)

    def toggle_connect(self):
        if self.board:
            self.disconnect()
            return
        link_name = self.link.get()
        uid = None
        if link_name == "pyocd":
            i = self.probe_box.current()
            uid = self.probe_ids[i] if 0 <= i < len(getattr(self, "probe_ids", [])) else None

        def work():
            if link_name == "simulated":
                from fakeboard import FakeLink

                link = FakeLink()
            elif link_name == "pyocd":
                link = d.PyOcdLink(uid)
            else:
                link = d.CubeProgrammerLink()
            try:
                board = d.Board(link)
                return board, board.mcu(), board.status()
            except Exception:
                link.close()
                raise

        self.conn_label.config(text="connecting...")
        self.worker.run(work, self.connected, self.failed)
        self.update_state()

    def connected(self, result):
        self.board, mcu, status = result
        self.conn_label.config(text=f"connected ({self.board.link.name})", foreground="#1b7a1b")
        self.info["firmware"].config(text=self.board.fw_version)
        dev, rev, uid = mcu
        self.info["mcu"].config(
            text=f"device 0x{dev:03X} rev 0x{rev:04X}, flash {self.board.flash_kb} KB, UID {uid}"
        )
        self.matched, why = d.version_match(self.board.fw_version)
        if self.matched:
            self.info["match"].config(text="matches", foreground="#1b7a1b")
            self.allow_box.grid_remove()
        else:
            self.info["match"].config(
                text=f"{why}: settings are read-only. Use the app released with the "
                "board's firmware, or flash this version.",
                foreground="#b00020",
                wraplength=520,
            )
            self.allow.set(False)
            self.allow_box.grid(row=len(self.info), column=1, sticky="w")
        self.show_status(status)
        self.say("")

    def disconnect(self):
        if self.unsaved() and not messagebox.askyesno(
            TITLE, "The board has applied settings that aren't saved to the EEPROM. Disconnect?"
        ):
            return
        board, self.board, self.loaded = self.board, None, None
        self.worker.run(board.link.close, lambda _: None, lambda _: None)
        self.conn_label.config(text="not connected", foreground="")
        for key in ("firmware", "match", "mcu", "source", "tx", "counters"):
            self.info[key].config(text="-", foreground="")
        self.allow_box.grid_remove()
        for name in self.vars:
            self.marks[name].config(text=" ")
        self.update_state()

    def failed(self, text):
        if self.board is None:
            self.conn_label.config(text="not connected", foreground="")
        self.say(text, error=True)
        self.update_state()

    # ---- settings -------------------------------------------------------------

    def show_status(self, st):
        self.status = st
        self.loaded = st["settings"]
        for name, var in self.vars.items():
            var.set(d.field_value(self.loaded, name))
        unsaved = " (applied, not saved to the EEPROM)" if self.unsaved() else ""
        self.info["source"].config(
            text=f"from {st['source']}{unsaved}; last check {st['error']}",
            foreground="#b06000" if unsaved else "",
        )
        tx = "allowed" if st["tx_allowed"] else "disabled: no callsign set up (N0CALL)"
        self.info["tx"].config(text=tx, foreground="" if st["tx_allowed"] else "#b00020")
        self.info["counters"].config(
            text=f"key-ups refused {st['tx_refused']}, live settings restored {st['repairs']}"
        )
        self.validate()

    def unsaved(self):
        return self.board is not None and getattr(self, "status", {}).get("source") == d.SOURCES[2]

    def read(self):
        self.worker.run(self.board.status, self.show_status, self.failed)
        self.update_state()

    def apply(self, save):
        try:
            s = self.edited()
        except d.SettingsError as e:
            self.say(str(e), error=True)
            return
        board = self.board

        def work():
            board.apply(s, save=save)
            return board.status()

        def done(st):
            self.show_status(st)
            self.say("applied and saved to the EEPROM" if save else "applied, not saved yet")

        self.worker.run(work, done, self.failed)
        self.update_state()

    def command(self, name):
        board = self.board

        def work():
            board.run(name)
            return board.status()

        def done(st):
            self.show_status(st)
            self.say(f"{name}: OK")

        self.worker.run(work, done, self.failed)
        self.update_state()

    def defaults(self):
        if messagebox.askyesno(
            TITLE,
            "Use the built-in defaults? The callsign becomes N0CALL, so the board stops "
            "transmitting. Nothing is saved until you press Save.",
        ):
            self.command("defaults")

    def close(self):
        if self.changes() and not messagebox.askyesno(TITLE, "Discard the changes in the form?"):
            return
        if self.unsaved() and not messagebox.askyesno(
            TITLE, "The board has applied settings that aren't saved to the EEPROM. Quit?"
        ):
            return
        if self.board:
            # closing a dead probe can fail too: nothing more to do then
            with contextlib.suppress(Exception):
                self.board.link.close()
        self.root.destroy()


def self_test(path):
    """for the release build (app.yml): the packaged app's version, pyOCD and
    its USB back end loading, and the board code against the simulated board,
    written as JSON (a windowed app has no console on Windows)"""
    import json

    from fakeboard import FakeLink

    out = {"version": d.tool_version()}
    try:
        out["probes"] = [desc for _, desc in d.PyOcdLink.probes()]
    except Exception as e:  # noqa: BLE001 reported, and the CI step fails on it
        out["probes_error"] = f"{type(e).__name__}: {e}"
    board = d.Board(FakeLink())
    out["simulated"] = {"fw": board.fw_version, "match": d.version_match(board.fw_version)[0]}
    with open(path, "w") as f:
        json.dump(out, f, indent=2)


def main():
    args = sys.argv[1:]
    if "--version" in args:
        print(d.tool_version())
        return
    if "--self-test" in args:
        self_test(args[args.index("--self-test") + 1])
        return
    simulate = "--simulate" in args  # a simulated board, for working on the window
    root = tk.Tk()
    App(root, simulate)
    root.mainloop()


if __name__ == "__main__":
    main()
