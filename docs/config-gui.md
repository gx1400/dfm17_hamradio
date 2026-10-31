# Settings GUI over SWD: scope

A small desktop app that connects to a DFM-17 through an ST-Link, shows what
the board is running, and reads and changes its station settings. It is a
front end for what [`tools/config/dfmcfg.py`](../tools/config/dfmcfg.py) does
from the command line ([settings.md](settings.md)), not a new protocol.

## Goals

- Runs on Linux, Windows and macOS, x86-64 and arm64 (Raspberry Pi, Apple
  silicon), from one code base.
- Connect, show versions, show the settings with their source (EEPROM /
  defaults) and the last check result.
- Edit the settings in a form, check them before sending, apply, save to the
  EEPROM, reload, restore defaults.
- No ELF file needed on the user's side: the GUI must work with a board
  flashed from a release, by someone who never built the firmware.
- Hands-off for the firmware: the board keeps running (no halt, no reset)
  while the GUI reads and writes, as `dfmcfg.py` does today (`mode=HOTPLUG`).

Not in scope (later, if wanted): flashing firmware, live telemetry plots,
the PPS / diag counters beyond a few read-only values, serial (UART) access,
the NFC side of the EEPROM.

## How it talks to the board today

`dfmcfg.py` finds firmware variables by name with `arm-none-eabi-nm` on the
build's ELF, then uses `STM32_Programmer_CLI` to read and write RAM while the
firmware runs:

| Symbol | Use |
|---|---|
| `fw_version` | version string, compared with the ELF to make sure the addresses fit |
| `cfg` | live settings (64 B `settings_t`) |
| `settings_edit` | scratch copy the tool writes before an apply |
| `settings_cmd` | command byte: edit, apply, save, reload, defaults |
| `settings_done`, `settings_result` | completion counter and result of the last command |
| `settings_source`, `settings_error` | where the live settings came from, last check result |
| `si4060_tx_refused`, `settings_repairs` | counters shown in `show` |

The field layout, value limits, error texts and help (`LAYOUT`, `check`,
`WHY`, `FIELDS_HELP`) are in `dfmcfg.py` and checked against the C structure
by `make -C tools/tests`.

Two things stop that from becoming a cross-platform GUI as is:

1. **The ELF and `nm`.** A user of a release build has neither the ELF of
   their exact build nor the ARM toolchain.
2. **STM32_Programmer_CLI.** Proprietary, a large install, no Linux arm64
   build, and one process per read (about 0.3-1 s each), so a full refresh is
   several seconds.

## Proposed design

### Firmware: a debug descriptor (small)

A `const` structure in flash that holds the addresses the tool needs, found
without the ELF:

```c
/* read over SWD by tools/config (docs/config-gui.md); layout is versioned */
typedef struct {
	char magic[8];            /* "DFMDBG1\0" */
	uint8_t version, size;    /* descriptor layout version and byte size */
	uint8_t settings_size;    /* sizeof(settings_t), 64 */
	uint8_t reserved;
	const char *fw_version;
	settings_t *cfg;
	settings_t *settings_edit;
	volatile uint8_t *settings_cmd, *settings_result, *settings_source, *settings_error;
	volatile uint32_t *settings_done, *settings_repairs, *si4060_tx_refused;
} dbg_descriptor_t;
```

The tool finds it either at a fixed address (a dedicated linker section placed
right after the vector table), or by scanning the
first few KiB of flash for the magic. The fixed section is simpler and faster;
the scan needs no linker change. Recommendation: the linker section, with the
scan as a fallback.

Cost: about 60 B of flash, no RAM, no code. `dfmcfg.py` can use it too and
drop its `nm` dependency, keeping `--elf` for older builds.

### Debug probe: pyOCD instead of STM32_Programmer_CLI

[pyOCD](https://pyocd.io) is a pip-installable Python library that drives
ST-Link V2/V2-1/V3 (and CMSIS-DAP probes) through libusb on all three systems
and both architectures. Through its Python API the tool keeps one connection
open and reads and writes memory directly, in milliseconds:

```python
from pyocd.core.helpers import ConnectHelper
with ConnectHelper.session_with_chosen_probe(target_override="cortex_m",
        connect_mode="attach", options={"frequency": 1_000_000}) as s:
    t = s.target
    raw = bytes(t.read_memory_block8(addr, 64))
```

- `connect_mode="attach"`: no halt, no reset (same as `HOTPLUG`).
- The generic `cortex_m` target is enough for memory access, so no STM32
  device pack is needed.
- Windows: the ST-Link driver (WinUSB) is required, as for STM32CubeProgrammer.
  Linux: the ST-Link udev rules. macOS: nothing extra (libusb comes with the
  `libusb-package` wheel).

Keep STM32_Programmer_CLI as a fallback transport behind the same small
interface (`read(addr, n)`, `write(addr, bytes)`), so the current setup keeps
working and the two can be compared on the bench.

### Code structure

Split `dfmcfg.py` into a library and its command line, so the GUI and CLI
share every check and message:

```
tools/config/
  dfmcfg/
    settings.py   LAYOUT, pack/unpack, crc, check, WHY, field help (from dfmcfg.py)
    board.py      descriptor lookup, commands (edit/apply/save/...), status
    transport.py  PyOcdTransport, CubeProgrammerTransport (current code)
  dfmcfg.py       the CLI, unchanged commands and output
  dfmgui.py       the GUI
  test_dfmcfg.py  existing tests, plus a fake transport for board.py
```

### GUI toolkit: Tkinter

Tkinter comes with Python on all the targets (on Linux, the `python3-tk`
package), needs no extra wheels, and is plenty for one window with a form.
PySide6 (Qt) looks more native but adds about 100 MB, and has no wheels for
some arm64 Linux set-ups. A browser UI (local web server) is an option if the
GUI ever has to run on a headless Pi with a remote browser, but it's more code.

### Window

```
+-----------------------------------------------------------------+
| Probe: [ST-Link V2 066DFF...  v]  [Connect]      ● connected     |
+-----------------------------------------------------------------+
| Firmware  DFMFW v0.4.0-12-g1a2b3c tracker                       |
| Board     MCU 0x420 STM32F100 rev Z, UID 0x...                  |
| Settings  from EEPROM, check OK, TX allowed                     |
| Counters  key-ups refused 0, settings repaired 0                |
+-----------------------------------------------------------------+
| Station    Callsign [NZ0P  ]  SSID [12]  Symbol [/O]  Path [v]  |
| Mode       [tracker v]                                          |
| Radio      Freq [446.175] MHz  PA [32]  Deviation [2600] Hz     |
| Tracker    Period [300] s  Fix timeout [120] s  Settle [10] s   |
|            GPS power [duty cycle v]                             |
| Fox        Period [30] s  Tone [10000] ms  APRS every [10]  CW [15] wpm |
+-----------------------------------------------------------------+
| ⚠ freq: 451.000 MHz is outside 420-450 MHz                      |
| [Read]  [Apply]  [Apply + Save]  [Reload from EEPROM] [Defaults]|
+-----------------------------------------------------------------+
```

- Fields are built from the settings module's field list, so a new setting
  shows up with its limits and help text (tooltip) without GUI changes.
- Each field is checked as you type with the same `check()` the CLI uses; the
  Apply buttons stay disabled while anything is invalid, and the message names
  the field.
- Changed but unsaved fields are marked; closing the window with unsaved
  changes asks first.
- The connection runs in a worker thread so a slow or lost probe never
  freezes the window; a lost connection greys the form and offers Reconnect.

"Versions" means the firmware version string (from the descriptor), the MCU
ID code and revision (`DBGMCU_IDCODE`, 0xE0042000) and the 96-bit unique ID
(0x1FFFF7E8), all readable now. The GPS module's version (UBX-MON-VER) and the
Si4063 part info aren't kept in RAM today; adding them would cost firmware
flash and RAM, so they're a later option.

### Packaging

- Developers: `pip install -r tools/requirements-gui.txt` (pyOCD pinned),
  `python tools/config/dfmgui.py`.
- Users: PyInstaller one-file builds per platform, made in CI on a matrix
  (ubuntu-24.04 x64 and arm64, windows-latest, macos-latest arm64 and x64)
  and attached to releases. Unsigned, so Windows and macOS warn on first
  start; signing is out of scope.

## Work breakdown

1. Firmware: the debug descriptor, its linker section, a host test of its
   contents and a size report (about +60 B flash).
2. Split `dfmcfg.py` into the library; CLI behaviour and tests unchanged.
3. pyOCD transport and descriptor lookup; `dfmcfg.py --probe pyocd`; bench
   check on board 1 against the CubeProgrammer transport.
4. The GUI on Linux.
5. Try it on Windows and macOS; the PyInstaller CI matrix and release assets.

Steps 1-3 also make the CLI faster and drop its toolchain dependency, so
they're worth doing even without the GUI.

## Open questions

- Fixed descriptor address vs flash scan: a linker section means editing the
  CubeMX linker script (`STM32F100R8TX_FLASH.ld`), which CubeIDE regenerates
  only on request. Acceptable?
- Older boards without the descriptor: keep `--elf` support in the GUI
  (pick the ELF file), or require a reflash?
- Should the GUI also flash firmware? pyOCD can, which would make it a
  one-stop tool for non-developers, but it's a bigger and riskier feature.
