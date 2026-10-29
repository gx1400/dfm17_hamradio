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

## Building and testing

- STM32CubeIDE: open the `dfm17/` project as before.
- Command line: `make firmware` builds every variant into `build/<variant>/`
  (`tracker`, `fox`, `hw_survey`, `rf_test`) with warnings as errors, using
  `arm-none-eabi-gcc` from PATH or the one inside STM32CubeIDE.
- `make test` runs the host unit tests (`tools/tests`) and checks that the
  APRS packets decode with Dire Wolf (`tools/aprs_sim`).
- GitHub Actions (`.github/workflows/ci.yml`) does both on every pull request
  and push to `main`, and keeps the built firmware as an artifact.

## Documentation

- [docs/hardware-investigation.md](docs/hardware-investigation.md): analog inputs
  (supply voltage and current), NFC EEPROM contents and settings proposal,
  low-power measurements and battery-life estimates
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
- [ ] Power-up check if running from battery or usb
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
