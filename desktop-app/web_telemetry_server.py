r"""GraEsp local host dashboard server.

Run on the PC after ESP terminals join the same WiFi network:

    python desktop-app\web_telemetry_server.py

Then open:

    http://127.0.0.1:8080
"""

from __future__ import annotations

import argparse
import csv
import json
import mimetypes
import shutil
import socket
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlencode, urlparse
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]
WEB_ROOT = Path(__file__).resolve().parent / "web"
DEFAULT_OUTPUT = ROOT / "data" / "logs" / "telemetry_udp.csv"
UDP_PORT = 3333
COMMAND_PORT = 3334
MQTT_HOST = "broker.emqx.io"
MQTT_PORT = 1883
MQTT_TOPIC_PREFIX = "graesp/lin"
HTTP_PORT = 8080
HTTP_HOST = "0.0.0.0"
MAX_POINTS_PER_DEVICE = 600
DEVICE_OFFLINE_SECONDS = 8.0
DEVICE_STALE_SECONDS = 3.0
DEFAULT_PROVISION_HOST = "192.168.4.1"
PROVISION_TIMEOUT_SECONDS = 4.0

command_port_runtime = COMMAND_PORT
output_runtime = DEFAULT_OUTPUT

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

devices: dict[str, dict] = {}
points_by_device: dict[str, list[dict]] = {}
events: list[dict] = []
online_by_device: dict[str, bool] = {}
state_lock = threading.Lock()

def mqtt_command_topic(device_id: str | None = None) -> str:
    target = device_id or "broadcast"
    return f"{MQTT_TOPIC_PREFIX}/{target}/cmd"


def mqtt_publish_command(payload: dict, device_id: str | None = None) -> dict:
    exe = shutil.which("mosquitto_pub")
    if exe is None:
        return {"ok": False, "error": "mosquitto_pub not found"}

    topic = mqtt_command_topic(device_id)
    message = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
    try:
        completed = subprocess.run(
            [exe, "-h", MQTT_HOST, "-p", str(MQTT_PORT), "-t", topic, "-m", message],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return {"ok": False, "topic": topic, "error": str(exc)}

    if completed.returncode != 0:
        return {"ok": False, "topic": topic, "error": completed.stderr.strip() or completed.stdout.strip()}
    return {"ok": True, "topic": topic}


def normalize_mqtt_packet(packet: dict) -> dict:
    out = dict(packet)
    out["transport"] = "mqtt"
    out["sender_ip"] = out.get("sender_ip") or "MQTT"
    out["pc_time_s"] = time.time()

    if "ntc_c" not in out:
        out["ntc_c"] = [out.get("ntc1"), out.get("ntc2"), out.get("ambient")]
    if "ambient_c" not in out and "ambient" in out:
        out["ambient_c"] = out.get("ambient")
    if "temp_rise_c" not in out and "rise" in out:
        out["temp_rise_c"] = out.get("rise")
    if "heating_rate_c_per_min" not in out and "rate" in out:
        out["heating_rate_c_per_min"] = out.get("rate")
    if "estimated_current_a" not in out and "current" in out:
        out["estimated_current_a"] = out.get("current")
    if "overload_probability" not in out and "prob" in out:
        out["overload_probability"] = out.get("prob")
    if "battery_percent" not in out and "battery" in out:
        out["battery_percent"] = out.get("battery")
    if "self_test_fault_mask" not in out and "fault_mask" in out:
        out["self_test_fault_mask"] = out.get("fault_mask")
    return out


def device_key(packet: dict) -> str:
    return str(packet.get("device_id") or packet.get("sender_ip") or "unknown")


def csv_row(packet: dict, sender_ip: str) -> dict:
    ntc = packet.get("ntc_c", [None, None, None])
    adc = packet.get("adc_mv", [None, None, None])
    return {
        "pc_time_s": f"{packet.get('pc_time_s', time.time()):.3f}",
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


def event_message(packet: dict, old_state: str | None) -> str | None:
    state = packet.get("state")
    if old_state != state:
        return f"{device_key(packet)} 状态变为 {state}"
    return None


def append_event(device_id: str, state: str, message: str, pc_time_s: float | None = None) -> None:
    events.append({
        "pc_time_s": pc_time_s or time.time(),
        "device_id": device_id,
        "state": state,
        "message": message,
    })
    if len(events) > 120:
        del events[: len(events) - 120]


def ingest_packet(packet: dict) -> None:
    key = device_key(packet)
    with state_lock:
        old_state = devices.get(key, {}).get("state")
        devices[key] = packet
        if online_by_device.get(key) is False:
            append_event(key, "normal", f"{key} 恢复在线", packet["pc_time_s"])
        online_by_device[key] = True
        points = points_by_device.setdefault(key, [])
        points.append(packet)
        if len(points) > MAX_POINTS_PER_DEVICE:
            del points[: len(points) - MAX_POINTS_PER_DEVICE]

        message = event_message(packet, old_state)
        if message is not None:
            append_event(key, packet.get("state", ""), message, packet["pc_time_s"])


def mqtt_worker() -> None:
    exe = shutil.which("mosquitto_sub")
    if exe is None:
        with state_lock:
            append_event("mqtt", "fault", "未找到 mosquitto_sub，MQTT 远程通道未启动")
        return

    topic = f"{MQTT_TOPIC_PREFIX}/+/telemetry"
    while True:
        try:
            proc = subprocess.Popen(
                [exe, "-h", MQTT_HOST, "-p", str(MQTT_PORT), "-t", topic, "-v"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            with state_lock:
                append_event("mqtt", "normal", f"MQTT 订阅已启动 {topic}")

            assert proc.stdout is not None
            for line in proc.stdout:
                line = line.strip()
                if not line:
                    continue
                try:
                    _topic, payload = line.split(" ", 1)
                    packet = normalize_mqtt_packet(json.loads(payload))
                except (ValueError, json.JSONDecodeError):
                    continue
                ingest_packet(packet)

            stderr = ""
            if proc.stderr is not None:
                stderr = proc.stderr.read().strip()
            with state_lock:
                append_event("mqtt", "fault", f"MQTT 订阅断开: {stderr or proc.returncode}")
        except Exception as exc:
            with state_lock:
                append_event("mqtt", "fault", f"MQTT 订阅异常: {exc}")
        time.sleep(5)

def udp_worker(port: int, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    write_header = not output.exists() or output.stat().st_size == 0

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", port))

    with output.open("a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        if write_header:
            writer.writeheader()

        while True:
            data, address = sock.recvfrom(4096)
            try:
                packet = json.loads(data.decode("utf-8", errors="replace").strip())
            except json.JSONDecodeError:
                continue

            packet["sender_ip"] = address[0]
            packet["transport"] = "udp"
            packet["pc_time_s"] = time.time()
            ingest_packet(packet)

            writer.writerow(csv_row(packet, address[0]))
            f.flush()


def enrich(packet: dict | None, now: float) -> dict | None:
    if packet is None:
        return None
    out = dict(packet)
    age_s = now - float(out.get("pc_time_s", now))
    out["age_s"] = age_s
    out["online"] = age_s <= DEVICE_OFFLINE_SECONDS
    if age_s <= DEVICE_STALE_SECONDS:
        out["link_state"] = "online"
        out["link_label"] = "在线"
    elif age_s <= DEVICE_OFFLINE_SECONDS:
        out["link_state"] = "stale"
        out["link_label"] = "信号延迟"
    else:
        out["link_state"] = "offline"
        out["link_label"] = "离线"
    return out


def snapshot_payload(selected_id: str | None = None) -> dict:
    now = time.time()
    with state_lock:
        enriched_devices = [enrich(packet, now) for packet in devices.values()]
        enriched_devices = [d for d in enriched_devices if d is not None]
        for device in enriched_devices:
            key = device_key(device)
            online = bool(device.get("online"))
            old_online = online_by_device.get(key)
            if old_online is not None and old_online != online:
                state = "normal" if online else "offline"
                message = f"{key} 恢复在线" if online else f"{key} 已离线"
                append_event(key, state, message, now)
            elif old_online is None and not online:
                append_event(key, "offline", f"{key} 已离线", now)
            online_by_device[key] = online
        enriched_devices.sort(key=lambda d: float(d.get("pc_time_s", 0)), reverse=True)
        selected = enrich(devices.get(selected_id), now) if selected_id else None
        points = list(points_by_device.get(selected_id or "", []))[-180:]
        recent_events = list(events)[-80:]
    return {
        "devices": enriched_devices,
        "selected": selected,
        "points": points,
        "events": recent_events,
    }


def delete_device(device_id: str) -> dict:
    now = time.time()
    with state_lock:
        existed = device_id in devices or device_id in points_by_device or device_id in online_by_device
        devices.pop(device_id, None)
        points_by_device.pop(device_id, None)
        online_by_device.pop(device_id, None)
        if existed:
            append_event(device_id, "normal", f"主机已删除设备记录 {device_id}", now)
    return {"ok": existed, "device_id": device_id}


def clear_offline_devices() -> dict:
    now = time.time()
    removed: list[str] = []
    with state_lock:
        for key, packet in list(devices.items()):
            enriched = enrich(packet, now)
            if enriched and not enriched.get("online"):
                removed.append(key)
                devices.pop(key, None)
                points_by_device.pop(key, None)
                online_by_device.pop(key, None)
        if removed:
            append_event("host", "normal", f"主机已清除 {len(removed)} 个离线设备记录", now)
    return {"ok": True, "removed": removed, "count": len(removed)}


def normalize_provision_host(host: str | None) -> str:
    value = (host or DEFAULT_PROVISION_HOST).strip()
    if value.startswith("http://"):
        value = value[len("http://"):]
    if value.startswith("https://"):
        value = value[len("https://"):]
    return value.split("/", 1)[0] or DEFAULT_PROVISION_HOST


def provision_post(host: str, path: str, form: dict[str, str]) -> dict:
    target_host = normalize_provision_host(host)
    data = urlencode(form).encode("utf-8")
    request = Request(
        f"http://{target_host}{path}",
        data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    try:
        with urlopen(request, timeout=PROVISION_TIMEOUT_SECONDS) as response:
            text = response.read(512).decode("utf-8", errors="replace")
            return {"ok": True, "host": target_host, "status": response.status, "body": text}
    except HTTPError as exc:
        text = exc.read(512).decode("utf-8", errors="replace")
        return {"ok": False, "host": target_host, "status": exc.code, "error": text or exc.reason}
    except URLError as exc:
        return {"ok": False, "host": target_host, "error": str(exc.reason)}
    except TimeoutError:
        return {"ok": False, "host": target_host, "error": "timeout"}


def provision_get(host: str, path: str) -> dict:
    target_host = normalize_provision_host(host)
    request = Request(f"http://{target_host}{path}", method="GET")
    try:
        with urlopen(request, timeout=PROVISION_TIMEOUT_SECONDS) as response:
            text = response.read(2048).decode("utf-8", errors="replace")
            try:
                payload = json.loads(text)
            except json.JSONDecodeError:
                payload = {"body": text}
            return {"ok": True, "host": target_host, "status": response.status, **payload}
    except HTTPError as exc:
        text = exc.read(512).decode("utf-8", errors="replace")
        return {"ok": False, "host": target_host, "status": exc.code, "error": text or exc.reason}
    except URLError as exc:
        return {"ok": False, "host": target_host, "error": str(exc.reason)}
    except TimeoutError:
        return {"ok": False, "host": target_host, "error": "timeout"}


def provision_save(host: str | None, ssid: str, password: str) -> dict:
    now = time.time()
    result = provision_post(host or DEFAULT_PROVISION_HOST, "/save", {"ssid": ssid, "password": password})
    with state_lock:
        state = "normal" if result.get("ok") else "fault"
        message = f"主机提交配网 SSID:{ssid}" if result.get("ok") else f"配网提交失败: {result.get('error', 'unknown')}"
        append_event("provision", state, message, now)
    return result


def provision_scan(host: str | None) -> dict:
    now = time.time()
    result = provision_get(host or DEFAULT_PROVISION_HOST, "/scan")
    with state_lock:
        state = "normal" if result.get("ok") else "fault"
        count = len(result.get("aps", [])) if isinstance(result.get("aps"), list) else 0
        message = f"主机扫描离线终端 WiFi，发现 {count} 个网络" if result.get("ok") else f"扫描离线终端 WiFi 失败: {result.get('error', 'unknown')}"
        append_event("provision", state, message, now)
    return result


def provision_clear(host: str | None) -> dict:
    now = time.time()
    result = provision_post(host or DEFAULT_PROVISION_HOST, "/clear", {})
    with state_lock:
        state = "normal" if result.get("ok") else "fault"
        message = "主机请求清除终端 WiFi" if result.get("ok") else f"清除终端 WiFi 失败: {result.get('error', 'unknown')}"
        append_event("provision", state, message, now)
    return result


def send_command(command: str, device_id: str | None = None) -> dict:
    now = time.time()
    payload = {
        "type": "command",
        "command": command,
        "device_id": device_id or "broadcast",
        "pc_time_s": now,
    }
    encoded = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    channels: list[dict] = []

    with state_lock:
        device = devices.get(device_id or "") if device_id else None

    target_ip = device.get("sender_ip") if device else None
    if target_ip and target_ip != "MQTT":
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(encoded, (target_ip, command_port_runtime))
        channels.append({"type": "udp", "target": target_ip, "port": command_port_runtime, "ok": True})
    elif not device_id:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.sendto(encoded, ("255.255.255.255", command_port_runtime))
        channels.append({"type": "udp", "target": "255.255.255.255", "port": command_port_runtime, "ok": True})

    mqtt_result = mqtt_publish_command(payload, device_id)
    channels.append({"type": "mqtt", **mqtt_result})

    ok = any(channel.get("ok") for channel in channels)
    with state_lock:
        append_event(device_id or "broadcast",
                     "normal" if ok else "fault",
                     f"主机发送命令 {command} -> {device_id or 'broadcast'}，通道:{','.join(c.get('type', '?') for c in channels)}",
                     now)
    return {"ok": ok, "channels": channels, "payload": payload}


def send_wifi_update_command(device_id: str, ssid: str, password: str) -> dict:
    now = time.time()
    payload = {
        "type": "command",
        "command": "wifi_update",
        "device_id": device_id,
        "ssid": ssid,
        "password": password,
        "pc_time_s": now,
    }
    encoded = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    channels: list[dict] = []

    with state_lock:
        device = devices.get(device_id)
    if device is None:
        mqtt_result = mqtt_publish_command(payload, device_id)
        return {"ok": bool(mqtt_result.get("ok")), "channels": [{"type": "mqtt", **mqtt_result}], "payload": payload}

    target_ip = device.get("sender_ip")
    if target_ip and target_ip != "MQTT":
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(encoded, (target_ip, command_port_runtime))
        channels.append({"type": "udp", "target": target_ip, "port": command_port_runtime, "ok": True})

    mqtt_result = mqtt_publish_command(payload, device_id)
    channels.append({"type": "mqtt", **mqtt_result})
    ok = any(channel.get("ok") for channel in channels)

    with state_lock:
        append_event(device_id,
                     "normal" if ok else "fault",
                     f"主机请求在线改 WiFi SSID:{ssid}，通道:{','.join(c.get('type', '?') for c in channels)}",
                     now)
    return {"ok": ok, "channels": channels, "payload": payload}


def safe_static_path(path: str) -> Path | None:
    relative = "index.html" if path == "/" else path.lstrip("/")
    candidate = (WEB_ROOT / relative).resolve()
    try:
        candidate.relative_to(WEB_ROOT.resolve())
    except ValueError:
        return None
    if candidate.is_dir():
        candidate = candidate / "index.html"
    return candidate if candidate.exists() else None


def local_ipv4_addresses() -> list[str]:
    ips: set[str] = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if not ip.startswith("127."):
                ips.add(ip)
    except OSError:
        pass

    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        ip = probe.getsockname()[0]
        if not ip.startswith("127."):
            ips.add(ip)
        probe.close()
    except OSError:
        pass

    return sorted(ips)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        return

    def send_json(self, payload: dict, status: int = 200) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path
        if path == "/api/snapshot":
            query = parse_qs(parsed.query)
            selected_id = query.get("device_id", [None])[0]
            self.send_json(snapshot_payload(selected_id))
            return
        if path == "/api/latest":
            payload = snapshot_payload()
            self.send_json({"latest": payload["selected"], "points": payload["points"]})
            return
        if path == "/api/export.csv":
            if not output_runtime.exists():
                self.send_response(404)
                self.end_headers()
                return
            body = output_runtime.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/csv; charset=utf-8")
            self.send_header("Content-Disposition", 'attachment; filename="graesp_telemetry.csv"')
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return

        file_path = safe_static_path(path)
        if file_path is None:
            self.send_response(404)
            self.end_headers()
            return

        body = file_path.read_bytes()
        content_type = mimetypes.guess_type(file_path.name)[0] or "application/octet-stream"
        if file_path.suffix == ".js":
            content_type = "text/javascript"
        self.send_response(200)
        self.send_header("Content-Type", f"{content_type}; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def read_json_body(self) -> dict | None:
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8", errors="replace")
        try:
            return json.loads(body or "{}")
        except json.JSONDecodeError:
            self.send_json({"ok": False, "error": "invalid json"}, 400)
            return None

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/command":
            payload = self.read_json_body()
            if payload is None:
                return
            command = str(payload.get("command", "")).strip()
            if not command:
                self.send_json({"ok": False, "error": "missing command"}, 400)
                return
            device_id = payload.get("device_id")
            self.send_json(send_command(command, device_id))
            return

        if path == "/api/device/delete":
            payload = self.read_json_body()
            if payload is None:
                return
            device_id = str(payload.get("device_id", "")).strip()
            if not device_id:
                self.send_json({"ok": False, "error": "missing device_id"}, 400)
                return
            self.send_json(delete_device(device_id))
            return

        if path == "/api/devices/clear-offline":
            self.send_json(clear_offline_devices())
            return

        if path == "/api/provision/save":
            payload = self.read_json_body()
            if payload is None:
                return
            ssid = str(payload.get("ssid", "")).strip()
            password = str(payload.get("password", ""))
            host = str(payload.get("host", DEFAULT_PROVISION_HOST)).strip()
            if not ssid:
                self.send_json({"ok": False, "error": "missing ssid"}, 400)
                return
            self.send_json(provision_save(host, ssid, password))
            return

        if path == "/api/provision/clear":
            payload = self.read_json_body()
            if payload is None:
                return
            host = str(payload.get("host", DEFAULT_PROVISION_HOST)).strip()
            self.send_json(provision_clear(host))
            return

        if path == "/api/provision/scan":
            payload = self.read_json_body()
            if payload is None:
                return
            host = str(payload.get("host", DEFAULT_PROVISION_HOST)).strip()
            self.send_json(provision_scan(host))
            return

        if path == "/api/provision/online-update":
            payload = self.read_json_body()
            if payload is None:
                return
            device_id = str(payload.get("device_id", "")).strip()
            ssid = str(payload.get("ssid", "")).strip()
            password = str(payload.get("password", ""))
            if not device_id:
                self.send_json({"ok": False, "error": "missing device_id"}, 400)
                return
            if not ssid:
                self.send_json({"ok": False, "error": "missing ssid"}, 400)
                return
            self.send_json(send_wifi_update_command(device_id, ssid, password))
            return

        if path != "/api/command":
            self.send_response(404)
            self.end_headers()
            return


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--udp-port", type=int, default=UDP_PORT)
    parser.add_argument("--command-port", type=int, default=COMMAND_PORT)
    parser.add_argument("--host", default=HTTP_HOST)
    parser.add_argument("--http-port", type=int, default=HTTP_PORT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    global command_port_runtime, output_runtime
    command_port_runtime = args.command_port
    output_runtime = args.output

    threading.Thread(target=udp_worker, args=(args.udp_port, args.output), daemon=True).start()
    threading.Thread(target=mqtt_worker, daemon=True).start()
    server = ThreadingHTTPServer((args.host, args.http_port), Handler)
    print(f"UDP telemetry listening: 0.0.0.0:{args.udp_port}")
    print(f"UDP command target port: {args.command_port}")
    print(f"CSV saving: {args.output}")
    print(f"Open on this PC: http://127.0.0.1:{args.http_port}")
    for ip in local_ipv4_addresses():
        print(f"Open from same WiFi: http://{ip}:{args.http_port}")
    server.serve_forever()


if __name__ == "__main__":
    main()






