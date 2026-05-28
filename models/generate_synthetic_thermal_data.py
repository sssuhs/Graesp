"""Generate synthetic thermal-overload data for GraEsp model pretraining.

The output is simulation data, not measured experiment data. It should be used
for firmware/model pipeline development and pretraining only. Real experiment
data must be collected later for calibration and final evaluation.
"""

from __future__ import annotations

import argparse
import csv
import math
import random
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "data" / "synthetic" / "thermal_overload_synthetic_v0.csv"


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def state_from_current_and_rise(current_a: float, temp_rise_c: float, heating_rate: float) -> str:
    if current_a >= 10.0 and (temp_rise_c >= 12.0 or heating_rate >= 0.18):
        return "overload"
    if current_a >= 9.0 or temp_rise_c >= 10.0 or heating_rate >= 0.12:
        return "warning"
    return "normal"


def current_profile(run_index: int, rng: random.Random) -> tuple[str, float]:
    profiles = [
        ("normal_low", rng.uniform(2.0, 6.5)),
        ("normal_high", rng.uniform(6.5, 8.8)),
        ("near_rated", rng.uniform(8.8, 9.8)),
        ("rated_edge", rng.uniform(9.8, 10.5)),
        ("overload", rng.uniform(10.5, 13.5)),
    ]
    weights = [0.18, 0.26, 0.26, 0.18, 0.12]
    choice = rng.choices(range(len(profiles)), weights=weights, k=1)[0]
    name, current = profiles[choice]

    # Make every fifth run include a step change, useful for heating-rate learning.
    if run_index % 5 == 0:
        name = "step_" + name
    return name, current


def generate_dataset(output_path: Path, runs: int, seed: int) -> None:
    rng = random.Random(seed)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    fieldnames = [
        "source",
        "dataset_version",
        "run_id",
        "sample_index",
        "time_s",
        "profile",
        "current_a",
        "rated_current_a",
        "ambient_c",
        "wire_area_mm2",
        "thermal_tau_s",
        "steady_temp_rise_c",
        "ntc1_c",
        "ntc2_c",
        "ntc_env_c",
        "wire_temp_avg_c",
        "wire_temp_max_c",
        "temp_rise_c",
        "point_diff_c",
        "heating_rate_c_per_min",
        "battery_v",
        "state",
        "overload_label",
    ]

    with output_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        for run_id in range(runs):
            profile, base_current = current_profile(run_id, rng)
            ambient_c = rng.uniform(18.0, 32.0)
            wire_area = rng.choices([0.75, 1.0, 1.5], weights=[0.55, 0.30, 0.15], k=1)[0]

            # Calibrated by the user's first measured anchors:
            # 8 A -> about 9 C rise, 9 A -> about 9-11 C rise on common plug-strip wire.
            base_k = 9.0 / (8.0 * 8.0)
            area_factor = math.sqrt(0.75 / wire_area)
            contact_factor = rng.uniform(0.88, 1.15)
            airflow_factor = rng.uniform(0.82, 1.18)
            k = base_k * area_factor * contact_factor / airflow_factor
            tau_s = rng.uniform(420.0, 1100.0) * (1.0 + (wire_area - 0.75) * 0.25)

            duration_s = rng.randint(1800, 3600)
            sample_period_s = 10
            sample_count = duration_s // sample_period_s + 1

            last_temp_rise = 0.0
            battery_start = rng.uniform(3.65, 4.15)
            battery_drop = rng.uniform(0.005, 0.035)

            for sample_index in range(sample_count):
                t = sample_index * sample_period_s

                current_a = base_current
                if profile.startswith("step_"):
                    if t < duration_s * 0.35:
                        current_a = rng.uniform(3.0, 6.5)
                    else:
                        current_a = base_current
                current_a += rng.gauss(0.0, 0.08)
                current_a = clamp(current_a, 0.0, 15.0)

                steady_rise = k * current_a * current_a
                warmup_factor = 1.0 - math.exp(-t / tau_s)
                temp_rise = steady_rise * warmup_factor

                # Slow ambient drift and measurement uncertainty.
                ambient_now = ambient_c + 0.6 * math.sin(t / 1800.0 + run_id * 0.17) + rng.gauss(0.0, 0.08)
                temp_rise += rng.gauss(0.0, 0.25)
                temp_rise = max(0.0, temp_rise)

                ntc_skew = rng.gauss(0.0, 0.35)
                ntc1_c = ambient_now + temp_rise + ntc_skew + rng.gauss(0.0, 0.18)
                ntc2_c = ambient_now + temp_rise - ntc_skew + rng.gauss(0.0, 0.18)
                ntc_env_c = ambient_now + rng.gauss(0.0, 0.12)

                wire_avg = (ntc1_c + ntc2_c) * 0.5
                wire_max = max(ntc1_c, ntc2_c)
                measured_rise = wire_max - ntc_env_c
                point_diff = abs(ntc1_c - ntc2_c)

                if sample_index == 0:
                    heating_rate = 0.0
                else:
                    heating_rate = (measured_rise - last_temp_rise) / (sample_period_s / 60.0)
                last_temp_rise = measured_rise

                state = state_from_current_and_rise(current_a, measured_rise, heating_rate)
                # For the current board calibration, do not label low temperature
                # rise as overload. The user measured 9 A around 7.5-11 C rise,
                # so overload risk is gated by both current and thermal evidence.
                overload_label = 1 if current_a >= 10.0 and measured_rise >= 10.0 else 0
                battery_v = battery_start - battery_drop * (t / max(duration_s, 1))

                writer.writerow({
                    "source": "synthetic",
                    "dataset_version": "thermal_rc_v0_user_anchors_8A_9C_9A_9to11C",
                    "run_id": run_id,
                    "sample_index": sample_index,
                    "time_s": t,
                    "profile": profile,
                    "current_a": f"{current_a:.3f}",
                    "rated_current_a": "10.000",
                    "ambient_c": f"{ambient_now:.3f}",
                    "wire_area_mm2": f"{wire_area:.2f}",
                    "thermal_tau_s": f"{tau_s:.2f}",
                    "steady_temp_rise_c": f"{steady_rise:.3f}",
                    "ntc1_c": f"{ntc1_c:.3f}",
                    "ntc2_c": f"{ntc2_c:.3f}",
                    "ntc_env_c": f"{ntc_env_c:.3f}",
                    "wire_temp_avg_c": f"{wire_avg:.3f}",
                    "wire_temp_max_c": f"{wire_max:.3f}",
                    "temp_rise_c": f"{measured_rise:.3f}",
                    "point_diff_c": f"{point_diff:.3f}",
                    "heating_rate_c_per_min": f"{heating_rate:.3f}",
                    "battery_v": f"{battery_v:.3f}",
                    "state": state,
                    "overload_label": overload_label,
                })


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--runs", type=int, default=180)
    parser.add_argument("--seed", type=int, default=20260526)
    args = parser.parse_args()

    generate_dataset(args.output, args.runs, args.seed)
    print(f"generated {args.output}")


if __name__ == "__main__":
    main()
