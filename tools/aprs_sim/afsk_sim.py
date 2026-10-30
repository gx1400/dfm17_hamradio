#!/usr/bin/env python3
"""
afsk_sim.py - receiver audio from afsk_harness's per-tick carrier offset.

Reads one offset in Hz per TIM15 tick (26402.6 Hz) on stdin, treats it as the
output of an ideal FM discriminator, optionally applies land-mobile FM de-emphasis (6 dB/octave,
750 us, as a radio's speaker/TNC audio has), and writes a 48 kHz WAV for atest. Prints
the level of the 1200 and 2200 Hz tones to show the twist.
"""

import argparse
import sys
import wave

import numpy as np
from scipy import signal

TICK = 24e6 / 909
p = argparse.ArgumentParser()
p.add_argument("-o", required=True)
p.add_argument("--deemph", action="store_true")
a = p.parse_args()

f = np.array([float(x) for x in sys.stdin.read().split()])
t_in = np.arange(len(f)) / TICK
t_out = np.arange(0, t_in[-1], 1 / 48e3)
audio = f[np.minimum((t_out * TICK).astype(int), len(f) - 1)]  # zero-order hold
if a.deemph:
    # 6 dB/octave across the voice band (750 us, corner 212 Hz)
    b, aa = signal.bilinear([1], [750e-6, 1], fs=48e3)
    audio = signal.lfilter(b, aa, audio)
audio = signal.lfilter(*signal.butter(4, [300, 3500], btype="band", fs=48e3), audio)


def lvl(f0):
    """tone amplitude: envelope of a narrow band-pass, near its peak"""
    y = signal.filtfilt(*signal.butter(2, [f0 - 150, f0 + 150], btype="band", fs=48e3), audio)
    return 20 * np.log10(np.percentile(np.abs(signal.hilbert(y)), 99))


print(
    f"1200 Hz {lvl(1200):.1f} dB, 2200 Hz {lvl(2200):.1f} dB, "
    f"2200 relative to 1200: {lvl(2200) - lvl(1200):+.1f} dB, "
    f"peak deviation {np.max(np.abs(f)):.0f} Hz"
)
audio = audio / np.max(np.abs(audio)) * 0.8
with wave.open(a.o, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(48000)
    w.writeframes((audio * 32767).astype("<i2").tobytes())
