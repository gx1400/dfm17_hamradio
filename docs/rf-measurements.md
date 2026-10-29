# DFM-17 transmitter: frequency accuracy and stability

Board 1, transmitting into a small 50 ohm terminator, received by a HackRF Pro
picking up leakage. 2026-10-29. Unmodulated carrier at PA_PWR_LVL 0x00
(lowest), set over SWD with the `RF_TEST_SWD` build (`Core/Src/rf_test.c`) and
measured with `tools/rf_sweep/sweep.sh`.

## Summary

- **The synthesizer is exact across 420-450 MHz.** Every frequency lands at
  the same **-1.47 +/- 0.02 ppm** from the requested value: one common reference
  offset, no PLL or fractional-divider error.
- **That offset is the DFM-17's TCXO, not the HackRF.** Counting CPU cycles
  (24 MHz from the TCXO) between GPS PPS pulses gives **-1.25 to -1.42 ppm**;
  the HackRF agrees to within about 0.15 ppm. At 446.175 MHz that is about
  **-620 Hz**, harmless for FM.
- **Warm-up drift** after keying up: about **-0.15 ppm over the first 100 s**,
  levelling off (PA heat reaching the TCXO), then steady to ~0.01 ppm.
- The default frequency is now **446.175 MHz** (local APRS iGate KD0JWD-5);
  APRS decodes there.

## Sweep (PA 0x00, carrier vs requested, measured with the HackRF)

| Requested (MHz) | Offset (Hz) | ppm |
|---|---|---|
| 420.000 | -620 | -1.48 |
| 425.000 | -627 | -1.48 |
| 427.500 | -632 | -1.48 |
| 430.000 | -643 | -1.49 |
| 432.500 | -641 | -1.48 |
| 435.000 | -644 | -1.48 |
| 437.500 | -645 | -1.48 |
| 440.000 | -653 | -1.48 |
| 442.500 | -657 | -1.48 |
| 445.000 | -662 | -1.49 |
| 446.175 | -652 to -666 | -1.46 to -1.49 |
| 447.500 | -667 | -1.49 |
| 450.000 | -513 to -527 | -1.14 to -1.17 |

422.5 MHz was too weak to measure reliably in that run.

### Measurement pitfall: HackRF tuning artifacts

The first sweeps showed outliers (420.000 at -5 ppm, 437.5 at +4.3 ppm, 450.0
at +5.6 to +13.8 ppm, 430/440 at exactly -9 Hz). They are the HackRF, not the
DFM-17:

- at exact multiples of 10 MHz the HackRF shows strong spurs of its own;
- the remaining outliers move when only the HackRF's centre frequency is
  changed (420 MHz: -1.48 ppm from two centres, -4.90 ppm from a third), so
  they come from the HackRF's LO at some tuning settings.

`measure.py` therefore compares captures with the carrier on and off, searches
only +/-5 kHz around the target, and the table uses centres that agree.

## Stability and TCXO vs GPS (446.175 MHz, PA 0x00, continuous carrier)

| Time after key-up | Carrier vs HackRF | TCXO vs GPS (PPS cycle count) |
|---|---|---|
| 1 s | -1.42 ppm | -1.25 ppm (23 999 970 cycles) |
| 30 s | -1.50 ppm | -1.33 ppm |
| 60 s | -1.54 ppm | -1.38 ppm |
| 100 s | -1.56 ppm | -1.38 ppm (23 999 967 cycles) |

`pps_cycles` (diag.c) is the number of DWT cycles between the last two GPS PPS
pulses: 24 000 000 at exactly 24 MHz, one cycle = 0.042 ppm. It is updated
every second whenever the GPS has PPS.

## Recommendations

- **GPS frequency correction**: average `pps_cycles` for a few seconds with a
  fix, then program the synthesizer for `f / (1 + error)` (the fractional
  divider has far finer resolution than needed). Simpler than RS41ng's
  XO_TUNE perturb-and-observe loop and enough to put the carrier within
  ~0.1 ppm. Re-measure periodically for temperature.
- **Output filter response** is unknown (the stage was designed for
  400-406 MHz). The levels in these captures are leakage past a terminator
  and say nothing reliable about it. To measure: DFM-17 output -> 30-40 dB of
  attenuation -> HackRF (keep the HackRF input well below its maximum),
  sweep PA 0x00 across the band and compare levels. Only then use an antenna
  with a known-good match, at low power first.
- Keep `RF_FREQ_HZ_DFM` within 420-450 MHz (compile-time check in si4063.c).

## Receiving

The Si4063 is a **transmit-only** part (Si406x family; the Si446x parts are
the transceivers). It cannot receive, demodulate or stream I/Q, so the board
cannot take commands over the air as built. Options for remote commands:

- NFC: the M24LR04E EEPROM is writable from a phone (to be tested), so settings
  could be changed by tapping the sonde.
- The UART on the mini-USB connector (wired command interface).
- An external receiver (e.g. a small Si4463/CC1101 module or an SA818 FM
  module) on the XDATA pads (PB10/PB11), at the cost of more hardware.
  Swapping the Si4063 for a pin-compatible Si4463 would also need a receive
  path in the RF matching network that this board doesn't have.

## Tools

| Path | Purpose |
|---|---|
| `Core/Src/rf_test.c` | carrier on a frequency/power written over SWD (`RF_TEST_SWD` in main.c) |
| `tools/rf_sweep/sweep.sh` | sweep with a HackRF: `sweep.sh dfm17.elf [power] [freq_hz ...]` |
| `tools/rf_sweep/measure.py` | carrier frequency from on/off captures, spur rejection |
