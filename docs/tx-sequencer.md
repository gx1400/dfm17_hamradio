# RTTY and the transmit sequencer: plan (#53, #56)

Planning notes. Nothing here is built yet. The **Decided** items were agreed
when the issues were opened. Everything under **Proposed** and **To discuss**
is a starting point for when the work starts, not a decision.

## Decided

1. **RTTY transmit (#53)** has three message types:
   - a **fixed** stored text message;
   - a **GPS** message built from the current fix;
   - **mixed**: a GPS message every Xth transmission, the fixed message on
     the others.
2. **RTTY can be a standalone mode, or a step that alternates with the
   others (APRS, CW, tone) in a sequencer.** It is built as functions the
   sequencer calls (e.g. `rtty_send_gps()`, `rtty_send_msg(i)`). The
   standalone mode is a default sequence or a thin wrapper over them, so it
   isn't a second code path.
3. **The sequence is defined in the EEPROM (#56)**, so a board's transmit
   pattern changes with the settings tools, not with a new firmware build.
4. **Plan before building.** The format, the EEPROM space and the flash cost
   are agreed before any code, per the flash budget rule. Each PR reports the
   size change for tracker and fox.
5. **The tools stay in step.** Any new EEPROM block goes into `dfmcfg.py`,
   the app, `docs/settings.md` and the host tests in the same PR as the
   firmware ([config-gui.md](config-gui.md#keeping-the-app-in-step-with-the-firmware)).

## Proposed

### RTTY

- **GPS message:** in the UKHAS sentence format, so existing decoders and
  trackers parse it:
  `$$NZ0P,<seq>,<hh:mm:ss>,<lat>,<lon>,<alt>,<sats>,<temp>,<vin>*<CRC16>`.
  The CRC is CRC-16/CCITT (0x1021, init 0xFFFF) over the text between `$$`
  and `*`.
- **Defaults:** 50 baud, 425 Hz shift, ASCII, no parity, 2 stop bits. All
  of these are settings.
- **Modulation:** Si4063 2-FSK, mark/space set by `si4060_set_offset_fast`
  from a bit clock on TIM16 (one interrupt per bit). About 1 s of mark before
  the text so decoders lock.
- **Settings:** `rtty_gps_every` (X), baud, shift, bits, stop bits.
- **Receiving it:** an SDR in USB mode (RSPdx with SDRuno, or SDR++ with
  the RSPdx or HackRF) feeding fldigi, set to RTTY Custom with the same baud,
  shift and bits.

### Sequencer

A block with its own header and CRC, like the settings block. One cycle is
the list of steps, repeated every period.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | magic `SQ` |
| 2 | 1 | layout version |
| 3 | 1 | length |
| 4 | 2 | cycle period, s |
| 6 | 1 | step count |
| 7 | 4 × n | steps: action, every N / offset (a nibble each), u16 parameter |
| … | 2 | CRC-16/CCITT-FALSE |

A step runs on cycles where `cycle % N == offset`. That one rule covers
"every Xth" and "in between" without branches or loops.

| Action | Parameter |
|---|---|
| `WAIT_FIX` | timeout s (the settle time comes from the settings) |
| `APRS_POS` | (position and telemetry, as now) |
| `CW_ID` | (the callsign at `cw_wpm`) |
| `TONE` | ms |
| `CARRIER` | ms |
| `RTTY_GPS` | |
| `RTTY_MSG` | message index |
| `PAUSE` | ms |
| `GPS_SLEEP` | (GPS backup until the next cycle) |

Examples:

| Use | Steps |
|---|---|
| Tracker (today) | `WAIT_FIX 120`, `APRS_POS`, `GPS_SLEEP`; period 300 s |
| Fox (today) | `CW_ID`, `TONE 10000`, `APRS_POS` every 3; period 60 s |
| RTTY mixed | `RTTY_GPS` every X offset 0, `RTTY_MSG 0` every X offsets 1 to X−1 (one step per offset, or a "not on" flag: to discuss) |
| Combined | `WAIT_FIX`, `APRS_POS`, `RTTY_GPS`, `CW_ID` every 10 |

With no valid sequence block, the board runs the mode in the settings, as
now.

### EEPROM space

The M24LR04E has 512 B. The free space is:

| Range | Size | |
|---|---|---|
| 0x000–0x15F | 352 B | Graw factory data: don't touch |
| 0x160–0x17F | 32 B | free; sector 2 is NFC-locked by Graw, which doesn't matter over I2C |
| 0x180–0x1BF | 64 B | settings block |
| 0x1C0–0x1FF | 64 B | free; sector 3 is open to NFC since #8 |

About 96 B is free, shared by the sequence, the RTTY fixed message (#53) and
the calibration factors (#43). Two ways to split it:

- **A:** the sequence at 0x1C0, up to 13 steps, or 8 steps and a ~20 B
  message; calibration in 0x160.
- **B:** the sequence at 0x160, up to 5 steps; the message and calibration
  at 0x1C0.

## To discuss when working the issues

RTTY (#53):

- 7-bit (UKHAS norm) or 8-bit characters?
- How long is the fixed message, and where is it stored (see EEPROM space)?
- Frequency: RTTY is normally sent on USB in 432–435 MHz, not on the FM
  APRS frequency. Should it have a separate `rtty_freq_hz`, or reuse
  `freq_hz`?
- With no fix: send the last fix with sats = 0, or a no-fix line?
- Keep a CW ID when RTTY runs on its own?

Sequencer (#56):

- **Replace or add:** do the tracker and fox become built-in default
  sequences (one code path, perhaps less flash), or stay as they are with
  `MODE_SEQUENCE` added (less risk, more flash)?
- **Period:** measured from the start of one cycle to the start of the next
  (as the fox does), or from the end of a cycle? What if the steps take
  longer than the period?
- **Per-step radio settings:** frequency, PA level and deviation, e.g. RTTY
  away from the APRS frequency. Bigger steps, or a small "radio profile"
  table?
- **Steps that need a fix and have none:** skip, or use the last fix, per
  action?
- **The EEPROM split:** A or B above, shared with #43.
- **Buttons:** pause and resume as now; does a long press restart at step 0?
- **The app:** what the sequence editor looks like.
