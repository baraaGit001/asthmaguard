"""The C++ features (device/src/core/sensors/features.cpp, used on the Zero 2 W and
for training) must match the independent numpy reference on the same WAVs.

Run: ml/.venv/bin/python -m pytest ml/tests -q
Extra real recordings: AG_TEST_WAVS=/path/a.wav:/path/b.wav
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

import numpy as np
import pytest
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import features_ref as ref  # noqa: E402
from features_lib import Features, to_int16  # noqa: E402

SR = 16000


@pytest.fixture(scope="module")
def cpp():
    return Features()


def _synthetic(tmp: Path) -> list[Path]:
    rng = np.random.default_rng(7)
    t = np.arange(SR) / SR
    sigs = {
        "tone_440": 0.3 * np.sin(2 * np.pi * 440 * t),
        "tone_3k_quiet": 0.01 * np.sin(2 * np.pi * 3000 * t),
        "chirp": 0.5 * np.sin(2 * np.pi * (100 + 3900 * t) * t),
        "white_noise": 0.2 * rng.standard_normal(SR),
        # crude cough-like burst: noise with a fast attack and decay
        "burst": np.concatenate([np.zeros(4000), rng.standard_normal(6000) * np.exp(-np.arange(6000) / 1200) * 0.8, np.zeros(6000)]),
        "clipped": np.clip(2.0 * np.sin(2 * np.pi * 220 * t), -1, 1),
        "near_silence": 1e-4 * rng.standard_normal(SR),
    }
    out = []
    for name, x in sigs.items():
        p = tmp / f"{name}.wav"
        sf.write(p, x.astype(np.float32), SR, subtype="PCM_16")
        out.append(p)
    return out


def _wavs(tmp: Path) -> list[Path]:
    paths = _synthetic(tmp)
    extra = os.environ.get("AG_TEST_WAVS", "")
    paths += [Path(p) for p in extra.split(":") if p]
    return paths


def _first_second(path: Path) -> np.ndarray:
    x, sr = sf.read(path, dtype="int16", always_2d=True)
    x = x[:, 0]
    if sr != SR:
        pytest.skip(f"{path} is {sr} Hz; resample to 16 kHz first")
    if x.size < SR:
        x = np.pad(x, (0, SR - x.size))
    return x[:SR]


def test_dims(cpp):
    assert (cpp.frames, cpp.mel) == (ref.FRAMES, ref.MEL) == (49, 40)


def test_cpp_matches_reference_on_wavs(cpp, tmp_path):
    for p in _wavs(tmp_path):
        pcm = _first_second(p)
        a = cpp.window_features(pcm)
        b = ref.window_features(pcm)
        diff = np.abs(a - b)
        # float32 on the device vs float64 here: tiny differences, mostly in
        # near-empty mel bins close to the log floor.
        assert diff.mean() < 1e-3, (p.name, float(diff.mean()))
        assert diff.max() < 5e-2, (p.name, float(diff.max()))
        assert abs(cpp.rms_dbfs(pcm) - ref.rms_dbfs(pcm)) < 1e-3


def test_level_invariance(cpp):
    rng = np.random.default_rng(1)
    x = 0.02 * rng.standard_normal(SR)  # x8 stays far from clipping
    a = cpp.window_features(to_int16(x))
    b = cpp.window_features(to_int16(x * 8))  # +18 dB
    assert np.abs(a - b).mean() < 0.02


def test_rejects_wrong_length(cpp):
    with pytest.raises(ValueError):
        cpp.window_features(np.zeros(100, dtype=np.int16))
