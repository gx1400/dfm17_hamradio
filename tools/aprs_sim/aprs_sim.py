#!/usr/bin/env python3
"""
aprs_sim.py - turn the firmware's APRS line bits into receiver audio.

Reads the '0'/'1' NRZI bit stream printed by aprs_harness on stdin, checks the
AX.25 frame in software (NRZI decode, unstuff, FCS), and writes a WAV of what
an FM receiver would hear, for Dire Wolf's `atest`.

Modes:
  ideal     phase-continuous 1200/2200 Hz sines at exactly 1200 baud
            (tests only the bit stream: framing, stuffing, NRZI, FCS)
  firmware  square wave on Si4063 GPIO3 exactly as TIM15 + processAprsTick()
            + the tx_aprs() loop generate it, radio follows the pin instantly
  si-sync   like firmware, but GPIO3 is re-sampled on the Si4063's own TX data
            clock (MOD_DIRECT_MODE_SYNC at RF_MOD_APRS_SR), as on the target
"""

import argparse
import sys
import wave

import numpy as np
from scipy import signal

# firmware constants (dfm17/Core/Inc/aprs.h, tim.c, main.c, si4063.h)
SYSCLK = 24e6  # Si4063 12.8 MHz / 8 * 15 (or HSI/2 * 6)
APRS_MARK = 0
APRS_SPACE = 1
APRS_MARK_TICKS = 11
APRS_SPACE_TICKS = 6
APRS_BAUD_TICKS = 22
SI_DATA_RATE = 4400  # RF_MOD_APRS_SR

FINE_RATE = 1_600_000  # simulation grid
OUT_RATE = 48_000


def crc_x25(data):
    crc = 0xFFFF
    for b in data:
        for j in range(8):
            bit = (b >> j) & 1
            crc = (crc >> 1) ^ 0x8408 if (crc & 1) != bit else crc >> 1
    return crc ^ 0xFFFF


def check_frame(bits):
    """NRZI-decode and unstuff the line bits, then verify the first frame."""
    data = [1 if bits[i] == bits[i - 1] else 0 for i in range(1, len(bits))]
    s = "".join(map(str, data))
    flag = "01111110"
    start = s.find(flag)
    while s.startswith(flag, start + 8):
        start += 8
    start += 8
    end = s.find(flag, start)
    if start < 8 or end < 0:
        return "no frame found between flags"
    raw = s[start:end]
    out, ones, i = [], 0, 0
    while i < len(raw):
        b = raw[i]
        out.append(b)
        ones = ones + 1 if b == "1" else 0
        if ones == 5:
            i += 1  # drop the stuffed zero
            ones = 0
        i += 1
    if len(out) % 8:
        return f"frame is {len(out)} bits, not a whole number of bytes"
    frame = bytes(int("".join(out[k : k + 8])[::-1], 2) for k in range(0, len(out), 8))
    body, fcs = frame[:-2], frame[-2] | (frame[-1] << 8)
    want = crc_x25(body)
    src = "".join(chr(c >> 1) for c in body[7:13]).strip()
    dst = "".join(chr(c >> 1) for c in body[0:6]).strip()
    info = body[16 + 7 :].decode("ascii", "replace") if len(body) > 23 else ""
    status = "GOOD" if fcs == want else f"BAD (sent {fcs:04x}, expected {want:04x})"
    return f"{src}>{dst}: {info!r}  FCS {status}{decode_position(info)}"


def b91(s):
    v = 0
    for c in s:
        v = v * 91 + ord(c) - 33
    return v


def decode_position(info):
    """Decode the compressed '/ddhhmmz/YYYYXXXX$csT' position report."""
    if len(info) < 21 or info[0] != "/" or info[7] != "z":
        return ""
    lat = 90 - b91(info[9:13]) / 380926
    lon = -180 + b91(info[13:17]) / 190463
    alt_ft = 1.002 ** b91(info[18:20])
    out = (
        f"\n  time {info[1:3]}d {info[3:5]}:{info[5:7]}z  "
        f"lat {lat:.5f}  lon {lon:.5f}  alt {alt_ft * 0.3048:.0f} m"
    )
    # base91 telemetry |ss tt vv ii uu|: sequence, temp + 512, supply mV,
    # supply mA, USB mV (0 on battery)
    if len(info) >= 33 and info[21] == "|" and info[32] == "|":
        seq, temp, vin, iin, usb = (b91(info[i : i + 2]) for i in range(22, 32, 2))
        out += (
            f"\n  telemetry seq {seq}  temp {temp - 512} C  supply {vin} mV  {iin} mA  USB {usb} mV"
        )
    return out


def render_ideal(bits):
    n_per_bit = FINE_RATE / 1200.0
    n = int(len(bits) * n_per_bit)
    idx = (np.arange(n) / n_per_bit).astype(int)
    f = np.where(np.array(bits)[idx] == 1, 2200.0, 1200.0)
    phase = 2 * np.pi * np.cumsum(f) / FINE_RATE
    return np.sin(phase)


def firmware_pin_events(bits, tick_period):
    """Mirror processAprsTick() (ISR) and the tx_aprs() main loop.

    Returns (tick_rate, pin level after each TIM15 tick)."""
    tick_rate = SYSCLK / tick_period
    bit_iter = iter(bits)
    aprs_bit = APRS_MARK
    nco = bitc = 0
    aprs_tick = baud_tick = 0
    pin = 0
    levels = []
    done = False
    while not done:
        # ISR
        nco += 1
        bitc += 1
        if aprs_bit == APRS_SPACE and nco >= APRS_SPACE_TICKS:
            aprs_tick, nco = 1, 0
        if aprs_bit == APRS_MARK and nco >= APRS_MARK_TICKS:
            aprs_tick, nco = 1, 0
        if bitc == APRS_BAUD_TICKS:
            baud_tick, bitc = 1, 0
        # main loop (assumed to run before the next interrupt)
        if aprs_tick:
            aprs_tick = 0
            pin ^= 1
            if baud_tick:
                baud_tick = 0
                b = next(bit_iter, None)
                if b is None:
                    done = True
                else:
                    aprs_bit = APRS_SPACE if b else APRS_MARK
        levels.append(pin)
    return tick_rate, np.array(levels)


def render_firmware(bits, tick_period, si_sync, rng):
    tick_rate, levels = firmware_pin_events(bits, tick_period)
    n = int(len(levels) / tick_rate * FINE_RATE)
    t = np.arange(n) / FINE_RATE
    if si_sync:
        # the radio latches GPIO3 on its own clock, unrelated in phase to TIM15
        phase = rng.random()
        t = (np.floor(t * SI_DATA_RATE + phase) - phase) / SI_DATA_RATE
    idx = np.clip((t * tick_rate).astype(int), 0, len(levels) - 1)
    return levels[idx] * 2.0 - 1.0


def receiver_audio(x):
    """FM demod output is the deviation waveform; band-limit it like a radio."""
    sos = signal.butter(4, [300, 3500], btype="bandpass", fs=FINE_RATE, output="sos")
    y = signal.sosfilt(sos, x)
    y = signal.resample_poly(y, OUT_RATE, FINE_RATE)
    return y / (np.max(np.abs(y)) + 1e-12)


def write_wav(path, y):
    pad = np.zeros(OUT_RATE // 4)
    pcm = (np.concatenate([pad, y, pad]) * 0.8 * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(OUT_RATE)
        w.writeframes(pcm.tobytes())


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--mode", choices=["ideal", "firmware", "si-sync"], default="ideal")
    ap.add_argument(
        "--tick-period",
        type=int,
        default=909,
        help="TIM15 counts per tick, (PSC+1)*(ARR+1); firmware uses 1*909=909",
    )
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("-o", "--output", default="aprs.wav")
    args = ap.parse_args()

    line = sys.stdin.read().strip()
    bits = [int(c) for c in line if c in "01"]
    print(f"{len(bits)} line bits, decoded: {check_frame(bits)}")

    if args.mode == "ideal":
        x = render_ideal(bits)
    else:
        rate = SYSCLK / args.tick_period
        print(
            f"TIM15 tick {rate:.1f} Hz -> mark {rate / (2 * APRS_MARK_TICKS):.1f} Hz, "
            f"space {rate / (2 * APRS_SPACE_TICKS):.1f} Hz, "
            f"baud {rate / APRS_BAUD_TICKS:.1f}"
        )
        x = render_firmware(
            bits, args.tick_period, args.mode == "si-sync", np.random.default_rng(args.seed)
        )
    write_wav(args.output, receiver_audio(x))
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
