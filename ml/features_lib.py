"""ctypes wrapper around device/src/core/sensors/features.cpp.

The training pipeline computes its features with the *same C++ code* that
runs on the Pico, so the two can never drift apart. The shared library is
built on first use with the system C++ compiler.
"""
from __future__ import annotations

import ctypes
import hashlib
import os
import subprocess
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
DEFAULT_SRC = HERE.parent / "device" / "src" / "core" / "sensors"


def _src_dir() -> Path:
    return Path(os.environ.get("AG_FEATURES_SRC", DEFAULT_SRC))


def _build(src: Path, out_dir: Path) -> Path:
    cpp = src / "features.cpp"
    h = src / "features.h"
    digest = hashlib.sha1(cpp.read_bytes() + h.read_bytes()).hexdigest()[:12]
    lib = out_dir / f"libagfeat-{digest}.so"
    if not lib.exists():
        out_dir.mkdir(parents=True, exist_ok=True)
        # -ffp-contract=off: no fused multiply-add, closest to the M33 build
        subprocess.run(
            ["g++", "-O2", "-ffp-contract=off", "-shared", "-fPIC", "-std=gnu++17",
             str(cpp), "-o", str(lib)],  # no -I: glibc has its own <features.h>
            check=True,
        )
    return lib


class Features:
    def __init__(self, src: Path | None = None, build_dir: Path | None = None):
        src = Path(src) if src else _src_dir()
        build_dir = Path(build_dir) if build_dir else HERE / "out" / "lib"
        self.lib = ctypes.CDLL(str(_build(src, build_dir)))
        self.lib.ag_feat_window.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
        self.lib.ag_feat_window.restype = ctypes.c_int
        self.lib.ag_feat_rms_dbfs.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.lib.ag_feat_rms_dbfs.restype = ctypes.c_float
        f, m = ctypes.c_int(), ctypes.c_int()
        self.lib.ag_feat_dims(ctypes.byref(f), ctypes.byref(m))
        self.frames, self.mel = f.value, m.value
        self.window = 16000

    def window_features(self, pcm: np.ndarray) -> np.ndarray:
        """pcm: int16 array of exactly 16000 samples -> (frames, mel) float32."""
        pcm = np.ascontiguousarray(pcm, dtype=np.int16)
        if pcm.shape != (self.window,):
            raise ValueError(f"need {self.window} int16 samples, got {pcm.shape}")
        out = np.empty((self.frames, self.mel), dtype=np.float32)
        rc = self.lib.ag_feat_window(pcm.ctypes.data, self.window, out.ctypes.data)
        if rc != 0:
            raise RuntimeError("ag_feat_window failed")
        return out

    def rms_dbfs(self, pcm: np.ndarray) -> float:
        pcm = np.ascontiguousarray(pcm, dtype=np.int16)
        return float(self.lib.ag_feat_rms_dbfs(pcm.ctypes.data, pcm.size))


def to_int16(x: np.ndarray) -> np.ndarray:
    """float audio in [-1, 1] -> int16 (clipped)."""
    return np.clip(np.round(x * 32767.0), -32768, 32767).astype(np.int16)
