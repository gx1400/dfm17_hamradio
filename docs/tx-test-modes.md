# Transmit test modes: CW, CW ID, RTTY

The `rf_test` build (`RF_TEST_SWD`) sends three test transmissions on demand over SWD, besides its test carrier:

| Mode | Sends | Received as |
|---|---|---|
| `cw` | the text in Morse, then `DE <callsign>` | MCW: the 825 Hz tone keyed on the FM carrier, as the fox (NFM) |
| `cwid` | the fox's Morse ID: the callsign alone | the same |
| `rtty` | CR LF, the text, `DE <callsign>`, CR LF | FSK (USB) |

- **Always identifies:** the board appends the callsign from the settings to every message.
- **Transmit gate:** applies as for every key-up (a callsign must be set, the settings valid).
- **Frequency:** the settings' frequency, without the 1PPS correction (the `rf_test` build never applies it).
- **Power:** `--power`, PA_PWR_LVL, default 0x00, the lowest.

The code is meant for the transmit sequencer (#56) and RTTY mode (#53) to call:
- `cw_send()` (`cw.c`, now shared with the fox);
- `rtty_send()` (`rtty.c`, `rtty_hw.c`).

## Running a test

Flash `build/rf_test/dfm17.hex` with the board on USB power, then:

```sh
tools/rf_sweep/tx_test.py cwid --power 0x20
tools/rf_sweep/tx_test.py cw --text "VVV CQ TEST" --wpm 20 --power 0x20
tools/rf_sweep/tx_test.py rtty --power 0x20                                  # ITA2 45.45 baud, 170 Hz, 1.5 stop
tools/rf_sweep/tx_test.py rtty --baud 50 --shift 425 --bits 7 --stop 2 --power 0x20 --text '$$NZ0P,1,...'
```

- **How it works:** the script writes `rf_test_power` and the 64-byte `rf_test_tx` block (RTTY parameters, CW speed, text, then the command word last) through STM32_Programmer_CLI in hot-plug mode. It then waits for `rf_test_tx_done` to count up and prints `rf_test_tx_result`.
- **Addresses:** read from the ELF with `arm-none-eabi-nm`. Pass `--elf` if the build isn't `build/rf_test/dfm17.elf`.
- **Text:** up to 47 characters.
- **CW speed:** 5–40 WPM; 0 uses the settings' `cw_wpm`.
- **RTTY parameters** (checked by `rtty_params_ok()`): 10–300 baud, 50–1000 Hz shift, 5 (ITA2), 7 or 8 (ASCII) data bits, 1, 1.5 or 2 stop bits.
- **Not while the test carrier is on:** refused (`rf_test_freq_hz` must be 0).

## How RTTY is sent

- **Modulation:** the radio already runs async direct 2FSK from GPIO3, where GPIO3 high and low are the carrier ± the deviation.
  - `rtty_hw_begin()` sets the deviation to half the shift and stops the tone and AFSK tick, so nothing else drives GPIO3.
  - Holding GPIO3 for a bit time sends that bit.
  - Mark is the higher frequency (GPIO3 high, `RTTY_MARK_GPIO3`), checked on air. `--reverse` swaps it.
- **Bit timing:** counted in SYSCLK cycles (DWT CYCCNT, 24 MHz from the TCXO). Each edge is measured from the key-up, not from the previous edge, so the baud rate doesn't drift. The main loop is busy for the whole message, as for the fox's Morse.
- **Framing:**
  - 1 s of mark first, so a decoder can find the tones.
  - ITA2 then sends LTRS, and LTRS/FIGS where the shift changes. After a space in figures the next figure sends FIGS again, because receivers usually unshift on space.
  - ASCII sends 7 or 8 bits, LSB first.
  - 0.3 s of mark at the end.
- **ITA2 characters:** A–Z, 0–9, space, CR, LF and `- , : ( ) ? . /`, the figures ITA2 and US TTY share. Anything else is skipped, and lower case is sent as upper case.
- **Host tests:** `test_rtty.c` decodes the generated bit stream back to text, as a receiver would. `test_cw.c` does the same for the Morse timeline.

## Results, board 1 (2026-11-01)

- **Receiver:** RSPdx and SDRuno, about a metre from the board on a dummy load. NFM for CW, USB 2 kHz below the channel for RTTY.
- **Audio:** recorded from VB-Cable. fldigi decoded live from the same audio.

| Test | Measured | Decoded |
|---|---|---|
| `cw` 15 WPM | 14.5 WPM, dash/dot 2.94 | `VVV CQ TEST 123 /?.,= DE NZ0P` (envelope decoder); fldigi `…Q TEST 123 /?.,<BT> DE NZ0P` once it had locked on the speed |
| `cwid` | | `NZ0P` (keying checked element by element) |
| `rtty` ITA2 45.45/170/1.5 | 45.452 baud, mark high, 0 bad stop bits, shift 147.0 Hz | `RYRYRY THE QUICK BROWN FOX 0123456789 DE NZ0P` |
| `rtty` ASCII 7N2 50/425 | 50.003 baud, 0 bad stop bits, shift 415.8 Hz | `$$NZ0P,1,12:34:56,39.0997,-94.5786,250*ABCD DE NZ0P` |
| `rtty` ASCII 8N2 50/425 | 50.002 baud, 0 bad stop bits, shift 415.6 Hz | `$$NZ0P,2,12:35:10,39.0997,-94.5786,251*1A2B DE NZ0P` |

- **Power:** at PA 0x00 the RSPdx didn't hear the board at this distance. All results are at 0x20, the tracker's level.
- **Decoders:** the RTTY rows are from a script that tracks the drift (below).
- **fldigi on RTTY:** with its RTTY-45 settings, it decoded a synthetic signal perfectly. It didn't decode the board live. It did decode the board's recording once the drift was taken out.
- **Shift:** measured where the tones had settled, in the middle of runs of two or more equal bits (sd about 1 Hz).
  - **The cause:** the radio's deviation step is 12.207 Hz (25.6 MHz TCXO, output divider 8), and `si4060_set_deviation()` rounds down. So the first build sent 170 Hz as 6 steps (146.5 Hz, measured 147.0) and 425 Hz as 17 steps (415.0 Hz, measured 415.8).
  - **The fix:** `rtty_deviation_hz()` now asks for the nearest step: 7 steps for 170 Hz (170.9 Hz). 425 Hz stays at 17 steps, the nearest the radio can make (18 would be 439.5 Hz).
  - **Checks:** `test_rtty.c` applies the radio's own rounding to every shift from 50 to 1000 Hz. The fixed build's shift on air is in the PR.
  - **APRS isn't changed:** its 2600 Hz deviation is 212 steps either way (2588 Hz).

### The TCXO drifts while transmitting

An unmodulated carrier at PA 0x20, tracked in USB audio:

| Time after key-up | 0.1 s | 1 s | 2 s | 4 s | 10 s | 20 s | 30 s | 58 s |
|---|---|---|---|---|---|---|---|---|
| Offset from the start | 0 | −14 Hz | −22 Hz | −33 Hz | −59 Hz | −100 Hz | −125 Hz | −170 Hz (−0.38 ppm) |

It starts at about 14 Hz/s and is still falling at about 1 Hz/s after a minute. That's much more than the −0.15 ppm over 100 s measured at PA 0x00 (rf-measurements.md): the PA's heat reaches the TCXO.

- **Over FM** (APRS, CW, the fox) it doesn't matter.
- **For RTTY** it does. A 10 s ITA2 message drifts about 40 Hz, a quarter of the 170 Hz shift, and fldigi's narrow RTTY-45 filters lose lock. 425 Hz shift and decoders with wide AFC cope better.
- **Options for #53** are in #65.

## Files

| Path | |
|---|---|
| `dfm17/Core/Src/cw.c` | Morse table and keyed tone, shared by the fox and the tests |
| `dfm17/Core/Src/rtty.c` | ITA2/ASCII encoding and framing (host-tested) |
| `dfm17/Core/Src/rtty_hw.c` | GPIO3 levels and the cycle-counter bit timing |
| `dfm17/Core/Src/rf_test.c` | the SWD test block and the three modes |
| `tools/rf_sweep/tx_test.py` | runs a test over SWD |
