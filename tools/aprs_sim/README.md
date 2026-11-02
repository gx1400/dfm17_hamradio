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
| `si-sync`  | what goes on air: the same, sampled on the Si4063 TX data clock (sync direct 2GFSK) and smoothed by its TX filter |

`--tick-period N` sets TIM15 counts per tick ((PSC+1)*(ARR+1), firmware is
909 at 24 MHz) to try other timer settings or MCU clock error; `--si-rate N`
tries another TX data clock (at 4400, the old setting, the 1200 Hz tone is
mangled and nothing decodes). WAVs land in `out/`; open
them in Audacity's spectrogram view to see the tones.

## The square wave through a receiver

```
make afsk-test
```

`afsk_harness` runs the firmware's `tx_aprs()` with the TIM15 ISR simulated
and prints the carrier offset at every tick: the square wave on GPIO3.
`afsk_sim.py` turns that into receiver audio, `--si-sync` first passing it
through the radio's sampling and TX filter as `aprs_sim.py --mode si-sync`
does, `--deemph` applying the 6 dB/octave de-emphasis of a two-way radio. It
prints how much weaker the 2200 Hz tone arrives than 1200 Hz:

| | flat (SDR) | de-emphasized (radio) |
|---|---|---|
| GPIO3 square, +-2.6 kHz | +0.9 dB | -4.4 dB |
| through the TX filter (on air) | +0.1 dB | -5.1 dB |

Hardware TNCs tolerate less of this twist than Dire Wolf does.

Sine AFSK, from `MODEM_FREQ_OFFSET` writes at 13.2 kHz, passed this simulator
but never decoded on air: the Si4063 applies the offset only every 220 us,
which the simulator didn't model (#54). The code is gone; git has it.
