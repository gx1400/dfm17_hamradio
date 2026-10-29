# dfm17_hamradio

This repo is a development implementation of code to run on a Graw DFM-17 radiosonde

Current firmware: a 70 cm fox transmitter on 432.500 MHz (Morse ID, hunt tone,
APRS position with telemetry) with button and LED diagnostics. Tested on
hardware with a HackRF and Dire Wolf.

## Documentation

- [docs/hardware-investigation.md](docs/hardware-investigation.md): analog inputs
  (supply voltage and current), NFC EEPROM contents and settings proposal,
  low-power measurements and battery-life estimates
- [tools/aprs_sim/README.md](tools/aprs_sim/README.md): host simulator for the
  APRS encoder, decodes with Dire Wolf
- `tools/hw_survey/`: read the hardware survey and live analog values over SWD

## Using the board

- Button, acting on release: under 1 s = 5 s unmodulated carrier on the
  transmit frequency; 1-3 s = 5 s 1200 Hz tone then one APRS packet; over
  3 s = pause / resume the fox
- Green: on while transmitting, short heartbeat flash when idle (double flash
  while paused). Yellow: flashes with GPS PPS, solid with a 3D fix. Red: blink
  code, 1 = radio not found, 2 = not running from the radio clock,
  3 = GPS not answering
- Settings are `#define`s for now: frequency and power in `si4063.h`, fox
  timing, callsign and `FOX_SEND_APRS` / `FOX_START_PAUSED` in `fox.h`

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
