# dfm17_hamradio

This repo is a development implementation of code to run on a Graw DFM-17 radiosonde

## TODO

- [ ] Comment existing code
- [ ] Check crc in ubx payloads
- [ ] Test tone output on 2GFSK
- [ ] Implement APRS tick timer (26.4Khz to generate 1200 and 2200)
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