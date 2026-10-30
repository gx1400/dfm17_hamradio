"""measure.py on.iq off.iq centre_hz target_hz

Find the DFM-17 carrier within 5 kHz of target_hz in a HackRF capture (8-bit IQ, 2 MS/s,
tuned to centre_hz). off.iq is the same capture with the carrier switched off:
only peaks at least 10 dB stronger with the carrier on count, which rejects
the HackRF's own spurs (e.g. at multiples of 10 MHz). Prints target MHz,
measured MHz, offset Hz, offset ppm and level dB above the noise median, or
'not found'. Sub-bin accuracy by parabolic interpolation.
"""

import sys

import numpy as np

fs = 2e6
N = 1 << 19


def spectrum(path):
    x = np.fromfile(path, dtype=np.int8).astype(np.float32).view(np.complex64)
    x = x[200000 : 200000 + N]  # skip the tuner settling
    return np.abs(np.fft.fftshift(np.fft.fft(x * np.hanning(N)))) ** 2


on, off = spectrum(sys.argv[1]), spectrum(sys.argv[2])
fc, ft = float(sys.argv[3]), float(sys.argv[4])
f = fc + np.fft.fftshift(np.fft.fftfreq(N, 1 / fs))
# +-5 kHz covers a few ppm of TCXO plus HackRF error at 450 MHz
cand = (np.abs(f - ft) < 5e3) & (on > 10 * off)
if not cand.any():
    print(f"{ft / 1e6:.4f} not found")
    sys.exit()
k = np.flatnonzero(cand)[np.argmax(on[cand])]
a, b, c = np.log(on[k - 1 : k + 2])
fpk = f[k] + 0.5 * (a - c) / (a - 2 * b + c) * fs / N
print(
    f"{ft / 1e6:.4f} {fpk / 1e6:.6f} {fpk - ft:+8.1f} {(fpk - ft) / ft * 1e6:+6.2f} "
    f"{10 * np.log10(on[k] / np.median(on)):5.1f}"
)
