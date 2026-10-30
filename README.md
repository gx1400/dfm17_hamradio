# dfm17_hamradio

[![CI](https://github.com/gx1400/dfm17_hamradio/actions/workflows/ci.yml/badge.svg)](https://github.com/gx1400/dfm17_hamradio/actions/workflows/ci.yml)

This repo is a development implementation of code to run on a Graw DFM-17 radiosonde

Current firmware (`TRACKER` in main.c): an APRS tracker on 446.175 MHz. Every
5 min it wakes the GPS, waits for a 3D fix that has held for 10 s (up to 2 min),
sends one packet as NZ0P-12 and puts the GPS back into backup. The packet is a
position with supply voltage, current and temperature telemetry, or a status
packet (`>no GPS fix 4668mV 54mA 31C`) without a fix. The GPS stays on until
its first fix. Without `TRACKER` it is a 70 cm fox (Morse ID, hunt tone every
30 s, APRS every 5 min). Both have button and LED diagnostics. Tested on
hardware with a HackRF and Dire Wolf.

## Back up your EEPROM first

Every DFM-17 carries Graw factory data in its NFC EEPROM (M24LR04E-R, U12):
the board serial, configuration bytes and what look like sensor calibration
coefficients. Future firmware will reuse most of that space for settings, so
**take a full backup of each board before flashing anything else**, and
please share it (commit it to `docs/data/` or attach it to an issue). The
calibration data may turn out to be useful later, and backups from several
boards help decode it.

```sh
make firmware
tools/hw_survey/eeprom_backup.sh docs/data "board 2, PCB label ..."
```

This needs an ST-Link on the SWD pads. The script:
- flashes the `nfc_tool` build, which replaces the board's firmware and never
  transmits;
- reads the EEPROM twice and checks the two reads match;
- writes `docs/data/eeprom-M24LR04E-<UID>-<date>.txt` (annotated hex dump of
  the user memory and system area) and a `.bin` of the 512-byte user memory.

The original Graw firmware in the STM32 is not backed up by this. Board 1's
backup is in `docs/data/`.

## Building and testing

- STM32CubeIDE: open the `dfm17/` project as before.
- Command line: `make firmware` builds every variant into `build/<variant>/`
  (`tracker`, `fox`, `hw_survey`, `rf_test`, `nfc_tool`) with warnings as errors, using
  `arm-none-eabi-gcc` from PATH or the one inside STM32CubeIDE.
- `make test` runs the host unit tests (`tools/tests`) and checks that the
  APRS packets decode with Dire Wolf (`tools/aprs_sim`, Python packages in
  `tools/requirements.txt`).
- `make firmware` compiles our sources with `-Wextra -Wshadow
  -Wdouble-promotion` as errors. The last one flags float maths silently
  promoted to double, which drags several KB of software double routines into
  the image; the vendor HAL is built with `-Wall` only.
- `make size` reports flash and RAM per variant (`tools/size/size_report.py`)
  and fails if a variant is over budget or links the software double-precision
  routines. The budgets, set at the top of the script: flash 56 KB of 64 KB
  (room for the EEPROM settings), RAM 7 KB of 8 KB including the linker's
  1.5 KB heap and stack reserve. `make size SIZE_BASE=<dir>` compares with
  another build. In CI the firmware job compares each PR with its base and
  shows the table, plus the symbols that changed most, in the job summary.
- Lint and format checks (CI job `lint`; tools pinned in
  `tools/requirements-lint.txt`, `pip install -r tools/requirements-lint.txt`,
  plus `cppcheck` from your distribution):
  - `tools/lint/check_eol.sh`: the STM32CubeMX-generated files stay CRLF,
    everything else is LF (`.editorconfig` sets editors up to match).
  - `tools/lint/cppcheck.sh`: static analysis of `dfm17/Core`; justified
    suppressions in `tools/lint/cppcheck-suppressions.txt`.
  - `tools/lint/format.sh check` / `fix`: `.clang-format` applied to the C
    lines you changed (vs `origin/main`), never to generated or vendor files.
    Stage your changes before `fix`.
  - `ruff check .` and `ruff format --check .` for the Python tools.
  - `git ls-files -z '*.sh' | xargs -0 shellcheck -x -P SCRIPTDIR -P .` and
    `actionlint`.
- Pull requests get a checklist (`.github/pull_request_template.md`): size,
  tests, what ran on hardware, RF and station-ID changes, CubeMX, EEPROM
  backups. Hardware test results go in a "Hardware test report" issue
  (`.github/ISSUE_TEMPLATE/hardware-test.yml`).
- GitHub Actions (`.github/workflows/ci.yml`) runs the build, the tests and
  the lint checks on every pull request and push to `main`, and keeps the
  built firmware as an artifact. Dependabot
  (`.github/dependabot.yml`) proposes updates to the pinned actions and Python
  packages weekly.

## Documentation

- [docs/hardware-investigation.md](docs/hardware-investigation.md): analog inputs
  (supply voltage and current), NFC EEPROM contents and settings proposal,
  low-power measurements and battery-life estimates
- [docs/tests/nfc-sector-lock.md](docs/tests/nfc-sector-lock.md): test procedure
  for opening the NFC EEPROM's sector 3 (issue #8), with board 1 results
- [docs/rf-measurements.md](docs/rf-measurements.md): frequency accuracy across
  420-450 MHz, warm-up drift, TCXO vs GPS, and why the board can't receive
- [tools/aprs_sim/README.md](tools/aprs_sim/README.md): host simulator for the
  APRS encoder, decodes with Dire Wolf
- `tools/hw_survey/`: read the hardware survey and live analog values over SWD
- `tools/rf_sweep/`: transmitter frequency sweep with a HackRF
- [tools/tests/README.md](tools/tests/README.md): host unit tests

## Using the board

- Button, acting on release: under 1 s = 5 s unmodulated carrier on the
  transmit frequency; 1-3 s = 5 s 1200 Hz tone then one APRS packet; over
  3 s = pause / resume the fox or tracker
- Green: on while transmitting, short heartbeat flash when idle (double flash
  while paused). Yellow: flashes with GPS PPS, solid with a 3D fix. Red: blink
  code, 1 = radio not found, 2 = not running from the radio clock,
  3 = GPS not answering
- Settings are `#define`s for now: mode (`TRACKER`, or build with `-DFOX`) in `main.c`, tracker
  timing in `tracker.h`, frequency and power in `si4063.h`, fox timing, callsign, `FOX_SEND_APRS` / `FOX_APRS_EVERY` / `FOX_START_PAUSED` in
  `fox.h`

## TODO

- [ ] Comment existing code
- [X] Decide on functionality of LEDs
- [X] Check crc in ubx payloads
- [X] Decide on gps tick time, currently at once every 5 sec
- [X] Implement GPS lock status based on 1 PPS input (don't update GPS if no tick?)
- [X] Test tone output on 2GFSK
- [X] Implement APRS tick timer (26.4Khz to generate 1200 and 2200)
- [ ] Implement RTTY tick timer (100Hz to generate 50Hz, include 75Hz?)
- [X] Implement CRC generation for APRS packets
- [X] Implement morse code (MCW on FM rather than OOK)
- [X] Implement APRS with fixed packet data
- [X] Integrate GPS with APRS data
- [ ] Implement RTTY data
- [ ] Power-up check if running from battery or usb (PC2 read and reported,
  divider to measure)
- [X] Decide on use case for button
- [X] Read ADC samples (battery voltage, usb voltage, current)
- [ ] Calibrate ADC (bench supply sweep, both boards)
- [ ] APRS telemetry PARM/UNIT/EQNS messages
- [ ] Settings in EEPROM sector 3 (callsign, mode, frequency, calibration, last fix)
- [ ] Duty-cycled low-power mode (RTC wake, radio shutdown, GPS backup)


## Unknown capability

- [ ] Temperature/humidity measurement ("Tmux") operation; the EEPROM holds
  what look like its calibration coefficients
- [X] How or if to use NFC based memory: 160 bytes free, writable over I2C
  (see docs); NFC (phone) access still to test
- [ ] What draws the ~14 mA left with the MCU, radio and GPS idle

## Long term ideas

- [ ] Decide if peripherals should be shut off to conserve power
  - [ ] when no gps lock, should UART2 and DMA be disabled?
  - [ ] when radio is shut down, should SPI be disabled?
  - [ ] stop timers when applicable functions are not in use (delay_us timer, baud timers, etc)
