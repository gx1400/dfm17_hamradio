# Test: open the NFC EEPROM's sector 3 for RF access

Issue #8, PR #17. Run on board 1 on 2026-10-29: the I2C (MCU) part passed.
The RF (phone) part needs an NFC coil on the antenna pads and hasn't been run
yet.

## Purpose

The M24LR04E-R EEPROM (U12) is shared between I2C (the STM32) and RF (ISO
15693 / NFC-V, a phone). Graw locked every sector against RF access, so a
phone can see the tag but not read or write its memory. Sector 3
(0x180-0x1FF) is unused and is the planned home for the board's settings, so
opening it would let a phone read and change settings.

The test answers:

1. Is the I2C password still the factory default (`00000000`)?
2. Can the MCU rewrite sector 3's security status (SSS[3]) without touching
   Graw's sectors 0-2?
3. Does a wrong password have side effects (lockout, attempt counter)?
4. Is the change reversible?
5. How is the RF WIP/BUSY pin (PC13) configured?
6. *(RF part, pending)* Can a phone read and write sector 3 afterwards, at what
   range, and does PC13 signal a phone write?

## Background

From the M24LR04E-R datasheet, DocID022208 Rev 10:

- **System area** (I2C address 0x57):
  - SSS bytes at `0x0000`-`0x0003`, one per sector;
  - I2C_Write_Lock bits at `0x0800`;
  - present-password register at `0x0900`;
  - configuration byte at `0x0910`;
  - UID at `0x0914`;
  - control register at `0x0920`.
- **SSS byte** (section 4.1, tables 7-9):
  - b0 = sector locked;
  - b2:b1 = access when locked;
  - b4:b3 = which RF password (1-3) unlocks it, 00 = none.

  `00` means unlocked: anyone can read and write over RF.
- **SSS access over I2C:** reading is always allowed. Writing needs the I2C
  password to have been presented, and an I2C write takes effect for RF
  immediately.
- **I2C present password** (section 5.16.1): device 0x57, address `09 00`, the
  password (4 bytes, MSB first), validation code `09`, then the password again.
  - If the two copies differ, the chip doesn't compare at all.
  - A wrong password leaves the protected sectors protected. The chip ignores
    the bus for one write-cycle time while comparing.
  - A correct one stays valid until power-off or the next present command.
  - There is no lockout or attempt counter.
- **Configuration byte** bit 3 (section 4.3.1):
  - `1` = RF WIP mode: PC13 is low only while an RF write is being programmed.
  - `0` = RF BUSY mode: PC13 is low during every RF command.

## Safety

- **Graw's data is never written.** The firmware's only allowed write is SSS[3].
  Sectors 0-2, the RF passwords, the I2C password, the I2C_Write_Lock bits and
  the configuration byte can't be changed by this build.
- A wrong password is harmless (see Background), so trying the default first is
  safe.
- **The change is reversible.** Write `0F` back with the password presented.
- **No exposure without a coil.** The board has no NFC coil fitted, so opening
  sector 3 doesn't expose anything over RF until a coil is soldered on.
- **Backup:** the user area of board 1 is backed up in
  `docs/data/eeprom-M24LR04E-E0025A675B01677B.txt`. Board 1's system area
  values before the test are recorded under Results below.

## Equipment

- The board, powered (USB or battery), with the ST-Link on SWD.
- STM32CubeIDE installed: the scripts use its toolchain and STM32_Programmer_CLI.
- For the RF part: an NFC coil on the antenna through-holes (about 5 µH, e.g.
  6 turns, 50 mm across) and a phone with ST25 NFC Tap or NXP TagInfo.

## Procedure

### 1. Build and flash the tool

```sh
make firmware            # builds build/nfc_tool/dfm17.elf among the variants
source tools/hw_survey/common.sh
"$PROG" -c port=SWD mode=HOTPLUG -d build/nfc_tool/dfm17.elf -v -rst
```

Flashing a running board sometimes needs 2-3 tries.

The `nfc_tool` build (`-DHW_NFC`) starts with the tracker/fox paused and
waits for commands written over SWD into its `nfc_tool` struct.
`tools/hw_survey/nfc_tool.sh` writes a command and waits until the
firmware's `done` count moves, then prints the results. Wait about 10 s
after a reset before the first command, because GPS start-up delays the
main loop.

### 2. Record the starting state

```sh
tools/hw_survey/nfc_tool.sh show
```

Expected on an untouched board: `SSS 0-3  0D 0F 0F 0F`, `I2C_Write_Lock 00 00`.
Note the UID and the configuration byte.

### 3. Control: write SSS[3] without a password

```sh
tools/hw_survey/nfc_tool.sh sss3 0F      # the current value, harmless
tools/hw_survey/nfc_tool.sh sss3 00
```

Expected: `last status OK` (every byte ACKed), but `poll 00h ms` (no write
cycle), and SSS[3] still `0F`. This shows the chip ignores the write without
a password, and that an ACK proves nothing.

### 4. Present the default password and open sector 3

```sh
tools/hw_survey/nfc_tool.sh present 00000000
tools/hw_survey/nfc_tool.sh sss3 00
```

Expected:
- `present`: `poll` about 2-3 ms (the compare).
- `sss3`: `poll` about 2 ms (a real write cycle), and `SSS 0-3  0D 0F 0F 00`.

If SSS[3] still reads `0F`, the I2C password isn't the default. Stop there:
don't guess passwords.

### 5. Wrong password, then reversibility

```sh
tools/hw_survey/nfc_tool.sh present 12345678   # wrong: closes the session
tools/hw_survey/nfc_tool.sh sss3 0F            # expect ignored, stays 00
tools/hw_survey/nfc_tool.sh present 00000000   # still accepted: no lockout
tools/hw_survey/nfc_tool.sh sss3 0F            # restore: reads 0F
tools/hw_survey/nfc_tool.sh sss3 00            # reopen: reads 00
```

Sectors 0-2 must read `0D 0F 0F` after every step.

### 6. Restore the normal firmware

```sh
"$PROG" -c port=SWD mode=HOTPLUG -d build/tracker/dfm17.elf -v -rst
```

SSS[3] stays at whatever was written last; it is non-volatile. The I2C
password session ends at power-off.

### 7. RF part (pending: needs the coil and a phone)

With the `nfc_tool` build running and sector 3 open (`00`):

1. Hold the phone over the coil. The app should show:
   - the UID (`E0025A675B01677B` on board 1);
   - the IC reference `0x5A`;
   - 128 blocks of 4 bytes.
2. Read blocks 96-127 (sector 3, byte addresses 0x180-0x1FF). Expect `FF`.
   Blocks in sectors 0-2 should fail without RF password 1.
3. Write a test block to sector 3, e.g. block 126 (0x1F8): `A5 5A C3 3C`.
4. `tools/hw_survey/nfc_tool.sh show`: `PC13 falling edges` should have gone up
   (expected: one per block written, since the board is in RF WIP mode; not
   seen yet).
5. Read the block back over I2C with the survey build, or over RF. Then write
   `FF FF FF FF` back.
6. Note the range: the largest distance at which reads and writes still work.
7. Optional: try presenting RF password 2 = `00000000` from the app, to find
   out whether Graw left it at the default. That decides the final SSS[3]
   (see Results).
8. Repeat the whole test on board 2 and compare its system area and Graw data
   region with board 1.

## Results

### Board 1, 2026-10-29: I2C part passed

Starting state:

| Field | Value |
|---|---|
| SSS 0-3 | `0D 0F 0F 0F` |
| I2C_Write_Lock | `00 00` |
| Configuration byte | `FF`: bit 3 = 1, **RF WIP mode** |
| Control register | `00` (`80` after a write: T-Prog) |
| UID | `E0025A675B01677B` |
| PC13 | high (idle), no falling edges |

| Step | Status | Poll | SSS 0-3 | Conclusion |
|---|---|---|---|---|
| `sss3 0F`, no password | OK | 0 ms | `0D 0F 0F 0F` | ignored |
| `sss3 00`, no password | OK | 0 ms | `0D 0F 0F 0F` | ignored: an ACK doesn't mean a write |
| `present 00000000` | OK | 3 ms | `0D 0F 0F 0F` | |
| `sss3 00` | OK | 2 ms | `0D 0F 0F 00` | **default password accepted, sector 3 open** |
| `present 12345678` (wrong) | OK | 3 ms | `0D 0F 0F 00` | |
| `sss3 0F` | OK | - | `0D 0F 0F 00` | ignored: the wrong present closed the session |
| `present 00000000` | OK | 2 ms | | **no lockout** |
| `sss3 0F` | OK | - | `0D 0F 0F 0F` | restored |
| `sss3 00` | OK | - | `0D 0F 0F 00` | reopened; left in this state |

Answers:

1. **The I2C password is the factory default `00000000`.**
2. SSS[3] can be rewritten. Sectors 0-2 were unchanged throughout.
3. **No side effects from a wrong password:** no lockout or counter, matching the
   datasheet.
4. **Reversible** (`0F` ↔ `00`).
5. **PC13 is already in RF WIP mode** (configuration byte `FF`): low only while a
   phone write is programmed, the right signal for reloading settings. No
   configuration change is needed.
6. Pending.

Board 1 is left with **SSS[3] = `00`** (open) for the phone test.

### Final SSS[3] for the settings block (decision pending)

Fully open (`00`) lets anyone with a phone change the settings. A better final
value is `11` (b0 = 1 locked, b2:b1 = 00, b4:b3 = 10 password 2):

- anyone can read sector 3;
- writing needs RF password 2.

That depends on knowing RF password 2. The factory default is `00000000`,
unless Graw changed it, which step 7.7 checks. The MCU can't change the RF
passwords over I2C; the I2C write-password command only sets the I2C password
(datasheet section 5.16.2).

## Pitfalls

- **Pass hex to STM32_Programmer_CLI** (`0x01`, not `1`). Decimal values get
  written as something else; `nfc_tool.sh` formats them for you.
- **An ACK isn't proof of a write.** The chip ACKs a write to a protected byte
  and then ignores it. Always read back (the tool does).
- **Wait after a reset.** The main loop is busy with GPS start-up for several
  seconds after a reset or flash, so a command sent straight away completes
  late. The script waits up to about 15 SWD reads.
- **One SWD client at a time.** Don't run other SWD scripts at the same time:
  two programmer instances contend for the ST-Link and reads fail.
