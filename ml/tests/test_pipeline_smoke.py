"""Smoke test of the data side of ml/pipeline.py on a tiny fake input tree
(no TensorFlow needed): discovery, group split, onset windows, silence drop,
augmentation, balancing and features."""
from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

import numpy as np
import pytest
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pipeline as P  # noqa: E402
from features_lib import Features  # noqa: E402

SR = 16000
pytestmark = pytest.mark.skipif(shutil.which("ffmpeg") is None, reason="ffmpeg missing")


def _cough(rng, n_coughs=3, dur=4.0):
    x = 0.002 * rng.standard_normal(int(SR * dur))
    for k in range(n_coughs):
        s = int(SR * (0.4 + k * 1.1))
        burst = rng.standard_normal(5000) * np.exp(-np.arange(5000) / 1000) * 0.6
        x[s:s + 5000] += burst
    return x


def _tree(root: Path):
    rng = np.random.default_rng(0)
    cv = root / "coughvid/public_dataset_v3/coughvid_20211012"
    cv.mkdir(parents=True)
    for i in range(12):
        sf.write(cv / f"rec{i:02d}.wav", _cough(rng).astype(np.float32), SR)
        (cv / f"rec{i:02d}.json").write_text(json.dumps({"cough_detected": "0.97" if i < 10 else "0.2"}))
    esc = root / "esc/ESC-50-master/wav_files"
    esc.mkdir(parents=True)
    for i in range(20):
        x = 0.1 * rng.standard_normal(SR * 5) if i % 4 else np.zeros(SR * 5)  # some silent clips
        sf.write(esc / f"1-{1000 + i}-A-{(i % 3) + 30}.wav", x.astype(np.float32), 44100 // 2)
    sf.write(esc / "1-2000-A-24.wav", _cough(rng, dur=5.0).astype(np.float32), SR)


def test_data_pipeline(tmp_path):
    _tree(tmp_path)
    items = P.discover(tmp_path)
    assert sum(1 for i in items if i.source == "coughvid") == 10   # low score dropped
    assert any(i.source == "esc:coughing" and i.label == 1 for i in items)
    assert P.group_split("cv:x") == P.group_split("cv:x")

    splits = P.build_windows(items)
    allw = sum(len(s.y) for s in splits.values())
    assert allw > 20
    for s in splits.values():
        for w in s.x:
            assert w.shape == (SR,)
            assert P.rms_db(w) > -60  # silent ESC clips dropped
    # Each group lands in exactly one split
    groups = {}
    for it in items:
        groups.setdefault(it.group, set()).add(P.group_split(it.group))
    assert all(len(v) == 1 for v in groups.values())

    rng = np.random.default_rng(0)
    tr = P.balance(P.augment(splits["train"], rng), rng)
    assert sum(tr.y) * 2 == len(tr.y)
    X, y = P.featurize(Features(), tr)
    assert X.shape[1:] == (49, 40, 1) and np.isfinite(X).all()


def test_onsets_find_each_cough():
    rng = np.random.default_rng(3)
    on = P.cough_onsets(_cough(rng, 3), 6)
    assert len(on) == 3
    for k, s in enumerate(on):
        assert abs(s - int(SR * (0.4 + k * 1.1))) < 700
