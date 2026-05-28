r"""Receive GraEsp UDP telemetry and save it as CSV.

Run after the ESP32-S3 is connected to the same WiFi network:

    python desktop-app\udp_telemetry_receiver.py
"""

from __future__ import annotations

import argparse
import csv
import json
import socket
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "data" / "logs" / "telemetry_udp.csv"

CSV_FIELDS = [
    "pc_time_s",
    "sender_ip",
    "device_id",
    "uptime_ms",
    "ntc1_c",
    "ntc2_c",
    "ntc_env_c",
    "adc1_mv",
    "adc2_mv",
    "adc_env_mv",
    "cable_avg_c",
    "cable_max_c",
    "ambient_c",
    "temp_rise_c",
    "point_diff_c",
    "heating_rate_c_per_min",
    "estimated_current_a",
    "overload_probability",
    "state",
    "battery_v",
    "battery_percent",
    "battery_state",
    "self_test_ok",
    "self_test_fault_mask",
    "temp_high_count",
    "warning_count",
    "alarm_count",
    "fault_count",
    "low_battery_count",
    "max_temp_rise_c",
    "max_estimated_current_a",
]


def flatten_packet(packet: dict, sender_ip: str) -> dict:
    ntc = packet.get("ntc_c", [None, None, None])
    adc = packet.get("adc_mv", [None, None, None])
    return {
        "pc_time_s": f"{time.time():.3f}",
        "sender_ip": sender_ip,
        "device_id": packet.get("device_id", ""),
        "uptime_ms": packet.get("uptime_ms", ""),
        "ntc1_c": ntc[0] if len(ntc) > 0 else "",
        "ntc2_c": ntc[1] if len(ntc) > 1 else "",
        "ntc_env_c": ntc[2] if len(ntc) > 2 else "",
        "adc1_mv": adc[0] if len(adc) > 0 else "",
        "adc2_mv": adc[1] if len(adc) > 1 else "",
        "adc_env_mv": adc[2] if len(adc) > 2 else "",
        "cable_avg_c": packet.get("cable_avg_c", ""),
        "cable_max_c": packet.get("cable_max_c", ""),
        "ambient_c": packet.get("ambient_c", ""),
        "temp_rise_c": packet.get("temp_rise_c", ""),
        "point_diff_c": packet.get("point_diff_c", ""),
        "heating_rate_c_per_min": packet.get("heating_rate_c_per_min", ""),
        "estimated_current_a": packet.get("estimated_current_a", ""),
        "overload_probability": packet.get("overload_probability", ""),
        "state": packet.get("state", ""),
        "battery_v": packet.get("battery_v", ""),
        "battery_percent": packet.get("battery_percent", ""),
        "battery_state": packet.get("battery_state", ""),
        "self_test_ok": packet.get("self_test_ok", ""),
        "self_test_fault_mask": packet.get("self_test_fault_mask", ""),
        "temp_high_count": packet.get("temp_high_count", ""),
        "warning_count": packet.get("warning_count", ""),
        "alarm_count": packet.get("alarm_count", ""),
        "fault_count": packet.get("fault_count", ""),
        "low_battery_count": packet.get("low_battery_count", ""),
        "max_temp_rise_c": packet.get("max_temp_rise_c", ""),
        "max_estimated_current_a": packet.get("max_estimated_current_a", ""),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=3333)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_header = not args.output.exists() or args.output.stat().st_size == 0

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", args.port))

    print(f"Listening UDP 0.0.0.0:{args.port}")
    print(f"Saving CSV: {args.output}")

    with args.output.open("a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        if write_header:
            writer.writeheader()

        while True:
            data, address = sock.recvfrom(2048)
            text = data.decode("utf-8", errors="replace").strip()
            try:
                packet = json.loads(text)
            except json.JSONDecodeError:
                print(f"{address[0]} invalid json: {text[:120]}")
                continue

            row = flatten_packet(packet, address[0])
            writer.writerow(row)
            f.flush()
            print(
                f"{address[0]} "
                f"state={row['state']} "
                f"I={row['estimated_current_a']}A "
                f"P={row['overload_probability']} "
                f"rise={row['temp_rise_c']}C "
                f"bat={row['battery_v']}V"
            )


if __name__ == "__main__":
    main()
