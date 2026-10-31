# Settings app over SWD (#47)

A small desktop app, `tools/config/dfmgui.py`, that connects to a DFM-17
through an ST-Link, shows what the board is running, and reads and changes its
station settings ([settings.md](settings.md)). It is a window on the same code
as the command line tool, [`dfmcfg.py`](../tools/config/dfmcfg.py): the same
checks, messages and board access, so the two can't disagree.

```
python3 tools/config/dfmgui.py              # pip install -r tools/requirements-config.txt
python3 tools/config/dfmgui.py --simulate   # a simulated board, no hardware
```

Releases include a ready-to-run app for each platform (see
[Releases](#releases-and-versions)); it needs no Python and no toolchain.

- Runs on Linux, Windows and macOS, x86-64 and arm64, from one code base
  (Python and Tkinter).
- Shows the firmware version, the MCU (device, revision, flash size, unique
  ID), where the settings came from, the last check result, whether the board
  may transmit, and its counters.
- The settings form is checked as you type with the firmware's own rules;
  changed fields are marked, and Apply stays disabled while anything is
  invalid, with the message naming the field. Apply, Apply + Save, Save,
  Reload from EEPROM, Defaults.
- The board keeps running while the app reads and writes (no halt, no
  reset). Board calls run off the window's thread, so a slow or lost probe
  doesn't freeze it.
- Leaving with changes in the form, or with applied settings not saved to the
  EEPROM, asks first.

Not in scope (later, if wanted): flashing firmware, live telemetry, the PPS
and diag counters beyond the two shown, serial (UART) access, the NFC side of
the EEPROM, the GPS and radio chip versions (not kept in RAM by the firmware).

## How it finds the board's settings: the SWD descriptor

The firmware's SWD mailbox (settings_store.c: `settings_edit`, `settings_cmd`,
`settings_result`, `settings_done`) lives at addresses that change with every
build. `dfmcfg.py` used to look them up in the build's ELF, which someone
running a release doesn't have. Instead, the firmware now carries a small
descriptor in flash, `swd_info` (settings.h, settings_store.c):

| Field | |
|---|---|
| `magic` | `"DFM-SWD\0"`, what the tools scan flash for (4-byte aligned) |
| `version`, `size` | the descriptor's own layout version and size |
| `settings_version`, `settings_size` | `SETTINGS_VERSION` and `sizeof(settings_t)` |
| `fw_version` | the version string, `DFMFW <git describe> <variant>` |
| `cfg`, `settings_edit` | live settings, and the copy the tools edit |
| `settings_cmd`, `settings_result`, `settings_done` | the mailbox |
| `settings_source`, `settings_error` | where the live settings came from, last check |
| `settings_repairs`, `tx_refused` | counters |
| `aprs_send_now`, `aprs_packets_sent` | version 2: the send-now trigger and the packets-sent counter (#51) |

60 B of flash (52 B in version 1); `init.c` prints the version string through
it, which keeps it in the image. The tools scan flash for the magic, 8 KB per
read, so no linker script change was needed.

Rules for changing it (`SWD_INFO` in dfmcfg.py mirrors it):

- Fields are only ever appended, and `SWD_INFO_VERSION` goes up. Tools accept
  a newer descriptor (they read the fields they know). They also accept an
  older one, using only the fields its `size` covers: a version 1 board works
  for settings, and `send` reports that its firmware has no trigger.
- `make -C tools/tests` checks the pointers against the variables;
  `dfmcfg.py --check-elf` checks every built ELF's descriptor against its
  symbol table and sizes (CI, firmware job, and the release).

Older firmware without the descriptor: `dfmcfg.py --elf <the build's ELF>`
still works. The app needs the descriptor.

## Keeping the app in step with the firmware

The app and `dfmcfg.py` are part of the firmware, not a separate product:

- **One layout.** The settings block layout, limits, error texts and help
  (`LAYOUT`, `check`, `WHY`, `FIELDS_HELP`, `FIELDS`) are in `dfmcfg.py` only.
  The form is generated from `FIELDS`, so a new setting needs a line there
  and in `FIELDS_HELP`, plus `apply_assignment` and `field_value` for its
  text form; `test_dfmcfg.py` round-trips every field and fails on missing
  help.
- **Same PR.** A change to `settings_t`, `settings_check()` or the mailbox
  changes `dfmcfg.py` in the same PR. The host tests compare `LAYOUT` with the
  C offsets, and `crc` and `check` with the firmware's.
- **Layout version.** A new or moved field in `settings_t` bumps
  `SETTINGS_VERSION` (settings.md). The tools refuse a board whose settings
  version or size differs from theirs, before reading or writing anything.

## Releases and versions

The app carries the firmware's version: `tool_version()` is `git describe
--tags --always --dirty`, the command the Makefile stamps the firmware with,
or for a packaged app the value `build_app.py` wrote into it. A release tag
builds both from the same commit, so they carry the same version.

On connect, the app (and `dfmcfg.py`) compares the board's version string with
its own:

- **Match:** shown in green; everything is enabled.
- **Mismatch** (another release, a development build, or `unknown`, e.g. a
  CubeIDE build): shown in red with both versions; the settings are
  read-only. Ticking "allow changes anyway" (`dfmcfg.py --allow-mismatch`)
  enables them, which is safe as long as the settings layout matches, and
  that's checked separately (above). The fix is to use the app from the
  board's release, or flash the app's.

In a checkout, a firmware build and the tools match as long as both come from
the same commit, with the same uncommitted changes (`-dirty`).

The Release workflow (release.yml) calls [app.yml](../.github/workflows/app.yml),
which packages the app with PyInstaller (`tools/config/build_app.py`) on

| Runner | Release file |
|---|---|
| ubuntu-24.04 | `dfm17-config-<tag>-linux-x64` |
| ubuntu-24.04-arm | `dfm17-config-<tag>-linux-arm64` (Raspberry Pi OS 64-bit) |
| windows-latest | `dfm17-config-<tag>-windows-x64.exe` |
| macos-latest | `dfm17-config-<tag>-macos-arm64.zip` (an .app) |

and runs each app's `--self-test`: its version must equal the tag, pyOCD and
its USB back end must load, and the board code must work against the
simulated board. The apps go into the same GitHub release as the firmware,
with their checksums in `SHA256SUMS`. app.yml also runs on pull requests that
change `tools/config`, so a broken package shows up before a release.

Not built: Windows on arm64 (no cmsis-pack-manager wheel for pyOCD) and Intel
Macs; both can run the app from a checkout. The apps aren't signed, so
Windows SmartScreen and macOS Gatekeeper warn on first start (macOS: right
click, Open).

## Debug probe access

`--link pyocd` (default): [pyOCD](https://pyocd.io), a pip-installable library
that drives ST-Link V2/V2-1/V3 (and CMSIS-DAP probes) through libusb on all
three systems and both architectures. One connection, attached without
halting (`connect_mode="attach"`, the generic `cortex_m` target, so no device
pack), memory read and written directly in milliseconds. Set-up:

- Linux: the ST-Link udev rules (from STM32CubeIDE / stlink-tools, or
  [pyOCD's](https://github.com/pyocd/pyOCD/tree/main/udev)).
- Windows: the ST-Link USB driver (comes with STM32CubeProgrammer / CubeIDE,
  or ST's STSW-LINK009).
- macOS: nothing.

`--link cube`: STM32_Programmer_CLI in hot-plug mode, as `dfmcfg.py` always
did; one process per access, so several seconds per refresh, and no Linux
arm64 build. Kept for set-ups where pyOCD can't reach the probe.

## Code

| File | |
|---|---|
| `tools/config/dfmcfg.py` | settings layout and checks, field model, versions, descriptor, links (pyOCD, CubeProgrammer), `Board`, and the CLI |
| `tools/config/dfmgui.py` | the window (Tkinter), `--simulate`, `--self-test` |
| `tools/config/fakeboard.py` | a simulated board: flash with a descriptor, RAM, and the mailbox commands, as settings_store.c runs them |
| `tools/config/test_dfmcfg.py` | checks, fields, versions, descriptor scan, board commands (in `make -C tools/tests`) |
| `tools/config/test_dfmgui.py` | the window against the simulated board (CI: under `xvfb-run`) |
| `tools/config/build_app.py` | PyInstaller packaging, stamps the version |

Why Tkinter: it comes with Python on every target (Linux: `python3-tk`),
needs no extra wheels, and one window with a form doesn't need more. PySide6
would look more native but adds about 100 MB per app.

## Bench checks still to do

The app and the new `dfmcfg.py` were tested against the simulated board and
the descriptor against every built ELF, but not yet on hardware (no ST-Link
attached while this was written). To do on board 1:

1. Flash this build; `dfmcfg.py show` (pyOCD) and `--link cube`: same output.
2. The app: connect, versions, apply, save, power cycle, read back.
3. A board running an older build: the mismatch warning, read-only form.
4. The packaged Linux app; Windows and macOS when a machine is at hand.
