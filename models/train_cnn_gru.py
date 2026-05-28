"""Train/export the ESP32-S3 edge CNN-GRU weights for GraEsp.

This script intentionally uses only the Python standard library so it can run
on the current Windows setup without installing numpy, torch, or sklearn.

Training strategy for the current firmware:
1. Build 30-sample thermal sequences from synthetic time-series data.
2. Add user measured steady-state anchors as calibration windows.
3. Run the same fixed CNN-GRU temporal feature extractor as firmware.
4. Fit the current regression head with ridge regression.
5. Fit the overload-probability head with logistic gradient descent.
6. Export firmware/main/include/model_weights.h.
"""

from __future__ import annotations

import csv
import math
import random
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SYNTHETIC_CSV = ROOT / "data" / "synthetic" / "thermal_overload_synthetic_v0.csv"
REAL_ANCHOR_CSV = ROOT / "data" / "real" / "real_anchor_thermal_points.csv"
OUTPUT_HEADER = ROOT / "firmware" / "main" / "include" / "model_weights.h"
GENERATOR = ROOT / "models" / "generate_synthetic_thermal_data.py"

SEQ_LEN = 30
FEATURE_COUNT = 7
CONV_CHANNELS = 4
GRU_UNITS = 6

CONV_KERNEL = [
    [
        [0.10, -0.12, 0.00, 0.02, 0.02, -0.02, 0.00],
        [0.20,  0.16, 0.01, 0.03, 0.03, -0.02, 0.00],
        [0.10,  0.12, 0.00, 0.02, 0.02, -0.02, 0.00],
    ],
    [
        [0.02,  0.10, 0.03, 0.00, 0.04, -0.02, 0.00],
        [0.04,  0.20, 0.04, 0.00, 0.06, -0.03, 0.00],
        [0.02,  0.10, 0.03, 0.00, 0.04, -0.02, 0.00],
    ],
    [
        [0.04,  0.02, 0.14, 0.00, 0.02, -0.02, 0.00],
        [0.08,  0.03, 0.22, 0.00, 0.04, -0.04, 0.00],
        [0.04,  0.02, 0.14, 0.00, 0.02, -0.02, 0.00],
    ],
    [
        [0.06, -0.04, 0.01, 0.03, 0.03, -0.06, -0.01],
        [0.10,  0.08, 0.01, 0.04, 0.04, -0.08, -0.02],
        [0.06,  0.10, 0.01, 0.03, 0.03, -0.06, -0.01],
    ],
]
CONV_BIAS = [0.02, 0.01, 0.00, 0.02]
GRU_INPUT_WEIGHT = [
    [ 0.30,  0.18,  0.05,  0.12],
    [ 0.10,  0.35,  0.12,  0.08],
    [ 0.05,  0.08,  0.32,  0.10],
    [ 0.18,  0.12,  0.10,  0.28],
    [-0.10,  0.25,  0.05,  0.18],
    [ 0.22, -0.08,  0.10,  0.20],
]
GRU_RECURRENT_WEIGHT = [0.42, 0.38, 0.35, 0.40, 0.36, 0.34]
GRU_BIAS = [0.00, 0.00, -0.02, 0.00, -0.02, 0.00]


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def sigmoid(value: float) -> float:
    value = clamp(value, -20.0, 20.0)
    return 1.0 / (1.0 + math.exp(-value))


def ensure_synthetic_data() -> None:
    if SYNTHETIC_CSV.exists():
        return
    subprocess.check_call([sys.executable, str(GENERATOR)], cwd=str(ROOT))


def read_synthetic_runs() -> dict[str, list[dict[str, float]]]:
    runs: dict[str, list[dict[str, float]]] = {}
    with SYNTHETIC_CSV.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            run_id = row["run_id"]
            item = {
                "sample_index": float(row["sample_index"]),
                "current_a": float(row["current_a"]),
                "temp_rise_c": float(row["temp_rise_c"]),
                "heating_rate_c_per_min": clamp(float(row["heating_rate_c_per_min"]), -8.0, 8.0),
                "point_diff_c": float(row["point_diff_c"]),
                "wire_temp_avg_c": float(row["wire_temp_avg_c"]),
                "wire_temp_max_c": float(row["wire_temp_max_c"]),
                "ambient_c": float(row["ambient_c"]),
                "battery_v": float(row["battery_v"]),
                "overload_label": float(row["overload_label"]),
            }
            runs.setdefault(run_id, []).append(item)
    for values in runs.values():
        values.sort(key=lambda x: x["sample_index"])
    return runs


def row_features(row: dict[str, float]) -> list[float]:
    return [
        row["temp_rise_c"],
        row["heating_rate_c_per_min"],
        row["point_diff_c"],
        row["wire_temp_avg_c"],
        row["wire_temp_max_c"],
        row["ambient_c"],
        row["battery_v"],
    ]


def build_windows(runs: dict[str, list[dict[str, float]]]) -> list[tuple[list[list[float]], float, float]]:
    windows: list[tuple[list[list[float]], float, float]] = []
    for rows in runs.values():
        if len(rows) < SEQ_LEN:
            continue
        step = 3
        for end in range(SEQ_LEN, len(rows) + 1, step):
            chunk = rows[end - SEQ_LEN:end]
            target = chunk[-1]
            windows.append(([
                row_features(row) for row in chunk
            ], target["current_a"], target["overload_label"]))
    return windows


def add_measured_anchors(windows: list[tuple[list[list[float]], float, float]]) -> None:
    if not REAL_ANCHOR_CSV.exists():
        return
    with REAL_ANCHOR_CSV.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            current = float(row["current_a"])
            ambient = float(row["ambient_c"])
            wire = float(row["wire_temp_c"])
            rise = float(row["temp_rise_c"])
            label = 1.0 if current >= 10.0 and rise >= 10.0 else 0.0
            base = [rise, 0.0, 0.40, wire - 0.2, wire, ambient, 3.85]
            seq = []
            for i in range(SEQ_LEN):
                warm = 0.86 + 0.14 * (i / max(SEQ_LEN - 1, 1))
                seq.append([
                    base[0] * warm,
                    0.08 * (1.0 - i / max(SEQ_LEN - 1, 1)),
                    base[2],
                    ambient + base[0] * warm - 0.2,
                    ambient + base[0] * warm,
                    ambient,
                    base[6],
                ])
            for _ in range(36):
                windows.append((seq, current, label))


def feature_stats(windows: list[tuple[list[list[float]], float, float]]) -> tuple[list[float], list[float]]:
    values = [[] for _ in range(FEATURE_COUNT)]
    for seq, _, _ in windows:
        for row in seq:
            for i, value in enumerate(row):
                values[i].append(value)
    mean = [sum(col) / len(col) for col in values]
    scale = []
    for i, col in enumerate(values):
        var = sum((x - mean[i]) ** 2 for x in col) / max(len(col), 1)
        scale.append(max(math.sqrt(var), 0.001))
    return mean, scale


def normalize_sequence(seq: list[list[float]], mean: list[float], scale: list[float]) -> list[list[float]]:
    return [[clamp((value - mean[i]) / scale[i], -4.0, 4.0) for i, value in enumerate(row)] for row in seq]


def run_temporal_model(seq: list[list[float]]) -> list[float]:
    hidden = [0.0] * GRU_UNITS
    valid_count = len(seq)
    for t in range(valid_count):
        conv = []
        for ch in range(CONV_CHANNELS):
            total = CONV_BIAS[ch]
            for k in range(3):
                idx = t
                if k == 0 and t > 0:
                    idx = t - 1
                elif k == 2 and t + 1 < valid_count:
                    idx = t + 1
                row = seq[idx]
                for f in range(FEATURE_COUNT):
                    total += row[f] * CONV_KERNEL[ch][k][f]
            conv.append(max(0.0, total))
        next_hidden = hidden[:]
        for unit in range(GRU_UNITS):
            candidate = GRU_BIAS[unit] + hidden[unit] * GRU_RECURRENT_WEIGHT[unit]
            for ch in range(CONV_CHANNELS):
                candidate += conv[ch] * GRU_INPUT_WEIGHT[unit][ch]
            gate = sigmoid(0.8 * candidate)
            new_value = math.tanh(candidate)
            next_hidden[unit] = hidden[unit] * (1.0 - gate) + new_value * gate
        hidden = next_hidden
    return hidden


def solve_linear_system(matrix: list[list[float]], vector: list[float]) -> list[float]:
    n = len(vector)
    aug = [matrix[i][:] + [vector[i]] for i in range(n)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(aug[r][col]))
        aug[col], aug[pivot] = aug[pivot], aug[col]
        div = aug[col][col]
        if abs(div) < 1e-12:
            continue
        for j in range(col, n + 1):
            aug[col][j] /= div
        for r in range(n):
            if r == col:
                continue
            factor = aug[r][col]
            for j in range(col, n + 1):
                aug[r][j] -= factor * aug[col][j]
    return [aug[i][n] for i in range(n)]


def fit_ridge(xs: list[list[float]], ys: list[float], ridge: float = 0.4) -> list[float]:
    n_features = len(xs[0]) + 1
    xtx = [[0.0] * n_features for _ in range(n_features)]
    xty = [0.0] * n_features
    for x, y in zip(xs, ys):
        row = [1.0] + x
        for i in range(n_features):
            xty[i] += row[i] * y
            for j in range(n_features):
                xtx[i][j] += row[i] * row[j]
    for i in range(1, n_features):
        xtx[i][i] += ridge
    return solve_linear_system(xtx, xty)


def fit_logistic(xs: list[list[float]], ys: list[float], epochs: int = 1600, lr: float = 0.05) -> list[float]:
    rng = random.Random(20260526)
    weights = [0.0] + [rng.uniform(-0.05, 0.05) for _ in xs[0]]
    pos = sum(ys)
    neg = len(ys) - pos
    pos_weight = neg / max(pos, 1.0)

    for _ in range(epochs):
        grad = [0.0] * len(weights)
        for x, y in zip(xs, ys):
            row = [1.0] + x
            pred = sigmoid(sum(w * v for w, v in zip(weights, row)))
            sample_weight = pos_weight if y >= 0.5 else 1.0
            err = (pred - y) * sample_weight
            for i, value in enumerate(row):
                grad[i] += err * value
        inv_n = 1.0 / len(xs)
        for i in range(len(weights)):
            l2 = 0.002 * weights[i] if i > 0 else 0.0
            weights[i] -= lr * (grad[i] * inv_n + l2)
    return weights


def evaluate(xs: list[list[float]], current_y: list[float], label_y: list[float], current_w: list[float], prob_w: list[float]) -> str:
    mae = 0.0
    correct = 0
    for x, current, label in zip(xs, current_y, label_y):
        pred_current = current_w[0] + sum(w * v for w, v in zip(current_w[1:], x))
        pred_prob = sigmoid(prob_w[0] + sum(w * v for w, v in zip(prob_w[1:], x)))
        mae += abs(pred_current - current)
        correct += int((pred_prob >= 0.5) == (label >= 0.5))
    mae /= max(len(xs), 1)
    acc = correct / max(len(xs), 1)
    return f"train windows={len(xs)} current_mae={mae:.3f}A overload_acc={acc:.3f}"


def c_array(values: list[float], indent: str = "    ") -> str:
    return indent + ", ".join(f"{v:.8f}f" for v in values)


def c_matrix_2d(values: list[list[float]], indent: str = "    ") -> str:
    return ",\n".join(indent + "{" + ", ".join(f"{v:.8f}f" for v in row) + "}" for row in values)


def c_kernel() -> str:
    chunks = []
    for ch in CONV_KERNEL:
        rows = []
        for row in ch:
            rows.append("        {" + ", ".join(f"{v:.8f}f" for v in row) + "}")
        chunks.append("    {\n" + ",\n".join(rows) + "\n    }")
    return ",\n".join(chunks)


def write_header(mean: list[float], scale: list[float], current_w: list[float], prob_w: list[float], metrics: str) -> None:
    text = f"""#pragma once

#include <stdint.h>

// Trained/exported by models/train_cnn_gru.py.
// Data sources: synthetic thermal sequences + user measured steady anchors.
// {metrics}
#define MODEL_PREDICTOR_VERSION \"edge_cnn_gru_trained_v1\"

#define MODEL_SEQUENCE_LENGTH {SEQ_LEN}
#define MODEL_FEATURE_COUNT {FEATURE_COUNT}
#define MODEL_CONV_CHANNELS {CONV_CHANNELS}
#define MODEL_GRU_UNITS {GRU_UNITS}

static const float MODEL_FEATURE_MEAN[MODEL_FEATURE_COUNT] = {{
{c_array(mean)}
}};

static const float MODEL_FEATURE_SCALE[MODEL_FEATURE_COUNT] = {{
{c_array(scale)}
}};

static const float MODEL_CONV_KERNEL[MODEL_CONV_CHANNELS][3][MODEL_FEATURE_COUNT] = {{
{c_kernel()}
}};

static const float MODEL_CONV_BIAS[MODEL_CONV_CHANNELS] = {{
{c_array(CONV_BIAS)}
}};

static const float MODEL_GRU_INPUT_WEIGHT[MODEL_GRU_UNITS][MODEL_CONV_CHANNELS] = {{
{c_matrix_2d(GRU_INPUT_WEIGHT)}
}};

static const float MODEL_GRU_RECURRENT_WEIGHT[MODEL_GRU_UNITS] = {{
{c_array(GRU_RECURRENT_WEIGHT)}
}};

static const float MODEL_GRU_BIAS[MODEL_GRU_UNITS] = {{
{c_array(GRU_BIAS)}
}};

static const float MODEL_CURRENT_HEAD_WEIGHT[MODEL_GRU_UNITS] = {{
{c_array(current_w[1:])}
}};

static const float MODEL_PROB_HEAD_WEIGHT[MODEL_GRU_UNITS] = {{
{c_array(prob_w[1:1 + GRU_UNITS])}
}};

#define MODEL_CURRENT_HEAD_BIAS ({current_w[0]:.8f}f)
#define MODEL_PROB_HEAD_BIAS ({prob_w[0]:.8f}f)
#define MODEL_PROB_CURRENT_WEIGHT ({prob_w[-1]:.8f}f)
"""
    OUTPUT_HEADER.write_text(text, encoding="utf-8", newline="\n")


def main() -> None:
    ensure_synthetic_data()
    runs = read_synthetic_runs()
    windows = build_windows(runs)
    add_measured_anchors(windows)
    if not windows:
        raise SystemExit("no training windows found")

    mean, scale = feature_stats(windows)
    xs = []
    current_y = []
    label_y = []
    for seq, current, label in windows:
        hidden = run_temporal_model(normalize_sequence(seq, mean, scale))
        xs.append(hidden)
        current_y.append(current)
        label_y.append(label)

    current_w = fit_ridge(xs, current_y)
    current_pred = [
        current_w[0] + sum(w * v for w, v in zip(current_w[1:], x))
        for x in xs
    ]
    xs_for_prob = [x + [pred] for x, pred in zip(xs, current_pred)]
    prob_w = fit_logistic(xs_for_prob, label_y)
    metrics = evaluate(xs_for_prob, current_y, label_y, current_w, prob_w)
    write_header(mean, scale, current_w, prob_w, metrics)
    print(metrics)
    print(f"wrote {OUTPUT_HEADER}")


if __name__ == "__main__":
    main()
