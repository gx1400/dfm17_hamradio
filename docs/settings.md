# Station settings

The firmware reads its station settings from the NFC EEPROM (M24LR04E-R)
at boot. One firmware build serves every board and mode. A board that
hasn't been set up uses the built-in defaults with the callsign **N0CALL**,
and **doesn't transmit at all** until a real callsign is set. Issue #18.

## Setting up a board

Back up the EEPROM first, so Graw's factory data is kept:
`tools/hw_survey/eeprom_backup.sh`. Then, with the board running a build from
this repository and the ST-Link connected, use the settings app
([config-gui.md](config-gui.md): a window, also released ready to run for each
platform) or the command line:

```sh
pip install -r tools/requirements-config.txt   # pyOCD, for the ST-Link
make firmware
# flash build/tracker/dfm17.elf, then:
tools/config/dfmcfg.py set callsign=NZ0P ssid=12 --save
tools/config/dfmcfg.py show
```

`set` applies the change immediately. It survives a reset only once saved:
`--save`, or `dfmcfg.py save` later. Other commands:

| Command | What it does |
|---|---|
| `show` | the settings in use, where they came from, whether the board may transmit |
| `set name=value ... [--save]` | change settings; checked first, refused if invalid |
| `save` | write the settings in use to the EEPROM |
| `reload` | load the EEPROM settings again (undoes unsaved changes) |
| `defaults` | use the built-in defaults (N0CALL, no transmitting) until a reset or `reload` |
| `check name=value ...` | check values offline, without a board |
| `fields` | the list below |

The script finds the settings through the descriptor the firmware keeps in
flash (`swd_info`, [config-gui.md](config-gui.md)) and compares the board's
firmware version with its own (`git describe`, as the firmware is stamped).
If they differ, `show` warns and the commands that change something refuse
unless given `--allow-mismatch`; a different settings layout is always
refused. Other options:

| Option | |
|---|---|
| `--link pyocd` / `--link cube` | the ST-Link through pyOCD (default) or STM32_Programmer_CLI |
| `--elf build.elf` | for firmware older than the descriptor: the build on the board, whose symbols give the addresses (its version string is compared with the board's flash first) |
| `--check-elf build.elf` | offline: check a build's descriptor against its symbols (CI) |
| `--version` | the tools' version |

## Settings

| Name | Default | Allowed |
|---|---|---|
| `callsign` | N0CALL | 3-6 of A-Z, 0-9, with at least one letter and one digit; no SSID. Lowercase is converted. N0CALL means "not set up" |
| `ssid` | 12 | 0-15 |
| `symbol` | `/O` (balloon) | table `/`, `\` or an overlay A-Z, then a printable code, e.g. `/>` car |
| `path` | WIDE1-1 | WIDE1-1, WIDE2-1, WIDE2-2 |
| `mode` | tracker (fox with `-DFOX`) | tracker, fox |
| `start_paused` | no | yes: nothing is sent until a long button press |
| `freq` | 446.175 MHz | 420-450 MHz, given in whole Hz (`446175000`) or with a unit (`446.175MHz`, `432500kHz`); a bare `446.175` is refused |
| `power` | 32 (0x20) | Si4063 PA level 0-127 |
| `deviation` | 2600 Hz | 1000-5000 Hz |
| `cw_wpm` | 15 | 5-40 |
| `tracker_period_s` | 300 | >= 30 |
| `tracker_fix_timeout_s` | 120 | >= 10 and < period |
| `tracker_settle_s` | 10 | < fix timeout |
| `fox_period_s` | 30 | >= 10 |
| `fox_tone_ms` | 10000 | <= 60000, and tone + 8 s must fit in the period |
| `fox_aprs_every` | 10 | APRS after every Nth fox cycle; 0 = never (for an actual hunt: APRS gives the fox's position away) |
| `gps_power` | default (duty_cycle) | `duty_cycle`: the GPS goes into backup mode between tracker fixes. `always_on`: never; continuous 1PPS for the frequency correction and faster fixes, for about 20-25 mA more (on the stock CR123A cells that shortens the run time noticeably, #15). `default`: duty_cycle for now; #36 may make it switch on USB power |

## Protection against bad values

A typo or an out-of-range value can't take the board out of band, keep it
transmitting, or stop it booting:

1. **`dfmcfg.py`** checks every value against these rules before writing
   anything, and names the field and the allowed range.
2. **The firmware checks again when applying** (`settings_check()` in
   `settings.c`): the CRC, each field, and timing that fits together. Invalid
   settings are refused, and the old ones stay in use.
3. **At boot** an erased, corrupt or unreadable EEPROM block is ignored and
   the defaults are used: the board boots, doesn't transmit, and can be fixed
   over SWD.
4. **At run time** the settings in use are checked on every main loop pass.
   If anything changed them outside the apply path (e.g. a stray SWD write),
   the last good copy is restored (`settings_repairs` counts it).
5. **The transmit gate** in `si4060_start_tx()`, which every transmission
   passes through, checks the settings and the callsign before each key-up
   (`si4060_tx_refused` counts refusals).

Tested on board 1 (2026-10-29):
- a fresh boot refused to transmit;
- setting NZ0P-12 and saving brought it back on the air;
- a 460 MHz value written past the tool into the edit buffer was refused by
  the firmware;
- 460 MHz poked straight into the live settings was put back on the next
  loop pass;
- a reset reloaded the saved settings.

## Layout

64 bytes at user-memory address `0x180` (sector 3, the free sector: no Graw
data is overwritten, and it's open for phone access since #8). Little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | magic `DF` |
| 2 | 1 | layout version (1) |
| 3 | 1 | length (64) |
| 4 | 6 | callsign, NUL-padded ASCII |
| 10 | 1 | SSID |
| 11 | 1 | symbol table |
| 12 | 1 | symbol code |
| 13 | 1 | path, `0xnN` for WIDEn-N |
| 14 | 1 | mode (0 tracker, 1 fox) |
| 15 | 1 | flags (bit 0: start paused) |
| 16 | 4 | frequency, Hz |
| 20 | 1 | PA level |
| 21 | 1 | CW speed, WPM |
| 22 | 2 | deviation, Hz |
| 24 | 2 | tracker period, s |
| 26 | 2 | tracker fix timeout, s |
| 28 | 2 | tracker settle time, s |
| 30 | 2 | fox period, s |
| 32 | 2 | fox tone, ms |
| 34 | 1 | APRS every Nth fox cycle |
| 35 | 1 | GPS power: 0 duty cycle, 1 always on, 0xFF default |
| 36 | 26 | reserved (0xFF) |
| 62 | 2 | CRC-16/CCITT-FALSE of bytes 0-61 (Python: `binascii.crc_hqx(data, 0xFFFF)`) |

`settings_t` in `dfm17/Core/Inc/settings.h` pins these offsets with static
asserts. `make test` checks that `dfmcfg.py` uses the same ones.

## Not yet (issue #18)

- The telemetry sequence counter and the learned 1PPS correction kept in the
  EEPROM (board block).
- The board serial (EEPROM 0x088) in the APRS comment.
- Reloading after a phone writes the settings over NFC (PC13).
- Programs: several fox/test recipes and a scheduler.
