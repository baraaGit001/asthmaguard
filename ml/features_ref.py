"""Independent numpy implementation of the log-mel features.

Written from the specification in firmware/src/sensors/features.h, NOT
translated from the C++ code, so a test that agrees with both is a real
check (ml/tests/test_features.py).
"""
from __future__ import annotations

import numpy as np

SR = 16000
WINDOW = 16000
FRAME = 512
NFFT = 512
HOP = 320
FRAMES = 1 + (WINDOW - FRAME) // HOP
MEL = 40
FMIN, FMAX = 20.0, 8000.0
FLOOR = 1e-6


def _hz_to_mel(f):
    return 2595.0 * np.log10(1.0 + np.asarray(f, dtype=np.float64) / 700.0)


def _mel_to_hz(m):
    return 700.0 * (10.0 ** (np.asarray(m, dtype=np.float64) / 2595.0) - 1.0)


def mel_filterbank() -> np.ndarray:
    pts = _mel_to_hz(np.linspace(_hz_to_mel(FMIN), _hz_to_mel(FMAX), MEL + 2))
    freqs = np.arange(NFFT // 2 + 1) * SR / NFFT
    fb = np.zeros((MEL, freqs.size))
    for m in range(MEL):
        a, b, c = pts[m], pts[m + 1], pts[m + 2]
        up = (freqs - a) / (b - a)
        down = (c - freqs) / (c - b)
        w = np.where(freqs <= b, up, down)
        w[(freqs <= a) | (freqs >= c)] = 0.0
        fb[m] = np.maximum(w, 0.0)
    return fb


_FB = mel_filterbank()
_WIN = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(FRAME) / FRAME)  # periodic Hann


def window_features(pcm: np.ndarray) -> np.ndarray:
    x = np.asarray(pcm, dtype=np.int16).astype(np.float64) / 32768.0
    assert x.shape == (WINDOW,)
    idx = np.arange(FRAMES)[:, None] * HOP + np.arange(FRAME)[None, :]
    frames = x[idx] * _WIN
    spec = np.fft.rfft(frames, n=NFFT, axis=1)
    power = spec.real ** 2 + spec.imag ** 2
    mel = power @ _FB.T
    logmel = np.log(mel + FLOOR)
    return (logmel - logmel.mean()).astype(np.float32)


def rms_dbfs(pcm: np.ndarray) -> float:
    x = np.asarray(pcm, dtype=np.float64)
    if x.size == 0:
        return -120.0
    rms = np.sqrt(np.mean(x * x)) / 32768.0
    return float(20 * np.log10(rms)) if rms > 1e-6 else -120.0
