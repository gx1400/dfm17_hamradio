# Serial console (#50)

A command line on the mini-USB connector. With it, a board can be checked and set up with a plain 3.3 V USB-serial adapter and no ST-Link. `dfmcfg.py --port` and the settings app use it the same way they use SWD.

## Wiring

The connector carries USART1, not USB (checked on board 1, #50):

| Mini-USB pin | MCU | Adapter |
|---|---|---|
| VBUS | supply input (USB power) | not connected |
| D+ | PA9, USART1 TX | RX |
| D- | PA10, USART1 RX | TX |
| GND | ground | GND |

- 115200 8N1, 3.3 V logic levels.
- **Not RS-232:** a DB9 serial adapter idles at about −6 V and would damage the pins.
- Leave the adapter's VCC unconnected.

**The console is on only while the board has USB power:**
- **On battery:** the UART is off, PA9/PA10 are analog inputs (no current into a cable, and a floating RX can't produce characters), and `printf` output is dropped.
- **Switching:** it follows USB power after two readings in a row (about 2 s). When it turns on it prints a banner with the firmware version.

## Using it

Any terminal at 115200 8N1 works; the console echoes what you type. Each command is one line. The reply ends with a line starting `ok` or `err`, followed by a `> ` prompt.

The firmware's diagnostics (`GPS Update!`, …) go to the same port. `log off` stops them; command replies are always sent.

**One command at a time:** a line runs from the main loop between other work. While the board is busy (booting, configuring the GPS, transmitting a packet or a fox tone), the line waits, and anything typed meanwhile is dropped. Send a command and wait for its `ok`/`err`; the tools do.

| Command | Does |
|---|---|
| `help` | the command list |
| `ver` | `ok DFMFW <version> <variant> proto 1 settings 1 64 cal 1 32 uid <MCU UID> flash <KB>` |
| `status` | mode, settings, GPS, TCXO correction, supply, counters (a few lines, then `ok …`) |
| `analog` | `ok` + the latest readings: `vin_raw iin_raw vref_raw vdda_mv vin_mv iin_ma temp_c usb_raw usb_mv on_usb` |
| `log on` / `log off` | the `printf` diagnostics on the port |
| `fox stop` / `fox start` | pause / resume the fox or tracker, like a long button press |
| `config` | config mode: nothing is scheduled or transmitted (the transmit gate refuses every key-up). Replies `ok 0` or `ok 1`: whether it was on already |
| `run` | back to normal; also wakes a GPS stopped with `gps off` (the tracker starts a new cycle) |
| `gps on` / `gps off` | wake the GPS, or put it in backup mode (config mode only) |
| `cfg get` | `ok <128 hex digits> <source> <error> <repairs> <refused>`: the live settings block ([settings.md](settings.md)) |
| `cfg put <hex>` | load a whole 64-byte block, CRC included; checked like over SWD, then applied (config mode only) |
| `cfg save` / `cfg reload` / `cfg defaults` | as `dfmcfg.py save/reload/defaults` (config mode only) |
| `cal get` / `cal put <hex>` / `cal save` / `cal reload` / `cal defaults` | the same for the 32-byte calibration block |
| `reset` | restart the MCU (refused on battery, where a reset turns the board off, #44) |

**Errors:**
- `err unknown`: not a command.
- `err usage`: wrong arguments.
- `err config`: a change outside config mode.
- `err length`, `err hex`, `err crc`: a damaged or wrong `put` block.
- `err invalid <n>`: the firmware's check refused the block; `n` is the `settings_error` / `cal_error` code.
- `err eeprom`: the EEPROM write or read-back failed.
- `err long`: a line over 143 characters.
- `err battery`: `reset` on battery.

**Safety:**
- No command keys the transmitter.
- Every change goes through the same checks as over SWD.
- Config mode is RAM-only, and it ends when USB power goes away, so a board can't be left mute in the field.

## From the settings tools

```sh
tools/config/dfmcfg.py --port COM20 show            # Linux: /dev/ttyUSB0, macOS: /dev/cu.usbserial-…
tools/config/dfmcfg.py --port COM20 set callsign=NZ0P ssid=12 --save
tools/config/dfmcfg.py --port COM20 cal
```

- **What the tools do:** send `log off`, check `ver` (protocol and both block layouts must match these tools), and change things inside `config` … `run`. If the board was in config mode already, it stays there.
- **Needs pyserial:** `tools/requirements-config.txt`.
- **In the settings app:** pick the `serial` link and the port.
- **Not over serial:** `send` (no console command transmits), and the MCU's device/revision ID (only a debugger can read it; `ver` gives the unique ID).

## Code

| File | |
|---|---|
| `dfm17/Core/Src/console.c` | USB gating, line assembly and echo, the commands, hex blocks, config/log modes (host-tested: `tools/tests/test_console.c`) |
| `dfm17/Core/Src/console_hw.c` | USART1 receive interrupt (register level, no `.ioc` change), pin states, `ver`/`status`/`reset` |
| `settings_store.c` | `settings_command()`: the mailbox commands, shared by SWD and the console |
| `tools/config/dfmcfg.py` | `SerialLink`, `SerialBoard` (the same methods as `Board`) |
| `tools/config/fakeboard.py` | `FakeSerial`: the console's protocol on the simulated board, for the tests and `dfmgui.py --simulate` |

**Size:** about 4.8 KB of flash and 164 B of RAM at -O0. A binary protocol instead of text is assessed in #60.
