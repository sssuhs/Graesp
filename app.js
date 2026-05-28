const $ = id => document.getElementById(id);
const chart = $("chart");
const ctx = chart.getContext("2d");

let client = null;
let selectedId = localStorage.getItem("graesp-cloud-selected") || "";
let devices = new Map();
let histories = new Map();
let logs = [];
let seriesMode = "thermal";

const HISTORY_LIMIT = 160;

const stateLabel = {
  normal: "正常监测",
  temp_high: "温升偏高",
  warning: "过载预警",
  overload: "过载报警",
  low_battery: "低电量",
  fault: "故障",
  offline: "离线"
};

const faultBits = [
  [0, "NTC1 探头异常"],
  [1, "NTC2 探头异常"],
  [2, "环境温度 NTC 异常"],
  [3, "电池电压检测异常"],
  [4, "传感器读取失败"]
];

function nowText() {
  return new Date().toLocaleTimeString("zh-CN", { hour12: false });
}

function log(message, level = "normal") {
  logs.unshift({ message, level, time: nowText() });
  logs = logs.slice(0, 80);
  renderLogs();
}

function fmt(value, digits = 2) {
  const number = Number(value);
  return Number.isFinite(number) ? number.toFixed(digits) : "--";
}

function pct(value) {
  const number = Number(value);
  return Number.isFinite(number) ? (number * 100).toFixed(1) : "--";
}

function topicPrefix() {
  return $("topicPrefix").value.trim().replace(/\/+$/g, "") || "graesp/lin";
}

function deviceTopic(deviceId, suffix) {
  return `${topicPrefix()}/${deviceId}/${suffix}`;
}

function commandTopic(deviceId) {
  return deviceTopic(deviceId, "cmd");
}

function telemetryTopic() {
  return `${topicPrefix()}/+/telemetry`;
}

function setMqttStatus(text, cls = "") {
  $("mqttStatus").textContent = text;
  $("mqttDot").className = `dot ${cls}`.trim();
  $("mqttMeta").textContent = $("brokerUrl").value.trim();
  $("mqttStatusCard").classList.toggle("online", cls === "online");
}

function linkState(device) {
  if (!device) return "offline";
  const age = (Date.now() - device.receivedAt) / 1000;
  if (age <= 8) return "online";
  if (age <= 20) return "stale";
  return "offline";
}

function faultText(device) {
  const mask = Number(device?.self_test_fault_mask ?? device?.fault_mask ?? 0);
  const list = faultBits.filter(([bit]) => (mask & (1 << bit)) !== 0).map(([, label]) => label);
  return list.length ? list.join("、") : "无";
}

function normalizePacket(packet) {
  const ntc = packet.ntc_c || [packet.ntc1, packet.ntc2, packet.ambient];
  return {
    ...packet,
    device_id: packet.device_id || "unknown",
    ntc_c: ntc,
    ambient_c: packet.ambient_c ?? packet.ambient,
    temp_rise_c: packet.temp_rise_c ?? packet.rise,
    heating_rate_c_per_min: packet.heating_rate_c_per_min ?? packet.rate,
    estimated_current_a: packet.estimated_current_a ?? packet.current,
    overload_probability: packet.overload_probability ?? packet.prob,
    battery_percent: packet.battery_percent ?? packet.battery,
    point_diff_c: packet.point_diff_c ?? Math.abs(Number(ntc?.[0]) - Number(ntc?.[1])),
    self_test_fault_mask: packet.self_test_fault_mask ?? packet.fault_mask ?? 0,
    receivedAt: Date.now()
  };
}

function appendHistory(packet) {
  const id = packet.device_id;
  const list = histories.get(id) || [];
  list.push({ ...packet, t: Date.now() });
  if (list.length > HISTORY_LIMIT) list.splice(0, list.length - HISTORY_LIMIT);
  histories.set(id, list);
}

function selectedDevice() {
  return devices.get(selectedId) || [...devices.values()][0] || null;
}

function chartSeries() {
  return seriesMode === "thermal"
    ? [
      { key: "temp_rise_c", label: "温升", unit: "℃", color: "#24a148", fill: "rgba(36, 161, 72, 0.05)", scale: v => Math.max(0, Math.min(100, Number(v) * 5)) },
      { key: "heating_rate_c_per_min", label: "升温速率", unit: "℃/min", color: "#b86b00", fill: "rgba(184, 107, 0, 0.04)", scale: v => Math.max(0, Math.min(100, Number(v) * 10)) },
      { key: "point_diff_c", label: "测点差", unit: "℃", color: "#0071e3", fill: "rgba(0, 113, 227, 0.04)", scale: v => Math.max(0, Math.min(100, Number(v) * 20)) }
    ]
    : [
      { key: "estimated_current_a", label: "估计电流", unit: "A", color: "#0071e3", fill: "rgba(0, 113, 227, 0.05)", scale: v => Math.max(0, Math.min(100, Number(v) * 5)) },
      { key: "overload_probability", label: "过载概率", unit: "%", color: "#d9383a", fill: "rgba(217, 56, 58, 0.05)", scale: v => Math.max(0, Math.min(100, Number(v) * 100)) },
      { key: "battery_percent", label: "电量", unit: "%", color: "#059669", fill: "rgba(5, 150, 105, 0.03)", scale: v => Math.max(0, Math.min(100, Number(v))) }
    ];
}

function drawChart(device = selectedDevice()) {
  const dpr = window.devicePixelRatio || 1;
  const rect = chart.getBoundingClientRect();
  if (!rect.width || !rect.height) return;

  ctx.setTransform(1, 0, 0, 1, 0, 0);
  chart.width = Math.floor(rect.width * dpr);
  chart.height = Math.floor(rect.height * dpr);
  ctx.scale(dpr, dpr);

  const width = rect.width;
  const height = rect.height;
  const points = device ? (histories.get(device.device_id) || []) : [];
  const series = chartSeries();

  ctx.clearRect(0, 0, width, height);
  const computedStyle = getComputedStyle(document.documentElement);
  ctx.strokeStyle = computedStyle.getPropertyValue("--chart-grid").trim() || "rgba(0, 0, 0, 0.06)";
  ctx.lineWidth = 1;

  for (let i = 0; i <= 4; i++) {
    const y = 28 + i * (height - 58) / 4;
    ctx.beginPath();
    ctx.moveTo(50, y);
    ctx.lineTo(width - 20, y);
    ctx.stroke();
  }

  ctx.fillStyle = computedStyle.getPropertyValue("--chart-text").trim() || "#86868b";
  ctx.font = "11px ui-monospace, SFMono-Regular, Consolas, monospace";
  ctx.textAlign = "right";
  ctx.fillText("100", 40, 32);
  ctx.fillText("50", 40, 28 + (height - 58) / 2 + 4);
  ctx.fillText("0", 40, height - 24);

  if (!points.length) {
    ctx.textAlign = "center";
    ctx.font = "13px -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif";
    ctx.fillText("暂无曲线数据，连接设备后自动绘制", width / 2, height / 2);
  }

  const xStep = Math.max(1, (width - 70) / Math.max(points.length - 1, 1));
  for (const line of series) {
    if (!points.length) continue;

    ctx.fillStyle = line.fill;
    ctx.beginPath();
    points.forEach((point, index) => {
      const x = 50 + index * xStep;
      const y = height - 28 - line.scale(point[line.key]) * (height - 58) / 100;
      if (index === 0) ctx.moveTo(x, height - 28);
      ctx.lineTo(x, y);
      if (index === points.length - 1) ctx.lineTo(x, height - 28);
    });
    ctx.closePath();
    ctx.fill();

    ctx.strokeStyle = line.color;
    ctx.lineWidth = 2.5;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.beginPath();
    points.forEach((point, index) => {
      const x = 50 + index * xStep;
      const y = height - 28 - line.scale(point[line.key]) * (height - 58) / 100;
      if (index === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }

  $("chartMeta").textContent = device ? `${device.device_id} · 最近 ${points.length} 个采样点` : "最近 0 个采样点";
  $("chartLegend").innerHTML = series.map(line => `<span><i style="background:${line.color}"></i>${line.label}</span>`).join("");
}

function renderSummary() {
  const all = [...devices.values()];
  const online = all.filter(d => linkState(d) === "online").length;
  $("summaryText").textContent = `${all.length} 个设备，${online} 个在线`;
  $("deviceCount").textContent = `${all.length} 个设备`;
}

function renderDevices() {
  const list = $("deviceList");
  const all = [...devices.values()].sort((a, b) => b.receivedAt - a.receivedAt);
  list.innerHTML = "";
  if (!all.length) {
    const empty = document.createElement("div");
    empty.className = "log-row";
    empty.textContent = "暂无设备遥测";
    list.append(empty);
    return;
  }

  all.forEach(device => {
    const id = device.device_id;
    const state = linkState(device);
    const card = document.createElement("button");
    card.className = `device-card ${selectedId === id ? "active" : ""} ${state === "offline" ? "offline" : ""}`;
    card.innerHTML = `<strong>${id}</strong><span>${stateLabel[device.state] || device.state || "--"} · ${state === "online" ? "在线" : state === "stale" ? "信号延迟" : "离线"}</span><span>温升 ${fmt(device.temp_rise_c)} ℃ · 电量 ${device.battery_percent ?? "--"}%</span>`;
    card.onclick = () => {
      selectedId = id;
      localStorage.setItem("graesp-cloud-selected", id);
      render();
    };
    list.append(card);
  });
}

function stateReason(device) {
  if (!device) return "连接 MQTT 后选择一个在线设备。";
  if (linkState(device) !== "online") return "超过 8 秒未收到云端遥测，设备可能离线或网络延迟。";
  if (device.state === "fault") return `自检/传感器异常：${faultText(device)}。`;
  if (device.state === "overload") return `温升 ${fmt(device.temp_rise_c)} ℃，估计电流 ${fmt(device.estimated_current_a)} A，已经进入过载报警。`;
  if (device.state === "warning") return `温升 ${fmt(device.temp_rise_c)} ℃，过载概率 ${pct(device.overload_probability)}%，处于预警状态。`;
  if (device.state === "temp_high") return `温升 ${fmt(device.temp_rise_c)} ℃，需要继续观察升温趋势。`;
  return `温升 ${fmt(device.temp_rise_c)} ℃，估计电流 ${fmt(device.estimated_current_a)} A，云端监测正常。`;
}

function renderDetail() {
  const device = selectedDevice();
  if (device && !selectedId) selectedId = device.device_id;
  const state = device?.state || "offline";
  const link = linkState(device);

  $("stateBanner").className = `state-banner ${state}`;
  $("stateTitle").textContent = device ? (stateLabel[state] || state) : "未选择设备";
  $("stateReason").textContent = stateReason(device);
  $("linkBadge").className = `pill ${link}`;
  $("linkBadge").textContent = link === "online" ? "在线" : link === "stale" ? "信号延迟" : "离线";

  $("deviceTitle").textContent = device?.device_id || "设备详情";
  $("deviceMeta").textContent = device ? `MQTT · 最近更新 ${Math.round((Date.now() - device.receivedAt) / 1000)} 秒前` : "--";
  $("rise").textContent = fmt(device?.temp_rise_c);
  $("current").textContent = fmt(device?.estimated_current_a);
  $("prob").textContent = pct(device?.overload_probability);
  $("battery").textContent = device?.battery_percent ?? "--";
  $("ntc1").textContent = `${fmt(device?.ntc_c?.[0])} ℃`;
  $("ntc2").textContent = `${fmt(device?.ntc_c?.[1])} ℃`;
  $("ambient").textContent = `${fmt(device?.ambient_c)} ℃`;
  $("rate").textContent = `${fmt(device?.heating_rate_c_per_min)} ℃/min`;
  $("selfTest").textContent = device?.self_test_ok === true ? "通过" : device?.self_test_ok === false ? "异常" : "--";
  $("faultText").textContent = device ? faultText(device) : "--";
  $("faultText").classList.toggle("fault", Boolean(device && faultText(device) !== "无"));
  renderWifiList(device);
  drawChart(device);
}

function renderWifiList(device) {
  const list = $("wifiList");
  list.innerHTML = "";
  const aps = Array.isArray(device?.wifi_scan) ? device.wifi_scan : [];
  if (!aps.length) {
    const empty = document.createElement("div");
    empty.className = "log-row";
    empty.textContent = "点击“扫描 WiFi”后，这里显示设备周围网络。";
    list.append(empty);
    return;
  }
  aps.slice().sort((a, b) => Number(b.rssi) - Number(a.rssi)).forEach(ap => {
    const row = document.createElement("button");
    row.className = "wifi-row";
    row.innerHTML = `<strong>${ap.ssid || "隐藏网络"}</strong><span>${ap.rssi ?? "--"} dBm</span>`;
    row.onclick = () => {
      $("wifiSsid").value = ap.ssid || "";
    };
    list.append(row);
  });
}

function renderLogs() {
  const list = $("logList");
  list.innerHTML = "";
  logs.forEach(item => {
    const row = document.createElement("div");
    row.className = "log-row";
    row.textContent = `${item.time}  ${item.message}`;
    list.append(row);
  });
}

function render() {
  renderSummary();
  renderDevices();
  renderDetail();
}

function connectMqtt() {
  if (!window.mqtt) {
    setMqttStatus("MQTT.js 未加载", "error");
    log("MQTT.js 库没有加载，检查网络或 CDN。", "fault");
    return;
  }
  if (client) client.end(true);

  localStorage.setItem("graesp-cloud-broker", $("brokerUrl").value.trim());
  localStorage.setItem("graesp-cloud-prefix", topicPrefix());
  setMqttStatus("连接中", "");

  client = mqtt.connect($("brokerUrl").value.trim(), {
    clientId: `graesp_cloud_${Math.random().toString(16).slice(2)}`,
    clean: true,
    reconnectPeriod: 3000,
    connectTimeout: 8000
  });

  client.on("connect", () => {
    setMqttStatus("已连接", "online");
    const topic = telemetryTopic();
    client.subscribe(topic, err => {
      if (err) log(`订阅失败：${err.message}`, "fault");
      else log(`已订阅 ${topic}`);
    });
  });
  client.on("reconnect", () => setMqttStatus("重连中", ""));
  client.on("close", () => setMqttStatus("已断开", ""));
  client.on("error", err => {
    setMqttStatus("连接错误", "error");
    log(`MQTT 错误：${err.message}`, "fault");
  });
  client.on("message", (topic, payload) => {
    try {
      const packet = normalizePacket(JSON.parse(payload.toString()));
      devices.set(packet.device_id, packet);
      appendHistory(packet);
      if (!selectedId) selectedId = packet.device_id;
      log(`收到 ${packet.device_id} 遥测：${packet.state || "--"}`);
      render();
    } catch (error) {
      log(`遥测解析失败：${error.message}`, "fault");
    }
  });
}

function publishCommand(command, extra = {}) {
  const device = devices.get(selectedId);
  if (!client || !client.connected) {
    log("MQTT 未连接，命令没有发送。", "fault");
    return;
  }
  if (!device) {
    log("请先选择一个设备。", "fault");
    return;
  }
  const payload = { command, device_id: device.device_id, ...extra, pc_time_s: Date.now() / 1000 };
  client.publish(commandTopic(device.device_id), JSON.stringify(payload), { qos: 0, retain: false }, err => {
    if (err) log(`命令发送失败：${err.message}`, "fault");
    else log(`已发送 ${command} -> ${device.device_id}`);
  });
}

$("connectBtn").onclick = connectMqtt;
$("disconnectBtn").onclick = () => {
  if (client) client.end();
  client = null;
  setMqttStatus("已断开", "");
};
$("clearOfflineBtn").onclick = () => {
  for (const [id, device] of devices) {
    if (linkState(device) === "offline") devices.delete(id);
  }
  render();
};
document.querySelectorAll("[data-command]").forEach(button => {
  button.onclick = () => publishCommand(button.dataset.command);
});
$("seriesThermal").onclick = () => {
  seriesMode = "thermal";
  $("seriesThermal").classList.add("active");
  $("seriesModel").classList.remove("active");
  drawChart();
};
$("seriesModel").onclick = () => {
  seriesMode = "model";
  $("seriesModel").classList.add("active");
  $("seriesThermal").classList.remove("active");
  drawChart();
};
window.addEventListener("resize", () => drawChart());

$("wifiUpdateBtn").onclick = () => {
  const ssid = $("wifiSsid").value.trim();
  if (!ssid) {
    log("请先选择或输入目标 WiFi。", "fault");
    $("wifiSsid").focus();
    return;
  }
  publishCommand("wifi_update", { ssid, password: $("wifiPassword").value });
};

$("brokerUrl").value = localStorage.getItem("graesp-cloud-broker") || $("brokerUrl").value;
$("topicPrefix").value = localStorage.getItem("graesp-cloud-prefix") || $("topicPrefix").value;
setInterval(render, 1000);
render();






