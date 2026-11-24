"""Build the Hugging Face release folder for the cough model.

  python ml/hf_release.py <kaggle_output_dir> <release_dir> --repo <user>/asthmaguard-cough
  hf upload <user>/asthmaguard-cough <release_dir> .

The release contains the trained model (Keras, int8 TFLite, C array for the
Pico), the TFLite reference vectors, the test report, and the complete
training code (including the C++ feature extractor shared with the
device code), with a model card generated from report.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

CODE = {
    "training/pipeline.py": HERE / "pipeline.py",
    "training/features_lib.py": HERE / "features_lib.py",
    "training/features_ref.py": HERE / "features_ref.py",
    "training/make_kernel.py": HERE / "make_kernel.py",
    "training/wait_kernel.sh": HERE / "wait_kernel.sh",
    "training/tests/test_features.py": HERE / "tests/test_features.py",
    "training/tests/test_pipeline_smoke.py": HERE / "tests/test_pipeline_smoke.py",
    "training/DATASETS.md": HERE / "DATASETS.md",
    "features/features.h": ROOT / "device/src/core/sensors/features.h",
    "features/features.cpp": ROOT / "device/src/core/sensors/features.cpp",
    "device/cough_model.cpp": ROOT / "device/src/hw/cough_model.cpp",
    "device/audio_alsa.cpp": ROOT / "device/src/hw/audio_alsa.cpp",
    "device/test_model.cpp": ROOT / "device/tests/test_model.cpp",
}
MODEL = ["model.keras", "model.tflite", "model_data.cc", "model_data.h", "golden.h", "report.json"]


def pct(x):
    return "—" if x is None else f"{100 * x:.1f}%"


def card(rep: dict, repo: str, github: str, sha: dict) -> str:
    b = rep["int8_test_balanced"]
    a = rep["int8_test_all"]
    f = rep["float_test_balanced"]
    cm = b["confusion_argmax"]
    worst = "\n".join(
        f"| {w['source']} | {pct(w['false_rate'])} | {w['n']} |" for w in a["worst_noise_sources_at_thr"]
    ) or "| — | — | — |"
    counts = rep["counts"]
    files = "\n".join(f"| `{k}` | `{v[:16]}…` |" for k, v in sha.items())
    return f"""---
license: cc-by-nc-4.0
library_name: tflite
pipeline_tag: audio-classification
tags:
- audio-classification
- cough-detection
- tinyml
- tflite-micro
- int8
- raspberry-pi
- asthma
datasets:
- coughvid
- coswara
- esc50
- speech_commands
metrics:
- accuracy
---

# AsthmaGuard cough detector (int8 DS-CNN for the Raspberry Pi Zero 2 W)

Tiny cough / not-cough classifier that runs on the wrist of the **AsthmaGuard** prototype
(Raspberry Pi Zero 2 W, INMP441 I2S microphone, TensorFlow Lite Micro).
Code, device software and app: **{github}** (MIT).

> **Not a medical device.** Student research prototype, not clinically validated.
> Accuracy below is on public phone/web recordings, **not** on the wrist microphone.

## Model

- **Architecture:** DS-CNN (depthwise-separable CNN, as in *Hello Edge: Keyword Spotting on Microcontrollers*), trained from scratch.
  Conv 10×4 /2 (64) → 4 × [depthwise 3×3 → pointwise 64] (BN + ReLU) → global average pool → dropout 0.3 → dense 2 (softmax).
- **Parameters:** {rep['params']:,} · **int8 TFLite size:** {rep['model_bytes'] / 1024:.1f} KB (full int8, int8 input/output).
- **Input:** 1 s of 16 kHz mono audio → 49 × 40 log-mel (frame 512, hop 320, Hann, 40 HTK mel bands 20–8000 Hz, ln(E + 1e-6), window mean subtracted).
  The **same C++ file** (`features/features.cpp`) computes the features on the device and in training (`training/features_lib.py` loads it with ctypes), and `training/tests/test_features.py` checks it against an independent numpy implementation.
- **Input quantization:** q = round(feature / {rep['quant']['in_scale']:.6g}) + {rep['quant']['in_zero']}.
- **Output:** int8 [noise, cough]; P(cough) = (q − {rep['quant']['out_zero']}) × {rep['quant']['out_scale']:.6g}. The device counts a cough event at P ≥ {rep['config']['threshold']}.

## Results (held-out test split: people / recordings never seen in training)

| | float | **int8 (on device)** |
|---|---|---|
| Accuracy, balanced test set ({sum(sum(r) for r in cm)} windows) | {pct(f['accuracy'])} | **{pct(b['accuracy'])}** |
| Cough recall at P ≥ {rep['config']['threshold']} | {pct(f['recall_cough_at_thr'])} | {pct(b['recall_cough_at_thr'])} |
| False-cough rate on noise at P ≥ {rep['config']['threshold']} | {pct(f['false_cough_rate_on_noise_at_thr'])} | {pct(b['false_cough_rate_on_noise_at_thr'])} |
| False-cough rate on **all** test noise windows ({counts['test']['windows'] - counts['test']['cough']}) | | {pct(a['false_cough_rate_on_noise_at_thr'])} |

Confusion matrix, int8, balanced test set (argmax):

| true \\ predicted | noise | cough |
|---|---|---|
| noise | {cm[0][0]} | {cm[0][1]} |
| cough | {cm[1][0]} | {cm[1][1]} |

Noise sources most often mistaken for a cough (int8, all test windows, P ≥ {rep['config']['threshold']}):

| source | false-cough rate | windows |
|---|---|---|
{worst}

## Training data

| Dataset | Used as | License |
|---|---|---|
| COUGHVID v3 (Orlandic et al., 2021), recordings with `cough_detected` ≥ 0.9 | cough | CC BY 4.0 |
| Coswara (Sharma et al., 2020), heavy-cough recordings | cough | CC BY 4.0 |
| ESC-50 (Piczak, 2015): 49 non-cough classes / class "coughing" | noise / cough | **CC BY-NC 3.0** |
| Google Speech Commands v0.02 (Warden, 2018): words + background noise | noise | CC BY 4.0 |

Windows: train {counts['train']['windows']:,} · val {counts['val']['windows']:,} · test {counts['test']['windows']:,}
(cough windows start just before a detected cough onset; silent windows dropped; split by recording / person, 70/15/15).
Augmentation: background-noise mixing at 0–20 dB SNR, −18…+3 dB gain, onset shift, SpecAugment-style masks; classes balanced.

Because ESC-50 is non-commercial, these **weights are CC BY-NC 4.0**. The code is MIT.

## Files

| File | What |
|---|---|
| `model.tflite` | full-int8 model for TensorFlow Lite (Micro) |
| `model_data.cc`, `model_data.h` | the same model as a C array + quantization constants (drop into `device/model/`) |
| `golden.h` | int8 inputs + TFLite reference outputs; `device/tests/test_model.cpp` and the device `SELFTEST` command check the on-device result against them |
| `model.keras` | float Keras model (fine-tuning) |
| `report.json` | full evaluation report |
| `training/` | complete pipeline: data discovery, onset windows, splits, augmentation, training, int8 export (`python training/make_kernel.py --push --user <kaggle-user>` runs it as a Kaggle kernel) |
| `features/` | the C++ log-mel extractor shared by device and training |
| `device/` | how the Zero 2 W runs it (audio thread: ALSA I2S capture → features → TFLM every 250 ms) and the golden-vector test |

SHA-256:

| file | sha256 |
|---|---|
{files}

## Limitations

- Trained on phone/web recordings. A wrist-worn INMP441 hears coughs differently (sleeve, distance, body-conducted sound); the team must record on-wrist audio and re-test (pipeline reads `wrist/cough/*.wav`, `wrist/noise/*.wav`).
- Cough only. Wheeze (the most specific asthma sound) needs a chest sensor and is future work.
- Throat clearing has no clean public source in the training data and may be confused with coughs.

## Citation

```
@misc{{asthmaguard2026,
  title  = {{AsthmaGuard: a wrist-worn asthma-attack detector with on-device cough detection}},
  year   = {{2026}},
  howpublished = {{\\url{{{github}}}}},
  note   = {{Model: https://huggingface.co/{repo}}}
}}
```
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kaggle_out", type=Path)
    ap.add_argument("release", type=Path)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--github", default="https://github.com/baraaGit001/asthmaguard")
    args = ap.parse_args()

    rel = args.release
    if rel.exists():
        shutil.rmtree(rel)
    rel.mkdir(parents=True)
    sha = {}
    for name in MODEL:
        src = args.kaggle_out / name
        if not src.exists():
            raise SystemExit(f"missing {src}")
        shutil.copy2(src, rel / name)
        sha[name] = hashlib.sha256(src.read_bytes()).hexdigest()
    for dst, src in CODE.items():
        (rel / dst).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, rel / dst)
    shutil.copy2(ROOT / "LICENSE", rel / "LICENSE-code-MIT")
    rep = json.loads((args.kaggle_out / "report.json").read_text())
    (rel / "README.md").write_text(card(rep, args.repo, args.github, sha))
    print("release ready:", rel)


if __name__ == "__main__":
    main()
