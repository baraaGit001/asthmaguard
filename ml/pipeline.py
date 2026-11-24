"""AsthmaGuard cough model: data -> windows -> features -> DS-CNN -> int8.

Runs inside a Kaggle kernel (ml/make_kernel.py bundles it) where the
datasets in DATASETS.md are mounted under /kaggle/input. It can also run on
any machine that has the same folders (set AG_INPUT=/path/to/input).

Section 7 of docs/PLAN.md:
  * 16 kHz mono, 1 s windows; cough windows start just before a cough onset
  * silent windows dropped; classes balanced
  * split by recording / person (never by window) -> no leakage
  * augmentation: background-noise mixing, gain, time shift
  * features from firmware/src/sensors/features.cpp (via features_lib)
  * small DS-CNN, 2 classes, int8 export + C array + report
"""
from __future__ import annotations

import hashlib
import json
import os
import random
import shutil
import subprocess
import sys
import time
from collections import Counter, defaultdict
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

SR = 16000
WIN = 16000
SEED = 1234

# ------------------------------------------------------------------ config
CFG = dict(
    coughvid_max=3500,          # recordings with cough_detected >= coughvid_min_score
    coughvid_min_score=0.9,
    coswara_max=2500,
    speech_max=4000,            # Speech Commands clips used as "talking"
    max_coughs_per_rec=6,
    esc_windows_per_clip=3,
    bg_windows_per_file=40,     # long background-noise files
    silence_rel_db=35.0,        # drop windows this far below the recording's loudest window
    silence_abs_dbfs=-60.0,     # and anything below this
    aug_copies_pos=2,
    aug_copies_neg=1,
    epochs=40,
    batch=128,
    lr=2e-3,
    threshold=0.80,             # device cough threshold (config.h cough_score_min)
)

ESC_COUGH = "coughing"


# ------------------------------------------------------------------ io
def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def load_audio(path: str | Path) -> np.ndarray:
    """Any format -> float32 mono 16 kHz in [-1, 1] (ffmpeg)."""
    cmd = ["ffmpeg", "-nostdin", "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(SR), "-f", "s16le", "-"]
    try:
        raw = subprocess.run(cmd, capture_output=True, check=True, timeout=60).stdout
    except Exception:
        return np.zeros(0, dtype=np.float32)
    return np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0


def group_split(group: str) -> str:
    """Deterministic split by recording/person id: 70 / 15 / 15."""
    h = int(hashlib.sha1(group.encode()).hexdigest(), 16) % 100
    return "train" if h < 70 else ("val" if h < 85 else "test")


def find_dir(root: Path, name: str, max_depth: int = 6) -> Path | None:
    for dirpath, dirnames, _ in os.walk(root):
        depth = Path(dirpath).relative_to(root).parts
        if len(depth) > max_depth:
            dirnames[:] = []
            continue
        if Path(dirpath).name == name:
            return Path(dirpath)
    return None


# ------------------------------------------------------------------ sources
@dataclass
class Item:
    path: str
    label: int          # 1 cough, 0 noise
    source: str         # coughvid / coswara / esc:<class> / speech / bg
    group: str          # recording or person id
    kind: str = "clip"  # cough (find onsets) | clip (random windows) | long


def discover(root: Path) -> list[Item]:
    rng = random.Random(SEED)
    items: list[Item] = []

    d = find_dir(root, "public_dataset_v3") or find_dir(root, "public_dataset")
    if d:
        recs = []
        for js in d.rglob("*.json"):
            try:
                meta = json.loads(js.read_text())
            except Exception:
                continue
            if float(meta.get("cough_detected", 0) or 0) < CFG["coughvid_min_score"]:
                continue
            for ext in (".webm", ".ogg", ".wav"):
                a = js.with_suffix(ext)
                if a.exists():
                    recs.append(a)
                    break
        rng.shuffle(recs)
        for a in recs[: CFG["coughvid_max"]]:
            items.append(Item(str(a), 1, "coughvid", "cv:" + a.stem, "cough"))
        log(f"coughvid: {len(recs)} usable, using {min(len(recs), CFG['coughvid_max'])}")
    else:
        log("coughvid: NOT FOUND")

    d = find_dir(root, "coswara_data")
    if d:
        recs = sorted(d.rglob("cough*.wav"))
        rng.shuffle(recs)
        for a in recs[: CFG["coswara_max"]]:
            items.append(Item(str(a), 1, "coswara", "cw:" + a.parent.name, "cough"))
        log(f"coswara: {len(recs)} files")
    else:
        log("coswara: NOT FOUND")

    d = find_dir(root, "ESC-50-master")
    if d:
        wavs = sorted(d.rglob("*.wav"))
        # filename fold-clipid-take-target.wav; class names from the csv when present
        names = {}
        for csv in d.rglob("*.csv"):
            try:
                import csv as _csv
                with open(csv) as f:
                    for row in _csv.DictReader(f):
                        if "filename" in row and "category" in row:
                            names[row["filename"]] = row["category"]
            except Exception:
                pass
        for w in wavs:
            cat = names.get(w.name, "")
            target = w.stem.split("-")[-1]
            if not cat:
                cat = "coughing" if target == "24" else f"class{target}"
            src_id = "-".join(w.stem.split("-")[:2])
            if cat == ESC_COUGH:
                items.append(Item(str(w), 1, "esc:coughing", "esc:" + src_id, "cough"))
            else:
                items.append(Item(str(w), 0, "esc:" + cat, "esc:" + src_id, "clip"))
        log(f"esc50: {len(wavs)} clips")
    else:
        log("esc50: NOT FOUND")

    # Speech Commands: talking (words) + its long background-noise files
    sc = None
    for cand in ("speech_commands_v0.02", "speech_commands", "speech-commands"):
        sc = find_dir(root, cand)
        if sc:
            break
    if sc is None:
        bgdir = find_dir(root, "_background_noise_")
        sc = bgdir.parent if bgdir else None
    if sc:
        wavs = [w for w in sc.rglob("*.wav") if "_background_noise_" not in w.parts]
        rng.shuffle(wavs)
        for w in wavs[: CFG["speech_max"]]:
            speaker = w.stem.split("_nohash_")[0]
            items.append(Item(str(w), 0, "speech", "sc:" + speaker, "clip"))
        for w in sc.rglob("_background_noise_/*.wav"):
            items.append(Item(str(w), 0, "bg:" + w.stem, "bg:" + w.stem, "long"))
        log(f"speech commands: {len(wavs)} clips")
    else:
        log("speech commands: NOT FOUND")

    # Team's own on-wrist INMP441 recordings (section 7.6): wrist/cough/*.wav,
    # wrist/noise/*.wav. File name prefix before "_" = person (split key).
    d = find_dir(root, "wrist")
    if d:
        n = 0
        for label, sub in ((1, "cough"), (0, "noise")):
            for w in sorted((d / sub).glob("*.wav")) if (d / sub).is_dir() else []:
                items.append(Item(str(w), label, "wrist:" + sub, "wr:" + w.stem.split("_")[0],
                                  "cough" if label else "clip"))
                n += 1
        log(f"wrist recordings: {n}")
    return items


# ------------------------------------------------------------------ windows
def frame_db(x: np.ndarray, hop: int = 320, win: int = 640) -> np.ndarray:
    if x.size < win:
        return np.array([-120.0])
    n = 1 + (x.size - win) // hop
    idx = np.arange(n)[:, None] * hop + np.arange(win)[None, :]
    e = np.mean(x[idx] ** 2, axis=1)
    return 10 * np.log10(e + 1e-12)


def cough_onsets(x: np.ndarray, max_n: int) -> list[int]:
    """Sample positions of cough onsets (rising energy after >= 150 ms quiet)."""
    db = frame_db(x)
    if db.size < 3:
        return []
    peak = db.max()
    floor = np.percentile(db, 20)
    thr = max(peak - 25.0, floor + 10.0)
    on, quiet = [], 8  # 8 frames x 20 ms = 160 ms
    below = quiet
    for i, v in enumerate(db):
        if v >= thr:
            if below >= quiet:
                on.append(i * 320)
            below = 0
        else:
            below += 1
    # loudest first
    on.sort(key=lambda s: -db[min(s // 320 + 5, db.size - 1)])
    return sorted(on[:max_n])


def crop(x: np.ndarray, start: int) -> np.ndarray:
    start = max(0, start)
    w = x[start:start + WIN]
    if w.size < WIN:
        w = np.pad(w, (0, WIN - w.size))
    return w


def rms_db(x: np.ndarray) -> float:
    return float(10 * np.log10(np.mean(x * x) + 1e-12))


def windows_for(item: Item, split: str, seed: int) -> list[np.ndarray]:
    """1 s float windows for one source file."""
    rng = np.random.default_rng(seed)
    x = load_audio(item.path)
    if x.size < SR // 4:
        return []
    peak = np.max(np.abs(x))
    if peak > 0:
        x = x * (0.7 / peak)  # normalise recording level; gain augmentation comes later
    out = []
    if item.kind == "cough":
        for s in cough_onsets(x, CFG["max_coughs_per_rec"]):
            pre = int(SR * (rng.uniform(0.08, 0.35) if split == "train" else 0.2))
            out.append(crop(x, s - pre))
    else:
        n = CFG["bg_windows_per_file"] if item.kind == "long" else CFG["esc_windows_per_clip"]
        if x.size <= WIN:
            out.append(crop(x, 0))
        else:
            for _ in range(n):
                out.append(crop(x, int(rng.integers(0, x.size - WIN))))
    best = max((rms_db(w) for w in out), default=-120)
    keep = [w for w in out if rms_db(w) > max(best - CFG["silence_rel_db"], CFG["silence_abs_dbfs"])]
    return keep


def _work(args):
    item, split, seed = args
    try:
        ws = windows_for(item, split, seed)
    except Exception as e:  # a broken file must not kill the run
        return item, split, [], str(e)
    return item, split, [w.astype(np.float32) for w in ws], None


# ------------------------------------------------------------------ dataset
@dataclass
class Split:
    x: list = field(default_factory=list)       # float windows
    y: list = field(default_factory=list)
    src: list = field(default_factory=list)


def build_windows(items: list[Item]) -> dict[str, Split]:
    splits = {k: Split() for k in ("train", "val", "test")}
    jobs = [(it, group_split(it.group), SEED + i) for i, it in enumerate(items)]
    errors = 0
    with ProcessPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for n, (item, split, ws, err) in enumerate(ex.map(_work, jobs, chunksize=16)):
            if err:
                errors += 1
            s = splits[split]
            for w in ws:
                s.x.append(w)
                s.y.append(item.label)
                s.src.append(item.source)
            if n % 1000 == 0:
                log(f"  windows: {n}/{len(jobs)} files")
    log(f"decode errors: {errors}")
    for k, s in splits.items():
        log(f"{k}: {len(s.y)} windows, cough={sum(s.y)} noise={len(s.y) - sum(s.y)}")
    return splits


def augment(split: Split, rng: np.random.Generator) -> Split:
    """Background mixing + gain. Time shift already happened in windows_for."""
    noise = [w for w, y in zip(split.x, split.y) if y == 0]
    out = Split(list(split.x), list(split.y), list(split.src))
    for w, y, s in zip(split.x, split.y, split.src):
        copies = CFG["aug_copies_pos"] if y == 1 else CFG["aug_copies_neg"]
        for _ in range(copies):
            a = w.copy()
            if noise and rng.random() < (0.7 if y == 1 else 0.3):
                n = noise[int(rng.integers(len(noise)))]
                snr = rng.uniform(0, 20)
                ps, pn = np.mean(a * a) + 1e-12, np.mean(n * n) + 1e-12
                a = a + n * np.sqrt(ps / (pn * 10 ** (snr / 10)))
            a = a * 10 ** (rng.uniform(-18, 3) / 20)
            out.x.append(np.clip(a, -1, 1).astype(np.float32))
            out.y.append(y)
            out.src.append(s + "+aug")
    return out


def balance(split: Split, rng: np.random.Generator) -> Split:
    pos = [i for i, y in enumerate(split.y) if y == 1]
    neg = [i for i, y in enumerate(split.y) if y == 0]
    n = min(len(pos), len(neg))
    keep = sorted(list(rng.choice(pos, n, replace=False)) + list(rng.choice(neg, n, replace=False)))
    return Split([split.x[i] for i in keep], [split.y[i] for i in keep], [split.src[i] for i in keep])


def featurize(feat, split: Split) -> tuple[np.ndarray, np.ndarray]:
    from features_lib import to_int16
    X = np.stack([feat.window_features(to_int16(w)) for w in split.x]).astype(np.float32)
    return X[..., None], np.asarray(split.y, dtype=np.int64)


# ------------------------------------------------------------------ model
def build_model(input_shape=(49, 40, 1), width=64, blocks=4):
    import tensorflow as tf
    from tensorflow.keras import layers as L

    inp = L.Input(shape=input_shape)
    x = L.Conv2D(width, (10, 4), strides=(2, 2), padding="same", use_bias=False)(inp)
    x = L.BatchNormalization()(x)
    x = L.ReLU()(x)
    for _ in range(blocks):
        x = L.DepthwiseConv2D((3, 3), padding="same", use_bias=False)(x)
        x = L.BatchNormalization()(x)
        x = L.ReLU()(x)
        x = L.Conv2D(width, (1, 1), use_bias=False)(x)
        x = L.BatchNormalization()(x)
        x = L.ReLU()(x)
    x = L.GlobalAveragePooling2D()(x)
    x = L.Dropout(0.3)(x)
    out = L.Dense(2, activation="softmax")(x)
    return tf.keras.Model(inp, out)


def spec_augment(x, rng: np.random.Generator):
    x = x.copy()
    for i in range(x.shape[0]):
        if rng.random() < 0.5:
            f0 = int(rng.integers(0, 40 - 6)); x[i, :, f0:f0 + int(rng.integers(1, 6)), 0] = 0
        if rng.random() < 0.5:
            t0 = int(rng.integers(0, 49 - 8)); x[i, t0:t0 + int(rng.integers(1, 8)), :, 0] = 0
    return x


def train(Xtr, ytr, Xva, yva, out: Path):
    import tensorflow as tf

    tf.random.set_seed(SEED)
    rng = np.random.default_rng(SEED)
    model = build_model()
    model.compile(optimizer=tf.keras.optimizers.Adam(CFG["lr"]),
                  loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    model.summary(print_fn=log)
    best, best_w, patience = -1.0, None, 0
    steps = int(np.ceil(len(ytr) / CFG["batch"]))
    sched = tf.keras.optimizers.schedules.CosineDecay(CFG["lr"], CFG["epochs"] * steps)
    model.optimizer.learning_rate = sched
    for ep in range(CFG["epochs"]):
        idx = rng.permutation(len(ytr))
        Xa = spec_augment(Xtr[idx], rng)
        h = model.fit(Xa, ytr[idx], batch_size=CFG["batch"], epochs=1, verbose=0)
        va = model.evaluate(Xva, yva, verbose=0)[1]
        log(f"epoch {ep + 1}/{CFG['epochs']} loss={h.history['loss'][0]:.4f} "
            f"acc={h.history['accuracy'][0]:.4f} val_acc={va:.4f}")
        if va > best:
            best, best_w, patience = va, model.get_weights(), 0
        else:
            patience += 1
            if patience >= 10:
                log("early stop")
                break
    model.set_weights(best_w)
    model.save(out / "model.keras")
    return model


# ------------------------------------------------------------------ eval/export
def confusion(y, p):
    m = np.zeros((2, 2), dtype=int)
    for a, b in zip(y, p):
        m[a, b] += 1
    return m


def report_block(name, y, prob, src, thr):
    pred_arg = (prob >= 0.5).astype(int)
    pred_thr = (prob >= thr).astype(int)
    cm = confusion(y, pred_arg)
    acc = float((pred_arg == y).mean())
    neg = y == 0
    pos = y == 1
    per_src = defaultdict(lambda: [0, 0])
    for s, yy, pp in zip(src, y, pred_thr):
        if yy == 0:
            per_src[s][0] += 1
            per_src[s][1] += int(pp)
    worst = sorted(((v[1] / v[0], k, v[0]) for k, v in per_src.items() if v[0] >= 5), reverse=True)[:10]
    return {
        "name": name,
        "accuracy": acc,
        "confusion_argmax": cm.tolist(),  # rows = true (noise, cough), cols = predicted
        "recall_cough_at_thr": float(pred_thr[pos].mean()) if pos.any() else None,
        "false_cough_rate_on_noise_at_thr": float(pred_thr[neg].mean()) if neg.any() else None,
        "worst_noise_sources_at_thr": [{"source": k, "false_rate": r, "n": n} for r, k, n in worst],
    }


def to_tflite_int8(model, Xrep: np.ndarray, out: Path) -> bytes:
    import tensorflow as tf

    def rep():
        for i in range(min(500, len(Xrep))):
            yield [Xrep[i:i + 1].astype(np.float32)]

    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.representative_dataset = rep
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.inference_input_type = tf.int8
    conv.inference_output_type = tf.int8
    data = conv.convert()
    (out / "model.tflite").write_bytes(data)
    return data


def tflite_predict(data: bytes, X: np.ndarray):
    import tensorflow as tf

    it = tf.lite.Interpreter(model_content=data)
    it.allocate_tensors()
    inp, outp = it.get_input_details()[0], it.get_output_details()[0]
    s_in, z_in = inp["quantization"]
    s_out, z_out = outp["quantization"]
    probs, raw_in, raw_out = [], [], []
    for i in range(len(X)):
        q = np.clip(np.round(X[i:i + 1] / s_in + z_in), -128, 127).astype(np.int8)
        it.set_tensor(inp["index"], q)
        it.invoke()
        o = it.get_tensor(outp["index"])
        raw_in.append(q[0])
        raw_out.append(o[0])
        probs.append((o[0].astype(np.float32) - z_out) * s_out)
    return np.array(probs), (s_in, int(z_in), s_out, int(z_out)), np.array(raw_in), np.array(raw_out)


def c_array(name: str, data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 16):
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return "\n".join(lines)


def export_c(data: bytes, qp, out: Path, golden_in, golden_out):
    s_in, z_in, s_out, z_out = qp
    h = f"""// Generated by ml/pipeline.py. Do not edit.
#pragma once
#include <stdint.h>

extern const unsigned char g_cough_model[];
extern const unsigned int g_cough_model_len;

// Input: int8 [1, 49, 40, 1] = round(feature / scale) + zero_point
constexpr float kCoughInScale = {s_in!r}f;
constexpr int kCoughInZero = {z_in};
// Output: int8 [1, 2] (noise, cough); prob = (q - zero_point) * scale
constexpr float kCoughOutScale = {s_out!r}f;
constexpr int kCoughOutZero = {z_out};
"""
    cc = f"""// Generated by ml/pipeline.py. Do not edit.
#include "model_data.h"

alignas(16) const unsigned char g_cough_model[] = {{
{c_array("g_cough_model", data)}
}};
const unsigned int g_cough_model_len = {len(data)};
"""
    (out / "model_data.h").write_text(h)
    (out / "model_data.cc").write_text(cc)
    # Golden vectors: quantised inputs and the TFLite reference outputs, used
    # by the firmware host test to check the on-device inference bit for bit.
    n = len(golden_in)
    g = ["// Generated by ml/pipeline.py: TFLite reference outputs for host tests.",
         "#pragma once", "#include <stdint.h>", "#define AG_HAVE_GOLDEN 1", f"constexpr int kGoldenN = {n};",
         "static const int8_t kGoldenIn[][49 * 40] = {"]
    for v in golden_in:
        g.append("  {" + ",".join(str(int(t)) for t in v.reshape(-1)) + "},")
    g.append("};")
    g.append("static const int8_t kGoldenOut[][2] = {")
    for v in golden_out:
        g.append("  {" + ",".join(str(int(t)) for t in v.reshape(-1)) + "},")
    g.append("};")
    (out / "golden.h").write_text("\n".join(g) + "\n")


def main():
    out = Path(os.environ.get("AG_OUT", "/kaggle/working"))
    root = Path(os.environ.get("AG_INPUT", "/kaggle/input"))
    out.mkdir(parents=True, exist_ok=True)
    if not shutil.which("ffmpeg"):
        sys.exit("ffmpeg is required")
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from features_lib import Features

    feat = Features(build_dir=out / "lib")
    random.seed(SEED)
    np.random.seed(SEED)
    rng = np.random.default_rng(SEED)

    items = discover(root)
    log("items by source:", dict(Counter(i.source.split(":")[0] for i in items)))
    splits = build_windows(items)

    tr = balance(augment(splits["train"], rng), rng)
    va = balance(splits["val"], rng)
    te_bal = balance(splits["test"], rng)
    te_all = splits["test"]  # unbalanced: every noise window counts for the false-cough rate
    log(f"train={len(tr.y)} val={len(va.y)} test_bal={len(te_bal.y)} test_all={len(te_all.y)}")

    Xtr, ytr = featurize(feat, tr)
    Xva, yva = featurize(feat, va)
    Xte, yte = featurize(feat, te_bal)
    Xta, yta = featurize(feat, te_all)

    model = train(Xtr, ytr, Xva, yva, out)
    p_bal = model.predict(Xte, verbose=0)[:, 1]
    p_all = model.predict(Xta, verbose=0)[:, 1]

    data = to_tflite_int8(model, Xtr, out)
    q_bal, qp, _, _ = tflite_predict(data, Xte)
    q_all, _, _, _ = tflite_predict(data, Xta)
    gi = np.concatenate([np.where(yte == 1)[0][:8], np.where(yte == 0)[0][:8]])
    _, _, g_in, g_out = tflite_predict(data, Xte[gi])
    export_c(data, qp, out, g_in, g_out)

    thr = CFG["threshold"]
    rep = {
        "config": CFG,
        "counts": {k: {"windows": len(s.y), "cough": int(sum(s.y))} for k, s in splits.items()},
        "train_windows_after_aug_balance": len(tr.y),
        "float_test_balanced": report_block("float/test_balanced", yte, p_bal, te_bal.src, thr),
        "int8_test_balanced": report_block("int8/test_balanced", yte, q_bal[:, 1], te_bal.src, thr),
        "int8_test_all": report_block("int8/test_all", yta, q_all[:, 1], te_all.src, thr),
        "model_bytes": len(data),
        "params": int(model.count_params()),
        "quant": {"in_scale": qp[0], "in_zero": qp[1], "out_scale": qp[2], "out_zero": qp[3]},
    }
    (out / "report.json").write_text(json.dumps(rep, indent=2))
    log(json.dumps({k: rep[k] for k in ("int8_test_balanced", "model_bytes", "params")}, indent=2))
    log("DONE")


if __name__ == "__main__":
    main()
