[![Build Status](https://app.travis-ci.com/trickv/dfm17_hamradio.svg?branch=main)](https://app.travis-ci.com/gx1400/dfm17_hamradio)
# dfm17_hamradio

This repo is a development implementation of code to run on a Graw DFM-17 radiosonde

## TODO

- [ ] Comment existing code
- [X] Decide on functionality of LEDs
- [X] Check crc in ubx payloads
- [ ] Decide on gps tick time, currently at once every 5 sec
- [X] Implement GPS lock status based on 1 PPS input (don't update GPS if no tick?)
- [X] Test tone output on 2GFSK
- [X] Implement APRS tick timer (26.4Khz to generate 1200 and 2200)
- [ ] Implement RTTY tick timer (100Hz to generate 50Hz, include 75Hz?)
- [ ] Implement CRC generation for APRS packets
- [ ] Implement morse code OOKing
- [ ] Implement APRS with fixed packet data
- [ ] Integrate GPS with APRS data
- [ ] Implement RTTY data
- [ ] Power-up check if running from battery or usb
- [ ] Decide on use case for button
- [ ] Read ADC samples (battery voltage, usb voltage, current)
- [ ] Calibrate ADC


## Unknown capability

- [ ] Temperature/humidity measurement ("Tmux") operation
- [ ] How or if to use NFC based memory

## Long term ideas

- [ ] Decide if peripherals should be shut off to conserve power
  - [ ] when no gps lock, should UART2 and DMA be disabled?
  - [ ] when radio is shut down, should SPI be disabled?
  - [ ] stop timers when applicable functions are not in use (delay_us timer, baud timers, etc)
