#!/usr/bin/env python3
"""Misst die gerenderten GALE-Hoerbeispiele: Lautheit (ITU-R BS.1770 / EBU R128), Spitzenpegel, Stereo-/Mono-Verhalten.
Aufruf: analyze.py <ordner-mit-wav> [--plots <ordner>]"""
import sys, glob, os, struct
import numpy as np
from scipy import signal


def read_wav_f32(path):
    with open(path, "rb") as f:
        d = f.read()
    assert d[:4] == b"RIFF"
    fs = struct.unpack("<I", d[24:28])[0]
    i = d.find(b"data")
    n = struct.unpack("<I", d[i + 4:i + 8])[0]
    x = np.frombuffer(d[i + 8:i + 8 + n], dtype="<f4").reshape(-1, 2)
    return fs, x.astype(np.float64)


def k_weight(x, fs):
    # BS.1770: Hochregal + RLB-Hochpass; Koeffizienten fuer beliebige fs aus den analogen Prototypen
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / fs); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    x = signal.lfilter(b, a, x, axis=0)
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = np.tan(np.pi * f0 / fs)
    a = [1, 2 * (K * K - 1) / (1 + K / Q + K * K), (1 - K / Q + K * K) / (1 + K / Q + K * K)]
    return signal.lfilter([1, -2, 1], a, x, axis=0)


def lufs_blocks(x, fs, win):
    y = k_weight(x, fs)
    n = int(win * fs); hop = int(0.1 * fs)
    z = [np.sum(np.mean(y[i:i + n] ** 2, axis=0)) for i in range(0, len(y) - n + 1, hop)]
    return np.array([-0.691 + 10 * np.log10(v + 1e-20) for v in z])


def integrated(x, fs):
    b = lufs_blocks(x, fs, 0.4)
    z = 10 ** ((b + 0.691) / 10)
    g = b > -70
    if not g.any(): return -99.0
    rel = -0.691 + 10 * np.log10(z[g].mean()) - 10
    g2 = b > max(-70, rel)
    return -0.691 + 10 * np.log10(z[g2].mean()) if g2.any() else -99.0


def analyse(path, plots=None):
    fs, x = read_wav_f32(path)
    name = os.path.basename(path)[:-4]
    st = lufs_blocks(x, fs, 3.0)
    peak = 20 * np.log10(np.max(np.abs(x)) + 1e-12)
    rms = 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)
    L, R = x[:, 0], x[:, 1]
    corr = np.corrcoef(L, R)[0, 1]
    mono = (L + R) / 2
    # Mono-Kompatibilitaet: Leistung der Summe gegen Leistung der Kanaele (0 dB = unkorreliert, +3 = gleich, -inf = Ausloeschung)
    mdb = 10 * np.log10(np.mean((L + R) ** 2) / (np.mean(L ** 2) + np.mean(R ** 2) + 1e-20))
    dc = np.mean(x, axis=0)
    print(f"{name:46s} LUFS int {integrated(x, fs):6.1f}  kurz min/max {st.min():6.1f}/{st.max():6.1f}  Spitze {peak:6.1f} dBFS  "
          f"RMS {rms:6.1f}  Korr L/R {corr:5.2f}  Mono-Summe {mdb:+4.1f} dB  DC {abs(dc).max():.1e}  NaN {int(np.isnan(x).any())}")
    if plots:
        import matplotlib; matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(2, 1, figsize=(11, 6), gridspec_kw={"height_ratios": [3, 1]}, sharex=True)
        f, t, S = signal.spectrogram(mono, fs, nperseg=4096, noverlap=3072)
        ax[0].pcolormesh(t, f, 10 * np.log10(S + 1e-14), shading="auto", vmin=-130, vmax=-50, cmap="magma")
        ax[0].set_yscale("symlog", linthresh=100); ax[0].set_ylim(20, 12000); ax[0].set_ylabel("Hz"); ax[0].set_title(name)
        tt = np.arange(len(st)) * 0.1 + 1.5
        ax[1].plot(tt, st); ax[1].set_ylabel("LUFS (3 s)"); ax[1].set_xlabel("s"); ax[1].grid(alpha=.3)
        fig.tight_layout(); fig.savefig(os.path.join(plots, name + ".png"), dpi=80); plt.close(fig)


if __name__ == "__main__":
    folder = sys.argv[1]
    plots = sys.argv[sys.argv.index("--plots") + 1] if "--plots" in sys.argv else None
    if plots: os.makedirs(plots, exist_ok=True)
    for p in sorted(glob.glob(os.path.join(folder, "*.wav"))):
        analyse(p, plots)
