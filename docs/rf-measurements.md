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

- **GPS frequency correction**: implemented, see below.
- **Output filter response** is unknown (the stage was designed for
  400-406 MHz). The levels in these captures are leakage past a terminator
  and say nothing reliable about it. To measure: DFM-17 output -> 30-40 dB of
  attenuation -> HackRF (keep the HackRF input well below its maximum),
  sweep PA 0x00 across the band and compare levels. Only then use an antenna
  with a known-good match, at low power first.
- Keep `RF_FREQ_HZ_DFM` within 420-450 MHz (compile-time check in si4063.c).

## 1PPS frequency discipline (`Core/Src/ppsdisc.c`)

The carrier and the CPU clock come from the same TCXO, so the CPU cycles
between GPS PPS pulses measure the carrier's error too. The firmware programs
`f * (1 - error)` so the carrier lands on `f`.

- **Only real GPS time (#35):** after its first lock, the u-blox keeps the
  PPS running from its own crystal when the satellites are gone (UBX-CFG-TP5
  defaults; on the 10-29 overnight run, pulses came all night with `0sv`).
  `diag_pps()` therefore passes a pulse on only while `gpsTimeLocked()`:
  - the last NAV-PVT had a fix (2D, 3D, with dead reckoning, or time only);
  - its time was valid and fully resolved;
  - its time accuracy was 500 ns or better;
  - it is at most 20 s old.

  Other pulses are counted in `pps_unlocked`, and the estimate is kept. The
  CFG-TP5 `syncMode` option, which would stop the pulse, only applies to
  u-blox's FTS timing variant, not the MAX-M8C.
- **Samples:** TIM4 latches its 24 MHz count on the PPS edge (input capture
  on PB8, `ppscap.c`, #12), so the interval is exact however late the PPS
  interrupt runs: during transmissions it can be held back by up to about
  11 µs, which made the earlier DWT count read in the interrupt wrong by up
  to 1.3 ppm under heavy TX. `diag_pps()` passes the captured interval (or
  the DWT count, `pps_source = 0` over SWD) and the HAL tick interval to
  `pps_disc_sample()`. A sample is rejected unless:
  - the interval is 1 s +-50 ms by the tick (this drops the first pulse after
    a GPS wake-up and missed pulses);
  - it is within +-10 ppm of 24 000 000;
  - SYSCLK runs from the TCXO (not on the HSI fallback).
- **Estimate:** a trimmed mean of the last 16 good samples, dropping the 4
  highest and 4 lowest, which resolves better than one cycle (41.7 ppb). It
  is kept while the GPS sleeps.
- **Applying it:** `pps_disc_update()` in the main loop, which only runs
  between transmissions (the PLL can't be retuned while transmitting),
  retunes when the estimate has moved by 10 ppb or more. 10 ppb is 4.5 Hz;
  the synthesizer step is 12.2 Hz.
- **Not corrected:** the `RF_TEST_SWD` build, so sweeps measure the raw TCXO.
- **Over SWD:**

  | Variable | Meaning |
  |---|---|
  | `pps_disc_enable` | 0 = nominal frequency |
  | `pps_disc_valid` | a full window of samples |
  | `pps_disc_ppb` | TCXO error, + = fast |
  | `pps_disc_applied_ppb` | correction in the synthesizer |
  | `pps_disc_samples`, `pps_disc_rejected` | sample counts |
  | `gpsTimeOk`, `pps_unlocked` | the last NAV-PVT said GPS time is locked; pulses kept out without it |

- **Not corrected during a transmission:** the TCXO's warm-up drift on a
  long carrier (-0.15 ppm over 100 s) waits for the next pause. Sustained
  transmitting heats it more: -0.92 to -1.60 ppm over 7 min of back-to-back
  packets, back to -1.21 ppm 2 min after (#12). That matters for high-duty fox
  modes.
- **Hardware test:** issue #9; results below.

### Results, board 1 (#9, #12)

The correction moves the carrier by what the board estimates. Two different
receivers agree on that, each within its own reference error:

| Date, receiver | Correction off | Correction on | Shift | Board's estimate |
|---|---|---|---|---|
| 2026-10-29, HackRF (CW carrier, `pps_disc_enable` 0/1) | -561 Hz (-1.26 ppm) | -108 Hz (-0.24 ppm) | +453 Hz (1.02 ppm) | -1073 ppb (1.07 ppm) |
| 2026-10-31, RSPdx (APRS bursts; "off" = just after a reflash, before the first estimate) | -251 Hz (sd 33, 4 bursts) | +146 Hz (sd 3, 18 bursts) | +397 Hz (0.89 ppm) | -917 ppb (0.92 ppm) |

- **Shift vs estimate:** agree within 50 ppb (HackRF) and 30 ppb (RSPdx).
- **What's left over is each receiver's own clock:** the corrected carrier reads -0.24 ppm on the HackRF and +0.33 ppm on the RSPdx. Opposite signs, so the corrected carrier lies between the two. Checking the ±0.1 ppm target absolutely needs a GPS-locked reference.
- **Overnight soak** (2026-10-31, 8 h, tracker sending every 5 min, GPS always on, TIM4 capture):
  - 28,871 pulses: 28,867 accepted, **0 rejected**, 0 missing captures, 4 kept out without GPS time lock;
  - estimate valid in every read, -917 to -1083 ppb, moving slowly with the room (hourly means -1034 … -1072, then -919 once the board had cooled by 1 °C);
  - all 96 packets on time.
- **APRS still decodes with the correction:** 16/16 telemetry checks matched the board's own values, Dire Wolf decoded 17/17 square-wave packets, and the D72A decoded them too on 10-29 (#10).
- **Not done yet:** board 2; the HSI-fallback check (no samples on the internal oscillator).

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
| `Core/Src/ppsdisc.c` | carrier correction from the GPS PPS (above) |
| `Core/Src/rf_test.c` | carrier on a frequency/power written over SWD (`RF_TEST_SWD` in main.c) |
| `tools/rf_sweep/sweep.sh` | sweep with a HackRF: `sweep.sh dfm17.elf [power] [freq_hz ...]` |
| `tools/rf_sweep/measure.py` | carrier frequency from on/off captures, spur rejection |
