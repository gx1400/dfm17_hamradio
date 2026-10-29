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
606) to try other timer settings or MCU clock error. WAVs land in `out/`; open
them in Audacity's spectrogram view to see the tones.
