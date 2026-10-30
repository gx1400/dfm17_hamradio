## What and why
<!-- What changed and why. Link the issue: "Closes #NN" or "Part of #NN". -->


## Firmware size
<!-- CI writes the flash/RAM table to the firmware job's summary and fails
     over budget or on software doubles. Call out anything unexpected; local
     CubeIDE (GCC 14) builds are ~2.4 KB smaller than CI's GCC 13. -->
- [ ] No firmware change
- [ ] Size change checked in the CI summary, and explained if it grew

## Tests
- [ ] Host tests added or updated (`tools/tests`), or not needed because:
- [ ] APRS simulation still decodes (`make test`, CI)

## Hardware
<!-- Simulation and host tests can't catch everything: sine AFSK passed the
     simulator and failed on air (#10). Say what ran on a real board. -->
- [ ] Not flashed, because:
- [ ] Flashed and tested
  - Board (PCB serial / EEPROM UID):
  - Build (variant, commit):
  - What ran (bench, dummy load, on air, in the car) and the result:

## RF and station ID
<!-- Anything that changes what goes out over the air. -->
- [ ] No change to RF behaviour
- [ ] Changes frequency, PA power, deviation, modulation or timing
- [ ] Changes callsign / SSID handling or how often the station is identified
- [ ] Tested on air; receivers used (HackRF + Dire Wolf, D72A, iGate):

## Checklist
- [ ] STM32CubeMX: `.ioc` unchanged, or the generated files regenerated and committed (CRLF kept; CI checks)
- [ ] EEPROM: no writes to the NFC EEPROM, or a full backup exists for each board touched (`tools/hw_survey/eeprom_backup.sh`)
- [ ] Docs updated (README, `docs/`), or not needed
- [ ] Lint passes locally or in CI (`tools/lint/*`, ruff, shellcheck)
