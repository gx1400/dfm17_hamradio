# DFM-17 hardware investigation: analog inputs, EEPROM, low power

Measured on board 1 (EEPROM UID `E0 02 5A 67 5B 01 67 7B`), powered by a USB-A
charger into the mini-USB connector, on 2026-10-28/29. Firmware branch
`hw-investigation`. Hardware reference:
[RECESSIM DFM-17 wiki](https://wiki.recessim.com/view/DFM-17_Radiosonde).

Everything here was measured with the on-board ADC and read over SWD
(`tools/hw_survey/`), not with a meter. Where a number depends on an
unconfirmed scale factor it says so.

## Summary

| Question | Answer |
|---|---|
| PA0 | **Supply input voltage through a divider**, not VCC: 2.33 V on the pin from USB 5 V, and it sags under load. Divider assumed 2:1 (gives 4.67 V = 5 V minus a diode drop). **Needs bench calibration.** |
| PB1 | **Supply current monitor**, consistent with **1 V = 1 A**: GPS ~20-25 mA, radio READY ~6-10 mA, TX steps with PA level. **Needs meter calibration.** |
| PC2 | **USB VBUS through a divider** (ADC1_IN12): traced on the PCB from the mini-USB connector ([data_dfm17_RE, Analog Inputs](https://github.com/gx1400/data_dfm17_RE/blob/master/imgs/signaltrace/Analog%20Inputs.png)). Read since the USB-sense change; USB power is reported present when the pin is above 0.5 V. **Divider ratio and battery-only reading not measured yet.** |
| APRS telemetry | **Implemented**: temperature, supply mV, supply mA in the existing base91 telemetry fields. Decoded over the air by Dire Wolf. |
| EEPROM (M24LR04E-R) | On I2C (PB6/PB7) at 0x53 (user) / 0x57 (system). 512 bytes: **0x000-0x15F Graw factory data**, **0x160-0x1FF free**. **Writable over I2C without a password** (tested at 0x1FC, restored). Good home for settings. Over NFC every sector is locked behind Graw's RF password; sector 3 can probably be opened by the MCU with the I2C password (default `00000000`, untested). |
| 32.768 kHz crystal (LSE) | **Present and starts**, so the RTC can wake the MCU from Stop/Standby on an accurate schedule. |
| Radio clock in sleep | Si4063 DIV_CLK **stops in SLEEP and SHUTDOWN**: the MCU must leave the radio clock (back to HSI) before the radio sleeps. |
| GPS power save | UBX-RXM-PMREQ software backup **works** (~20-25 mA saved) and the receiver **wakes on UART activity** with its fix intact. |
| Lowest current reached | **18.6 mA** (LEDs off, radio shutdown, GPS backup, MCU running at 8 MHz). About 14 mA of that is not the MCU, radio or GPS and is **unexplained**. It decides multi-day feasibility, so measuring it on battery power is the next step. |

## Analog inputs

ADC1 is driven by registers (`Core/Src/analog.c`); the CubeMX project has no ADC.
Every reading is scaled by VDDA measured from the internal reference
(VREFINT, nominally 1.20 V), which read **3.31 V**.

### Measurements by state (survey, two runs agreed within a few mA)

| State | PA0 (V) | PB1 (mV) | PB1 as mA at 1 V/A |
|---|---|---|---|
| Idle: MCU 24 MHz on radio clock, radio READY, GPS tracking, LEDs | 2.331 | 52-57 | 52-57 |
| TX unmodulated carrier, PA_PWR_LVL 0x00 | 2.322 | 64-65 | +10 over idle |
| TX, PA 0x20 (the firmware's setting) | 2.316 | 75-77 | +21 |
| TX, PA 0x40 | 2.307 | 87-88 | +33 |
| TX, PA 0x7F (maximum) | 2.298 | 96-103 | +40 to +49 |
| Radio SLEEP (MCU on HSI) | 2.338 | 46-48 | -6 to -10 |
| Radio SHUTDOWN (SDN high) | 2.336 | 47 | -7 to -10 |
| GPS software backup (radio READY) | 2.357 | 27-28 | -20 to -26 |
| Floor, MCU 24 MHz: LEDs off, radio shutdown, GPS backup | 2.356 | 24.2 | 24 |
| Floor, MCU 8 MHz (HSI, PLL off), same | 2.366 | 18.6 | 18.6 |

Reading this:

- **PB1 tracks load current** exactly as a 1 V/A high-side monitor would: the
  u-blox MAX-M8C's ~20-25 mA tracking current and the transmitter's PA steps
  both show up at plausible sizes. The scale is the RECESSIM guess; a meter in
  series will confirm it and any offset.
- **PA0 is the supply after a divider**, and it drops 35 mV at full TX power,
  i.e. it sits behind some source resistance (diode, cable, USB charger). With
  USB 5 V in, 2 x 2.33 V = 4.67 V fits 5 V minus a Schottky drop, hence
  `ANALOG_VIN_DIVIDER 2.0f`. It is not VCC as the wiki suggested.
- The STM32 internal temperature sensor read 30-32 C (typical-value
  calibration, several degrees of error possible).

### APRS telemetry (implemented)

`aprs_prepare_buffer()` now fills the existing base91 telemetry extension
`|ss tt vv ii|` with real values (each 0-8280):

| Field | Value |
|---|---|
| ss | sequence number |
| tt | MCU temperature + 512 (C) |
| vv | supply voltage, mV |
| ii | supply current, mA |

Over the air on 2026-10-29 (HackRF, Dire Wolf):

```
NZ0P-12>APZDFM,WIDE1-1:/290433z/:h%M6T=WOFR1|!#&xTB!X|
  N 38 59.9357, W 094 40.9194, alt 281 m      (real GPS fix)
  Seq=2, A1=542, A2=4674, A3=55   ->  30 C, 4674 mV, 55 mA
```

Recommended follow-ups:

- Send APRS telemetry metadata once in a while so APRS clients label the
  channels: `:NZ0P-12  :PARM.Temp,Vin,Iin`, `:NZ0P-12  :UNIT.C,mV,mA`,
  `:NZ0P-12  :EQNS.0,1,-512,0,1,0,0,1,0`.
- Read the supply while transmitting too (minimum voltage under load is the
  better battery indicator), not only before the packet.
- Move the calibration constants into the EEPROM settings (per board).

### Calibration procedure (planned, bench supply)

1. Flash any build with `analog.c` and run
   `tools/hw_survey/read_analog.sh path/to/dfm17.elf`. It prints raw ADC values,
   VDDA, Vin and Iin once a second over SWD without disturbing the board.
2. Unplug USB, feed the battery terminals from the bench supply with a meter
   on the input and an ammeter in series.
3. Step the supply (e.g. 6.5, 6.0, 5.0, 4.0, 3.6, 3.3, 3.0 V) and record
   `vin_raw`, `iin_raw`, `vref_raw` and the meter readings. Average a few lines:
   current moves with GPS and LED activity.
4. Fit `Vin = a * vin_raw * VDDA / 4095 + b` and the same for current, per
   board. Compare the two boards.
5. Watch the `VDDA_mV` column: when it starts falling below ~3.3 V the 3.3 V
   regulator is in dropout. That is the real minimum battery voltage.
6. Use a button test (1-3 s press: tone + APRS) to get readings with the
   transmitter on.

## EEPROM (U12, ST M24LR04E-R)

Dual-interface EEPROM: I2C to the MCU and ISO 15693 NFC (RF) to a reader.
4 Kbit = 512 bytes in 4 sectors of 128 bytes.

| Item | Finding |
|---|---|
| I2C pins | PB6 = SCL, PB7 = SDA; bus idles high (external pull-ups present) |
| I2C addresses | 0x53 user memory, 0x57 system area (7-bit); nothing else on the bus |
| PC13 (RF WIP/BUSY) | reads high when idle |
| Addressing | 16-bit memory address after the device select |
| UID | `E0 02 5A 67 5B 01 67 7B` (ST, read LSB first at system 0x0914); IC ref 0x5A |
| Sector security status | `0D 0F 0F 0F` (sectors 0-3) |
| I2C write | **works without presenting the I2C password**, sector 3 tested: 4-byte page write ACKed, ready again after ~3 ms, read back correctly, restored to `FF` |

### Contents (backup in `docs/data/eeprom-M24LR04E-E0025A675B01677B.txt`; full backup including the whole system area, made with `tools/hw_survey/eeprom_backup.sh`, in `docs/data/eeprom-M24LR04E-E0025A675B01677B-20261029.txt`/`.bin`)

- `0x000-0x0B7`: Graw configuration bytes (mostly `FF` with a few values;
  `0x038` reads `64 8A FD 00` = 16616036 as a little-endian 32-bit integer,
  not on any PCB label, possibly the sonde serial on the case; `0x088` reads
  `D0 F0 31 01` = **20050128**, the number on the PCB's datamatrix label
  ("2005 0128 G38662 TU"), so the board serial; the other label, "90277
  G39103#929", isn't stored as an integer), and ASCII `EDO..FGHPQ` around
  `0x0A0`.
- `0x0B8-0x157`: little-endian IEEE-754 doubles in groups separated by `1.0`,
  e.g. `-74.19, -13.54, 0.7222, -0.01083, 0.007644, 0.000153` and
  `-1051.08, 34554.3, -355302, 1.36e6`. These look like polynomial calibration
  coefficients, most likely for the temperature/humidity sensor boom.
- `0x160-0x1FF`: erased (`FF`), 160 bytes; **sector 3 (0x180-0x1FF) is entirely
  free**.

Leave the Graw region alone (it may be needed to make sense of the sensor boom
later) and use sector 3.

### Proposed settings block (sector 3, 0x180-0x1FF)

| Offset | Size | Field |
|---|---|---|
| 0x180 | 4 | magic `DFMS` |
| 0x184 | 1 | layout version |
| 0x185 | 7 | callsign (6, space padded) + SSID |
| 0x18C | 1 | mode: fox / APRS beacon / tracker / quiet, plus flags (send APRS in fox, start paused) |
| 0x18D | 4 | frequency, Hz |
| 0x191 | 1 | PA_PWR_LVL |
| 0x192 | 2 | deviation, Hz |
| 0x194 | 2+2+1+2 | fox: tone ms, period s, CW WPM, tone Hz |
| 0x19B | 2+2 | APRS interval s, GPS fix interval s |
| 0x19F | 2+2+2+2 | calibration: Vin scale/offset, Iin scale/offset |
| 0x1A7 | 1 | learned XO_TUNE |
| 0x1A8 | 2 | CRC-16 over 0x180-0x1A7 |
| 0x1C0 | 64 | last known fix: 4 slots of 16 bytes (lat, lon, alt, time, sequence) |

- Load at boot, fall back to compiled-in defaults if the magic or CRC is wrong.
- Write the 4-byte pages with ACK polling (~3-5 ms each).
- The M24LR04E-R is specified for a large but finite number of write cycles
  (see the datasheet). Writing the last fix every 5 s would wear one slot out
  in months, so write it at most every few minutes and only when it has
  moved, rotating through the 4 slots by sequence number.

Ways to change settings, in order of effort:

1. Over SWD (write the bytes with the programmer; a small script can do it).
2. A text command interface on USART1 (the mini-USB connector carries UART,
   not USB), e.g. `set call NZ0P-12`, `set freq 432500000`, `save`.
3. **NFC from a phone** (ST's NFC Tap / ST25 apps read and write ISO 15693
   tags), see below.

### NFC (RF) access and how to open sector 3

RF access is controlled per sector by the sector security status (SSS) byte;
I2C access separately by the I2C_Write_Lock bits (default 0, which is why the
MCU can write sector 3 freely). SSS bits, from the M24LR04E-R datasheet:
b0 = sector locked, b2:b1 = access rights when locked, b4:b3 = which RF
password (1-3, 00 = none) unlocks it.

| b2:b1 (locked) | password presented | no password |
|---|---|---|
| 00 | read, write | read only |
| 01 | read, write | read, write |
| 10 | read, write | no access |
| 11 | read only | no access |

On this board:

| Sector | SSS | Meaning over RF |
|---|---|---|
| 0 (0x000-0x07F) | `0D` | locked, b2:b1 = 10, password 1: no access without it, read/write with it |
| 1-3 (0x080-0x1FF) | `0F` | locked, b2:b1 = 11, password 1: no access without it, read only with it |

So a phone can identify the tag (UID, system info) but read none of the
memory without Graw's RF password 1, and can never write sectors 1-3 over
RF. Opening sector 3 needs its SSS rewritten, which only the MCU can do:

1. Present the I2C password: write 9 bytes to the system area (I2C 0x57) at
   address `0x0900`: password (4 bytes, MSB first), `0x09`, password again.
   `0x07` instead of `0x09` sets a new password (datasheet section 5.16, same
   as the Linux `drivers/nvmem/m24lr.c` driver).
2. Write SSS byte 3 (system area address `0x0003`) to `00` (unlocked: RF read
   and write without a password). Writing an SSS byte needs the I2C password
   to have been presented; reading never does.
3. Read it back.

**Done on board 1, 2026-10-29** with the `nfc_tool` test build
(`make firmware`, then `tools/hw_survey/nfc_tool.sh`, issue #8). Full test
procedure, expected output and the pending phone steps:
[tests/nfc-sector-lock.md](tests/nfc-sector-lock.md).

| Step | Result |
|---|---|
| Write SSS[3] without a password | every byte ACKed, no write cycle, value unchanged: **an ACK doesn't mean the write happened, only a read-back tells** |
| Present `00000000` | accepted (compare took ~3 ms): **the I2C password is still the factory default** |
| Write SSS[3] = `00` | write cycle ~2 ms, reads back `00`; SSS now `0D 0F 0F 00`, sectors 0-2 unchanged |
| Present a wrong password (`12345678`), then write `0F` | the write is ignored: a new present command closes the session |
| Present `00000000` again, write `0F`, then `00` | both stick: **no lockout or attempt counter**, and the change is reversible |

The datasheet confirms there's no side effect to a wrong I2C password: the
protected sectors simply stay protected, and the chip ignores the bus for one
write-cycle time while it compares. A correct password stays valid until
power-off or the next present command.

**Sector 3 of board 1 is left open (`00`)** for the phone test. Without an
NFC coil on the antenna pads the chip can't be reached over RF at all, so
this changes nothing in practice yet. For the settings block, a better final
value is `11`: locked, b2:b1 = 00, password 2. That lets anyone read sector 3,
but writing needs RF password 2 (factory default `00000000` too, unless Graw
changed it; to be checked with the phone).

Other system-area values on board 1:
- I2C_Write_Lock `00 00`: the MCU can write all four sectors over I2C.
- Configuration byte (0x0910) `FF`: bit 3 = 1, so **PC13 is in RF WIP mode**.
  It goes low only while an RF write is being programmed, which is exactly
  the signal for "a phone changed the settings, reload them". The other mode
  (bit 3 = 0, RF BUSY) pulls it low for every RF command.
- Control register (0x0920) `00`, `80` after a write (T-Prog).

RF and I2C cannot access the memory at the same moment; I2C is refused
while RF is busy.

Still to test (needs a coil on the antenna pads and a phone): reading and
writing sector 3 with ST25 NFC Tap / NXP TagInfo, the range, PC13 pulses on a
phone write (the tool counts falling edges on PC13), and board 2.

## Low power

### Measured building blocks

| Block | Current | Notes |
|---|---|---|
| GPS tracking vs software backup | ~20-25 mA saved | wakes on UART activity, fix kept (hot start) |
| Si4063 READY vs SLEEP/SHUTDOWN | ~6-10 mA saved | DIV_CLK stops: MCU must run from HSI first |
| Transmitter, PA 0x20 / 0x7F | +21 / +40-49 mA | only while transmitting |
| MCU 24 MHz vs 8 MHz | ~5.6 mA | running; Stop mode not measurable with its own ADC |
| Unexplained remainder | ~14 mA | with the above off and LEDs off |

Not measurable with the on-board ADC (the MCU is stopped): STM32F100 Stop mode
(datasheet: tens of uA) and Standby (a few uA), Si4063 shutdown (tens of nA),
u-blox backup (tens of uA).

The **~14 mA remainder** is the critical unknown. Candidates: the USB-UART
chip (U17) and the current-sense amplifier while powered from USB, regulator
quiescent current, the sensor-boom analog section (switches, comparator), the
battery protection IC, or an offset in the current monitor. Measure it with a
meter in series **on battery power** (USB unplugged), with the MCU in Stop.

### Duty-cycled design (recommended)

```
        RTC alarm (LSE 32.768 kHz)
             |
   STOP mode (MCU ~uA, radio SHUTDOWN, GPS backup, LEDs off)
             |
   wake on HSI 8 MHz --- every N min: wake GPS (UART), wait for fix (hot start
             |           ~1-5 s), use PPS to trim LSE and XO_TUNE, put GPS back
             |
   radio: SDN low, POWER_UP, configure (~tens of ms)
   optionally switch MCU to the radio clock for exact tone timing
   transmit: CW ID + tone / APRS packet with position and telemetry
   radio SHUTDOWN, MCU back to HSI, store last fix (rarely) in EEPROM
             |
   set next RTC alarm, back to STOP
```

Notes:

- The DFM-17 has an LSE crystal, so the STM32 RTC can wake it on an
  accurate schedule; the GPS PPS can calibrate the RTC.
- HSI timing was good enough for APRS on the bench (tones 0.1% off); a fox that
  transmits briefly can stay on HSI and skip the radio-clock switch.
- The GPS is worth duty-cycling once fixes are less frequent than every
  minute or so: hot starts need the receiver to have kept ephemeris (backup
  mode keeps it) and they are quick; tracking continuously costs ~20-25 mA.
  For a stationary fox, a fix every 30-60 min (or only at power-up) is plenty.

### Battery life estimates

Average current = floor + transmit + GPS. Example: APRS packet every 10 min
(~2 s at ~80 mA), GPS fix every 30 min (~5 s at ~25 mA):

| Floor current | Average | 1500 mAh (2 x CR123A in series, to be confirmed) | 3000 mAh pack |
|---|---|---|---|
| current firmware, always on (~55 mA + fox TX) | ~65 mA | ~1 day | ~2 days |
| 18.6 mA (lowest measured, MCU awake) | ~19 mA | ~3 days | ~6.5 days |
| 2 mA | ~2.3 mA | ~27 days | ~54 days |
| 0.2 mA | ~0.5 mA | ~4 months | ~8 months |

So **multi-day is already reachable** by duty-cycling the GPS and radio, and
**multi-week or more depends entirely on the floor** once the MCU sleeps.
Also check the regulator's dropout (calibration step 5) before choosing a
pack: a 1S Li-ion cell may be too low for a 3.3 V LDO near the end of
discharge; 2S, 3 x AA lithium, or 2 x CR123A suit it better.

## Other observations

- **Timing caveat**: until commit 5368a9d, `HSE_VALUE` was 8 MHz, so once the
  MCU ran from the radio clock every HAL millisecond was really 0.625 ms. The
  survey's waits were therefore shorter than written (e.g. the 3 s GPS backup
  wait was ~1.9 s); none of the conclusions depend on them. The same applies
  to the GPS reply timeout work: the NAV-PVT replies measured as "more than
  1.4 s" were really up to ~0.9 s, consistent with answers at the next 1 Hz
  navigation epoch.

- **PPS lock during GPS backup**: `ppsLockStatus` still read 1 after the
  backup request; with the timing caveat above that wait was really ~1.9 s,
  shorter than TIM7's 2 s lock-loss timeout, so this is probably expected.
  Re-check with the corrected clock.
- **Flashing a running board** with `STM32_Programmer_CLI` in hot-plug + halt
  mode fails on the first attempt about half the time ("failed to erase" /
  "failed to download Sector[0]") and succeeds on a retry.
- Two boards are available; calibrate both and compare.

## Tools

| Path | Purpose |
|---|---|
| `Core/Src/hw_survey.c` | one-shot survey, built with `HW_SURVEY` defined in `main.c`; the fox starts paused after it |
| `tools/hw_survey/read_survey.sh` | read and decode the survey results over SWD |
| `tools/hw_survey/read_analog.sh` | live supply voltage/current/VDDA/temperature over SWD |
| `Core/Src/nfc_tool.c` | NFC EEPROM tool, make variant `nfc_tool` (`-DHW_NFC`): sector security commands over SWD, PC13 watch |
| `tools/hw_survey/nfc_tool.sh` | drive the NFC tool: `show`, `present [PW]`, `sss3 VALUE` |
| `tools/hw_survey/eeprom_backup.sh` | full EEPROM backup of a board (flashes `nfc_tool`, reads twice, writes `docs/data/eeprom-M24LR04E-<UID>-<date>.txt` and `.bin`) |
| `Core/Src/i2c_ee.c` | bit-banged I2C and M24LR04E-R access, shared by the survey and the NFC tool |
| `Core/Src/radio_probe.c` | Si4063 clock-output probe (`RADIO_CLOCK_PROBE` in `init.c`) |
| `tools/aprs_sim/` | host simulator, now also decodes the telemetry |
