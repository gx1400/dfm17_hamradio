# aprs_sim

Host-side test bench for the firmware APRS encoder. It builds the real
`dfm17/Core/Src/aprs.c` with gcc (hardware stubbed out in `stubs/`), dumps the
NRZI line bits, renders them as receiver audio, and decodes them with Dire Wolf.

```
make test      # needs gcc, python3 + numpy/scipy, direwolf (atest)
```

Each mode prints a software decode of the frame (with FCS check) and the
number of packets `atest` decoded:

| mode       | what it models                                                    |
|------------|-------------------------------------------------------------------|
| `ideal`    | clean 1200/2200 Hz AFSK, checks framing / stuffing / NRZI / FCS    |
| `firmware` | GPIO3 square wave from TIM15 + `processAprsTick()` + `tx_aprs()`   |
| `si-sync`  | same, re-sampled on the Si4063 TX data clock (direct sync mode)    |

`--tick-period N` sets TIM15 counts per tick ((PSC+1)*(ARR+1), firmware is
909 at 24 MHz) to try other timer settings or MCU clock error. WAVs land in `out/`; open
them in Audacity's spectrogram view to see the tones.

## Square wave vs sine AFSK through a receiver

```
make afsk-test
```

`afsk_harness` runs the firmware's `tx_aprs()` with the TIM15 ISR simulated
in a second thread and prints the carrier offset at every tick, for the
square wave on GPIO3 (`square`) and the sine AFSK from `MODEM_FREQ_OFFSET`
writes (`sine`, `aprs_afsk_sine = 1`; the harness is built with
`-DAPRS_AFSK_SINE=1` like the `sine_test` firmware). `afsk_sim.py` turns that into receiver
audio, with `--deemph` applying the 6 dB/octave de-emphasis of a two-way radio,
and prints how much weaker the 2200 Hz tone arrives than 1200 Hz:

| | flat (SDR) | de-emphasized (radio) |
|---|---|---|
| square, +-2.6 kHz | +0.8 dB | -4.5 dB |
| sine, 2.8 / 4.0 kHz | +3.8 dB | -1.5 dB |

Hardware TNCs tolerate less of this twist than Dire Wolf does.
