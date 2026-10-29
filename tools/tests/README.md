# Host unit tests

The firmware sources in `dfm17/Core/Src` built on a PC against a fake HAL, so
their logic can be tested without the board. `make` (here, or `make test` at
the top level) builds and runs everything; each suite prints its failed checks
and a summary, and the run fails if any check does.

| Suite | Source under test | What it covers |
|---|---|---|
| `test_aprs` | aprs.c | position and status packets field by field (base91 position, altitude, telemetry), sequence numbers, fix validity, AX.25 header, FCS, and the line bit stream decoded back to the frame (NRZI, flags, bit stuffing) |
| `test_tracker` | tracker.c | the GPS duty cycle against a scripted GPS: quick fix, timeouts, 2D only, a fix that comes and goes (last 3D fix sent), no stale fix after a wake-up, a cold start longer than the timeout, the 5 min cadence |
| `test_gps` | gps.c | the exact UBX-RXM-PMREQ sleep command, wake-up bytes, no polling while asleep or without PPS, configuration retries, UBX checksums of every canned message, the UBX-only CFG-PRT |
| `test_gnss` | GNSS.c, gps.c | NAV-PVT replies through the UART callbacks (negative lat/lon/altitude), corrupt replies ignored, a silent GPS timing out, the buffer scan staying in bounds |
| `test_si4063` | si4063.c | PLL settings reproduce every frequency in 420-450 MHz to the synthesizer step, band selection, deviation and offset units, modulation modes, START_TX / READY, the CTS timeout, the fast SPI mode for sine AFSK |
| `test_fox` | fox.c | the Morse ID decoded back from the tone timeline, element and gap timing, hunt tone length, APRS every 10th cycle, the button test transmissions |
| `test_diag` | diag.c | button debounce, contact bounce, short/medium/long presses, a press held at power-on ignored, LED feedback while held, heartbeat, red blink codes, PPS flash, the PPS cycle count across the counter wrap |
| `test_analog` | analog.c | VDDA from VREFINT, supply voltage and current, independence from VDDA, temperature |

How it works:

- `fakehal/stm32f1xx_hal.h` replaces the HAL header, so the project's own
  headers (`main.h`, `gpio.h`, ...) compile unchanged. Registers are plain
  memory; `fake_hal.c` has a millisecond clock that `HAL_Delay()` advances.
- Each test supplies fakes for the modules around the one it tests and
  records what the code under test does (SPI bytes, UART bytes, tone timeline,
  LED states).
- Built with `-fsanitize=undefined,bounds-strict -fsanitize-trap=all`: undefined
  behaviour or an out-of-bounds array index stops the test (it found
  `GNSS_ParseBuffer()` reading past its buffer).

The APRS audio path (tones, de-emphasis, decoding with Dire Wolf) is checked
by `make -C tools/aprs_sim check`.
