const $ = id => document.getElementById(id);
const chart = $("chart");
const ctx = chart.getContext("2d");

let selectedId = null;
let seriesMode = "thermal";
let snapshot = { devices: [], selected: null, points: [], events: [] };
let deviceFilter = "all";
let deviceSearch = "";
let deviceNames = JSON.parse(localStorage.getItem("graesp-device-names") || "{}");
let provisionWait = null;
let wifiScanWait = null;

const stateLabel = {
  normal: "正常",
  temp_high: "温升偏高",
  warning: "预警",
  overload: "过载",
  low_battery: "低电量",
  fault: "故障",
  offline: "离线"
};

const alertStates = new Set(["temp_high", "warning", "overload", "low_battery", "fault"]);
const linkLabel = {
  online: "在线",
  stale: "信号延迟",
  offline: "离线"
};
const selfTestFaultBits = [
  { bit: 0, label: "NTC1 探头异常" },
  { bit: 1, label: "NTC2 探头异常" },
  { bit: 2, label: "环境温度 NTC 异常" },
  { bit: 3, label: "电池电压检测异常" },
  { bit: 4, label: "传感器读取失败" }
];

function selfTestFaultItems(mask) {
  const value = Number(mask);
  if (!Number.isFinite(value) || value <= 0) return [];
  const items = selfTestFaultBits
    .filter(item => (value & (1 << item.bit)) !== 0)
    .map(item => item.label);
  const knownMask = selfTestFaultBits.reduce((acc, item) => acc | (1 << item.bit), 0);
  const unknown = value & ~knownMask;
  if (unknown) items.push(`未知故障位 0x${unknown.toString(16)}`);
  return items;
}

function selfTestFaultText(device) {
  if (!device || device.self_test_fault_mask == null) return "--";
  const mask = Number(device.self_test_fault_mask);
  if (!Number.isFinite(mask)) return "--";
  const items = selfTestFaultItems(mask);
  if (items.length > 0) return items.join("、");
  if (device.self_test_ok === false) return "自检异常（未返回故障码）";
  return "无";
}

function selfTestStatusText(device) {
  if (!device || device.self_test_ok == null) return "--";
  return device.self_test_ok ? "通过" : "异常";
}
function linkState(device) {
  if (!device) return "offline";
  if (device.link_state) return device.link_state;
  if (!device.online) return "offline";
  return Number(device.age_s) > 3 ? "stale" : "online";
}

function alarmReason(device) {
  if (!device) return "等待设备数据。";
  const rise = fmt(device.temp_rise_c);
  const rate = fmt(device.heating_rate_c_per_min);
  const prob = pct(device.overload_probability);
  const current = fmt(device.estimated_current_a);
  const battery = Number.isFinite(Number(device.battery_percent)) ? `${device.battery_percent}%` : "--";
  switch (displayState(device)) {
    case "offline":
      return `超过 ${Math.round(Number(device.age_s) || 0)} 秒未收到遥测，设备可能断电或离开当前网络。`;
    case "fault":
      return `传感器/自检异常：${selfTestFaultText(device)}。请先排除硬件问题。`;
    case "low_battery":
      return `电池电量 ${battery}，系统降低采样/上传频率并关闭蜂鸣器。`;
    case "overload":
      return `温升 ${rise} °C，估计电流 ${current} A，过载概率 ${prob}%，已进入过载报警。`;
    case "warning":
      return `温升 ${rise} °C，升温速率 ${rate} °C/min，模型判断进入预警区。`;
    case "temp_high":
      return `温升 ${rise} °C 或升温速率 ${rate} °C/min 偏高，继续观察持续时间。`;
    case "normal":
    default:
      return `温升 ${rise} °C，估计电流 ${current} A，过载概率 ${prob}%，处于正常监测。`;
  }
}

function updateButtonBusy(button, busy, busyText, idleText) {
  if (!button) return;
  button.disabled = Boolean(busy);
  if (busy) {
    button.dataset.idleText = button.dataset.idleText || idleText || button.textContent;
    button.textContent = busyText;
    button.classList.add("busy");
  } else {
    button.textContent = button.dataset.idleText || idleText || button.textContent;
    button.classList.remove("busy");
  }
}

function fmt(value, digits = 2) {
  const number = Number(value);
  return Number.isFinite(number) ? number.toFixed(digits) : "--";
}

function pct(value) {
  const number = Number(value);
  return Number.isFinite(number) ? (number * 100).toFixed(1) : "--";
}

function deviceKey(device) {
  return device.device_id || device.sender_ip || "unknown";
}

function transportText(device) {
  const transport = String(device?.transport || "").toLowerCase();
  if (transport === "mqtt") return "MQTT 云端";
  if (transport === "udp") return device.sender_ip ? `UDP ${device.sender_ip}` : "UDP 本地";
  return device?.sender_ip || "未知通道";
}

function stateClass(state) {
  return state || "normal";
}

function displayState(device) {
  if (!device) return "--";
  return linkState(device) === "offline" ? "offline" : (device.state || "--");
}

function ageText(seconds) {
  const value = Number(seconds);
  if (!Number.isFinite(value)) return "--";
  if (value < 1.5) return "刚刚";
  return `${Math.round(value)} 秒前`;
}

function uptimeText(ms) {
  const seconds = Math.round(Number(ms || 0) / 1000);
  if (seconds < 60) return `${seconds} 秒`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes} 分钟`;
  return `${Math.floor(minutes / 60)} 小时 ${minutes % 60} 分钟`;
}

function setText(id, value) {
  const el = $(id);
  if (el) el.textContent = value;
}

function deviceName(id) {
  return deviceNames[id] || id;
}

function saveDeviceNames() {
  localStorage.setItem("graesp-device-names", JSON.stringify(deviceNames));
}

function filteredDevices() {
  const keyword = deviceSearch.trim().toLowerCase();
  return snapshot.devices.filter(device => {
    const id = deviceKey(device);
    const shownState = displayState(device);
    if (deviceFilter === "online" && !device.online) return false;
    if (deviceFilter === "alert" && !(device.online && alertStates.has(device.state))) return false;
    if (!keyword) return true;
    const haystack = `${id} ${deviceName(id)} ${transportText(device)} ${stateLabel[shownState] || shownState}`.toLowerCase();
    return haystack.includes(keyword);
  });
}

function statusCounts() {
  const total = snapshot.devices.length;
  const online = snapshot.devices.filter(device => device.online).length;
  const offline = total - online;
  const alert = snapshot.devices.filter(device => device.online && alertStates.has(device.state)).length;
  return { total, online, offline, alert };
}

function updateHostStatus() {
  const counts = statusCounts();
  $("hostDot").className = "dot " + (counts.online > 0 ? "online" : "");
  $("hostState").textContent = counts.total ? `${counts.online}/${counts.total} 在线` : "等待数据";
  setText("homeNavCount", String(counts.total));
}

function showHome() {
  selectedId = null;
  $("home").hidden = false;
  $("dashboard").hidden = true;
  $("homeNav").classList.add("active");
  renderDevices();
  drawChart();
}

function showDevice(id) {
  selectedId = id;
  $("home").hidden = true;
  $("dashboard").hidden = false;
  $("homeNav").classList.remove("active");
  refresh();
}

function createPill(state) {
  const pill = document.createElement("span");
  pill.className = `state-pill ${stateClass(state)}`;
  pill.textContent = stateLabel[state] || state || "--";
  return pill;
}

function renderDevices() {
  const list = $("deviceList");
  list.innerHTML = "";

  const devices = filteredDevices();
  if (!devices.length) {
    const empty = document.createElement("div");
    empty.className = "sidebar-empty";
    empty.textContent = snapshot.devices.length ? "没有匹配设备" : "暂无终端";
    list.appendChild(empty);
    return;
  }

  for (const device of devices) {
    const id = deviceKey(device);
    const shownState = displayState(device);
    const button = document.createElement("button");
    button.className = "device-card" +
      (id === selectedId ? " active" : "") +
      (linkState(device) === "offline" ? " offline" : "") +
      (linkState(device) === "stale" ? " stale" : "");
    if (device.online && alertStates.has(device.state)) button.classList.add("alerting");
    button.type = "button";
    button.onclick = () => showDevice(id);

    const top = document.createElement("div");
    top.className = "device-top";
    const name = document.createElement("div");
    name.className = "device-name";
    name.textContent = deviceName(id);
    top.append(name, createPill(shownState));

    const sub = document.createElement("div");
    sub.className = "device-sub";
    sub.textContent = `${id} · ${transportText(device)} · ${linkLabel[linkState(device)] || "--"} · ${ageText(device.age_s)}`;

    button.append(top, sub);
    list.appendChild(button);
  }
}

function renderHome() {
  const counts = statusCounts();
  setText("totalDevices", String(counts.total));
  setText("onlineDevices", String(counts.online));
  setText("offlineDevices", String(counts.offline));
  setText("alertDevices", String(counts.alert));
  setText("homeMeta", counts.total ? `${counts.total} 个终端接入，${counts.online} 个在线` : "等待终端接入");
  const shown = filteredDevices().length;
  setText("homeListHint", counts.total ? `显示 ${shown}/${counts.total} 个设备` : "设备上线后自动出现");

  const grid = $("homeDeviceGrid");
  grid.innerHTML = "";

  if (!snapshot.devices.length) {
    const empty = document.createElement("div");
    empty.className = "home-empty";
    empty.textContent = "暂无设备上线。请确认 MQTT 订阅已启动，或本地 UDP 3333 正在监听。";
    grid.appendChild(empty);
    return;
  }

  for (const device of filteredDevices()) {
    const id = deviceKey(device);
    const shownState = displayState(device);
    const card = document.createElement("article");
    card.className = `home-device ${linkState(device) === "offline" ? "offline" : ""} ${linkState(device) === "stale" ? "stale" : ""}`;
    if (device.online && alertStates.has(device.state)) card.classList.add("alerting");
    card.onclick = () => showDevice(id);

    const head = document.createElement("div");
    head.className = "home-device-head";
    const title = document.createElement("strong");
    title.textContent = deviceName(id);
    head.append(title, createPill(shownState));

    const meta = document.createElement("div");
    meta.className = "home-device-meta";
    meta.textContent = `${id} · ${transportText(device)} · ${linkLabel[linkState(device)] || "--"} · ${ageText(device.age_s)}`;

    const values = document.createElement("div");
    values.className = "home-device-values";
    values.append(
      smallValue("温升", `${fmt(device.temp_rise_c)} °C`),
      smallValue("电流", `${fmt(device.estimated_current_a)} A`),
      smallValue("电量", Number.isFinite(Number(device.battery_percent)) ? `${device.battery_percent}%` : "--")
    );

    const actions = document.createElement("div");
    actions.className = "home-device-actions";
    const open = document.createElement("button");
    open.className = "mini-btn";
    open.type = "button";
    open.textContent = "查看";
    open.onclick = event => {
      event.stopPropagation();
      showDevice(id);
    };
    const remove = document.createElement("button");
    remove.className = "mini-btn danger";
    remove.type = "button";
    remove.textContent = "删除";
    remove.onclick = event => {
      event.stopPropagation();
      deleteDevice(id);
    };
    actions.append(open, remove);

    card.append(head, meta, values, actions);
    grid.appendChild(card);
  }
}

function smallValue(label, value) {
  const item = document.createElement("div");
  const span = document.createElement("span");
  span.textContent = label;
  const strong = document.createElement("strong");
  strong.textContent = value;
  item.append(span, strong);
  return item;
}

function renderMetrics() {
  const current = snapshot.selected;
  if (selectedId && !current) {
    showHome();
    return;
  }

  if (!current) return;

  const id = deviceKey(current);
  const shownState = displayState(current);
  const lState = linkState(current);
  setText("deviceTitle", deviceName(id));
  setText("deviceMeta", `${transportText(current)} · ${linkLabel[lState] || "--"} · 上次更新 ${ageText(current.age_s)} · 运行 ${uptimeText(current.uptime_ms)}`);
  setText("state", stateLabel[shownState] || shownState || "--");
  setText("current", fmt(current.estimated_current_a));
  setText("prob", pct(current.overload_probability));
  setText("battery", Number.isFinite(Number(current.battery_percent)) ? current.battery_percent : "--");
  $("onlineBadge").className = "state-pill " + stateClass(shownState);
  $("onlineBadge").textContent = linkLabel[lState] || "离线";
  $("linkBadge").className = `state-pill ${lState}`;
  $("linkBadge").textContent = linkLabel[lState] || "离线";
  $("stateBanner").className = `state-banner ${shownState}`;
  setText("stateBannerTitle", stateLabel[shownState] || shownState || "--");
  setText("stateBannerReason", alarmReason(current));
  $("cmdSelfTest").disabled = !current.online;
  $("cmdResetStats").disabled = !current.online;
  $("deviceOnlineProvision").disabled = !current.online || $("deviceOnlineProvision").classList.contains("busy");
  $("deviceClearWifi").disabled = !current.online;
  $("deviceScanWifi").disabled = !current.online || $("deviceScanWifi").classList.contains("busy");

  const ntc = current.ntc_c || [];
  setText("ntc1", `${fmt(ntc[0])} °C`);
  setText("ntc2", `${fmt(ntc[1])} °C`);
  setText("ambient", `${fmt(current.ambient_c)} °C`);
  setText("rise", `${fmt(current.temp_rise_c)} °C`);
  setText("rate", `${fmt(current.heating_rate_c_per_min)} °C/min`);
  setText("diff", `${fmt(current.point_diff_c)} °C`);
  setText("selfTest", selfTestStatusText(current));
  setText("faultMask", selfTestFaultText(current));
  const faultMaskEl = $("faultMask");
  faultMaskEl.classList.toggle("fault-detail", selfTestFaultItems(current.self_test_fault_mask).length > 0 || current.self_test_ok === false);
  setText("maxRise", `${fmt(current.max_temp_rise_c)} °C`);
  setText("maxCurrent", `${fmt(current.max_estimated_current_a)} A`);
  setText("tempHighCount", current.temp_high_count ?? "--");
  setText("warningCount", current.warning_count ?? "--");
  setText("alarmCount", current.alarm_count ?? "--");
  setText("faultCount", current.fault_count ?? "--");
}

function drawChart() {
  const dpr = window.devicePixelRatio || 1;
  const rect = chart.getBoundingClientRect();
  if (!rect.width || !rect.height) return;

  ctx.setTransform(1, 0, 0, 1, 0, 0);
  chart.width = rect.width * dpr;
  chart.height = rect.height * dpr;
  ctx.scale(dpr, dpr);

  const width = rect.width;
  const height = rect.height;

  ctx.clearRect(0, 0, width, height);

  const computedStyle = getComputedStyle(document.documentElement);
  ctx.strokeStyle = computedStyle.getPropertyValue("--chart-grid").trim() || "rgba(0, 0, 0, 0.05)";
  ctx.lineWidth = 1;

  for (let i = 0; i <= 4; i++) {
    const y = 28 + i * (height - 58) / 4;
    ctx.beginPath();
    ctx.moveTo(50, y);
    ctx.lineTo(width - 20, y);
    ctx.stroke();
  }

  ctx.fillStyle = computedStyle.getPropertyValue("--chart-text").trim() || "#86868b";
  ctx.font = "11px ui-monospace, monospace";
  ctx.textAlign = "right";
  ctx.fillText("100", 40, 32);
  ctx.fillText("50", 40, 28 + (height - 58) / 2 + 4);
  ctx.fillText("0", 40, height - 24);

  const series = seriesMode === "thermal"
    ? [
      { key: "temp_rise_c", color: "#24b253", fill: "rgba(36, 178, 83, 0.04)", scale: v => Math.max(0, Math.min(100, Number(v) * 5)) },
      { key: "heating_rate_c_per_min", color: "#ea580c", fill: "rgba(234, 88, 12, 0.03)", scale: v => Math.max(0, Math.min(100, Number(v) * 10)) },
      { key: "point_diff_c", color: "#0071e3", fill: "rgba(0, 113, 227, 0.03)", scale: v => Math.max(0, Math.min(100, Number(v) * 20)) }
    ]
    : [
      { key: "estimated_current_a", color: "#0071e3", fill: "rgba(0, 113, 227, 0.04)", scale: v => Math.max(0, Math.min(100, Number(v) * 5)) },
      { key: "overload_probability", color: "#d9383a", fill: "rgba(217, 56, 58, 0.05)", scale: v => Math.max(0, Math.min(100, Number(v) * 100)) },
      { key: "battery_percent", color: "#059669", fill: "rgba(5, 150, 105, 0.02)", scale: v => Math.max(0, Math.min(100, Number(v))) }
    ];

  if (document.documentElement.getAttribute("data-theme") === "dark") {
    if (seriesMode === "thermal") {
      series[0].color = "#4ade80"; series[1].color = "#fb923c"; series[2].color = "#38bdf8";
    } else {
      series[0].color = "#38bdf8"; series[1].color = "#f87171"; series[2].color = "#a7f3d0";
    }
  }

  const pointsCount = snapshot.points.length;
  for (const line of series) {
    if (pointsCount === 0) continue;

    ctx.fillStyle = line.fill;
    ctx.beginPath();
    snapshot.points.forEach((point, index) => {
      const x = 50 + index * Math.max(1, (width - 70) / Math.max(pointsCount - 1, 1));
      const y = height - 28 - line.scale(point[line.key]) * (height - 58) / 100;
      if (index === 0) ctx.moveTo(x, height - 28);
      ctx.lineTo(x, y);
      if (index === pointsCount - 1) ctx.lineTo(x, height - 28);
    });
    ctx.closePath();
    ctx.fill();

    ctx.strokeStyle = line.color;
    ctx.lineWidth = 2.5;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.beginPath();
    snapshot.points.forEach((point, index) => {
      const x = 50 + index * Math.max(1, (width - 70) / Math.max(pointsCount - 1, 1));
      const y = height - 28 - line.scale(point[line.key]) * (height - 58) / 100;
      if (index === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }
}

function renderEventRows(targetId) {
  const box = $(targetId);
  box.innerHTML = "";
  const rows = snapshot.events || [];
  if (!rows.length) {
    const row = document.createElement("div");
    row.className = "log-row";
    row.innerHTML = '<span class="muted">--</span><span>暂无记录</span><span></span>';
    box.appendChild(row);
    return;
  }

  for (const event of rows) {
    const row = document.createElement("div");
    row.className = "log-row";
    const time = new Date((event.pc_time_s || 0) * 1000).toLocaleTimeString();
    const shownState = event.state || "normal";
    const state = stateLabel[shownState] || shownState || "";
    row.innerHTML = `
      <span class="muted code-font"></span>
      <span style="font-weight: 500;"></span>
      <span class="state-pill ${stateClass(shownState)}" style="justify-content: center;"></span>
    `;
    row.children[0].textContent = time;
    row.children[1].textContent = event.message || "";
    row.children[2].textContent = state;
    box.appendChild(row);
  }
}

function renderEvents() {
  renderEventRows("homeEventLog");
  renderEventRows("eventLog");
}

async function postJson(url, body = {}) {
  const response = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}

async function sendCommand(command, broadcast = false) {
  const body = { command };
  if (!broadcast) body.device_id = selectedId;
  await postJson("/api/command", body);
  await refresh();
}

async function deleteDevice(id) {
  const ok = window.confirm(`确定删除设备记录「${deviceName(id)}」吗？\n如果真实终端仍在发送数据，它会在下一次遥测后重新出现。`);
  if (!ok) return;
  await postJson("/api/device/delete", { device_id: id });
  if (selectedId === id) selectedId = null;
  await refresh();
}

async function clearOfflineDevices() {
  const offline = snapshot.devices.filter(device => !device.online).length;
  if (offline > 0 && !window.confirm(`确定清除 ${offline} 个离线设备记录吗？`)) return;
  await postJson("/api/devices/clear-offline");
  if (selectedId && !snapshot.devices.some(device => deviceKey(device) === selectedId && device.online)) {
    selectedId = null;
  }
  await refresh();
}

async function refresh() {
  const url = selectedId ? `/api/snapshot?device_id=${encodeURIComponent(selectedId)}` : "/api/snapshot";
  const response = await fetch(url, { cache: "no-store" });
  snapshot = await response.json();

  if (selectedId && !snapshot.selected) {
    selectedId = null;
    $("home").hidden = false;
    $("dashboard").hidden = true;
    $("homeNav").classList.add("active");
  }

  updateHostStatus();
  renderDevices();
  renderHome();
  renderMetrics();
  updateProvisionWait();
  updateWifiScanWait();
  drawChart();
  renderEvents();
}

function setFilter(nextFilter) {
  deviceFilter = nextFilter;
  for (const [id, value] of [["filterAll", "all"], ["filterOnline", "online"], ["filterAlert", "alert"]]) {
    $(id).classList.toggle("active", value === deviceFilter);
  }
  renderDevices();
  renderHome();
}

function renameSelectedDevice() {
  if (!selectedId) return;
  const nextName = window.prompt("输入设备备注名", deviceNames[selectedId] || selectedId);
  if (nextName === null) return;
  const trimmed = nextName.trim();
  if (!trimmed || trimmed === selectedId) {
    delete deviceNames[selectedId];
  } else {
    deviceNames[selectedId] = trimmed;
  }
  saveDeviceNames();
  renderDevices();
  renderHome();
  renderMetrics();
}


function notify(message, state = "normal") {
  const host = $("toastHost");
  if (!host) return;
  const item = document.createElement("div");
  item.className = `toast ${state}`;
  item.textContent = message;
  host.appendChild(item);
  requestAnimationFrame(() => item.classList.add("show"));
  window.setTimeout(() => {
    item.classList.remove("show");
    window.setTimeout(() => item.remove(), 260);
  }, state === "fault" ? 5200 : 3600);
}
function setDeviceProvisionStatus(text, state = "offline") {
  const pill = $("deviceProvisionStatus");
  if (!pill) return;
  pill.className = `state-pill ${state}`;
  pill.textContent = text;
}

function startProvisionWait(mode, deviceId = null) {
  provisionWait = {
    mode,
    deviceId,
    before: new Set(snapshot.devices.map(device => deviceKey(device))),
    sawOffline: false,
    startMs: Date.now(),
    timeoutMs: 60000
  };
  if (mode === "online") {
    setDeviceProvisionStatus("等待当前设备重连 60s", "temp_high");
  }
}

function updateProvisionWait() {
  if (!provisionWait) {
    return;
  }

  const elapsed = Date.now() - provisionWait.startMs;
  const remaining = Math.max(0, Math.ceil((provisionWait.timeoutMs - elapsed) / 1000));
  const onlineDevices = snapshot.devices.filter(device => device.online);
  let matched = false;
  if (provisionWait.mode === "online" && provisionWait.deviceId) {
    const device = snapshot.devices.find(item => deviceKey(item) === provisionWait.deviceId);
    if (device && !device.online) provisionWait.sawOffline = true;
    matched = Boolean(provisionWait.sawOffline && device && device.online);
  } else {
    matched = onlineDevices.some(device => !provisionWait.before.has(deviceKey(device)));
  }

  if (matched) {
    if (provisionWait.mode === "online") {
      setDeviceProvisionStatus("改网成功，设备已重连", "normal");
      notify("连接成功：设备已重新上线", "normal");
    }
    provisionWait = null;
    return;
  }

  if (elapsed >= provisionWait.timeoutMs) {
    if (provisionWait.mode === "online") {
      setDeviceProvisionStatus("等待超时", "fault");
      notify("连接失败：等待设备重连超时", "fault");
    }
    provisionWait = null;
    return;
  }

  if (provisionWait.mode === "online") {
    setDeviceProvisionStatus(`等待当前设备重连 ${remaining}s`, "temp_high");
  }
}

function wifiSignalLevel(rssi) {
  const value = Number(rssi);
  if (!Number.isFinite(value)) return 1;
  if (value >= -55) return 4;
  if (value >= -67) return 3;
  if (value >= -78) return 2;
  return 1;
}

function wifiSignalText(rssi) {
  const value = Number(rssi);
  if (!Number.isFinite(value)) return "未知信号";
  if (value >= -55) return "信号很强";
  if (value >= -67) return "信号良好";
  if (value >= -78) return "信号一般";
  return "信号较弱";
}

function normalizeWifiList(aps) {
  const seen = new Map();
  for (const ap of aps || []) {
    const ssid = String(ap.ssid || "").trim();
    if (!ssid) continue;
    const rssi = Number(ap.rssi);
    const old = seen.get(ssid);
    if (!old || (Number.isFinite(rssi) && rssi > Number(old.rssi))) {
      seen.set(ssid, { ssid, rssi: Number.isFinite(rssi) ? rssi : null });
    }
  }
  return Array.from(seen.values()).sort((a, b) => (b.rssi ?? -999) - (a.rssi ?? -999));
}

function renderWifiList(targetId, aps, inputId) {
  const box = $(targetId);
  box.innerHTML = "";
  const list = normalizeWifiList(aps);
  if (!list.length) {
    const empty = document.createElement("div");
    empty.className = "wifi-empty";
    empty.innerHTML = "<strong>没有扫描到 WiFi</strong><span>确认设备在线，或让设备靠近路由器后重新扫描。</span>";
    box.appendChild(empty);
    return;
  }

  const menu = document.createElement("div");
  menu.className = "wifi-menu";

  const head = document.createElement("div");
  head.className = "wifi-menu-head";
  head.innerHTML = `<span>选择 WiFi</span><small>${list.length} 个网络</small>`;
  menu.appendChild(head);

  for (const ap of list) {
    const level = wifiSignalLevel(ap.rssi);
    const button = document.createElement("button");
    button.className = "wifi-menu-row";
    button.type = "button";
    button.onclick = () => {
      $(inputId).value = ap.ssid;
      box.querySelectorAll(".wifi-menu-row").forEach(item => item.classList.remove("selected"));
      button.classList.add("selected");
      $(inputId).focus();
    };

    const bars = document.createElement("span");
    bars.className = `wifi-bars level-${level}`;
    bars.setAttribute("aria-hidden", "true");
    for (let i = 1; i <= 4; i += 1) {
      const bar = document.createElement("i");
      bar.className = i <= level ? "on" : "";
      bars.appendChild(bar);
    }

    const text = document.createElement("span");
    text.className = "wifi-menu-text";
    const name = document.createElement("strong");
    name.textContent = ap.ssid;
    const meta = document.createElement("small");
    meta.textContent = `${wifiSignalText(ap.rssi)} · ${ap.rssi ?? "--"} dBm`;
    text.append(name, meta);

    const arrow = document.createElement("span");
    arrow.className = "wifi-menu-arrow";
    arrow.textContent = "›";

    button.append(bars, text, arrow);
    menu.appendChild(button);
  }

  box.appendChild(menu);
}
async function scanCurrentDeviceWifi() {
  if (!selectedId || !(snapshot.selected && snapshot.selected.online)) {
    setDeviceProvisionStatus("请先选择一个在线设备", "warning");
    notify("操作失败：请先选择一个在线设备", "warning");
    return;
  }
  wifiScanWait = { deviceId: selectedId, startMs: Date.now(), timeoutMs: 15000 };
  updateButtonBusy($("deviceScanWifi"), true, "扫描中...", "扫描附近 WiFi");
  renderWifiList("deviceWifiList", [], "deviceProvisionSsid");
  setDeviceProvisionStatus("正在让设备扫描周围 WiFi", "temp_high");
  try {
    await postJson("/api/command", { command: "wifi_scan", device_id: selectedId });
    notify("已发送扫描命令，等待设备返回 WiFi 列表", "temp_high");
    await refresh();
  } catch (error) {
    wifiScanWait = null;
    updateButtonBusy($("deviceScanWifi"), false, "", "扫描附近 WiFi");
    setDeviceProvisionStatus("扫描命令发送失败", "fault");
    notify("扫描失败：主机没有成功下发命令", "fault");
  }
}
function updateWifiScanWait() {
  if (!wifiScanWait) return;
  const elapsed = Date.now() - wifiScanWait.startMs;
  const current = snapshot.devices.find(device => deviceKey(device) === wifiScanWait.deviceId);
  if (current && current.wifi_scan_status === "error") {
    renderWifiList("deviceWifiList", [], "deviceProvisionSsid");
    setDeviceProvisionStatus(`扫描失败：${current.wifi_scan_error || "设备返回错误"}`, "fault");
    notify(`扫描失败：${current.wifi_scan_error || "设备返回错误"}`, "fault");
    wifiScanWait = null;
    updateButtonBusy($("deviceScanWifi"), false, "", "扫描附近 WiFi");
    return;
  }
  if (current && Array.isArray(current.wifi_scan)) {
    renderWifiList("deviceWifiList", current.wifi_scan, "deviceProvisionSsid");
    setDeviceProvisionStatus(`扫描完成 ${current.wifi_scan.length} 个`, "normal");
    notify(`扫描完成：发现 ${current.wifi_scan.length} 个 WiFi`, "normal");
    wifiScanWait = null;
    updateButtonBusy($("deviceScanWifi"), false, "", "扫描附近 WiFi");
    return;
  }
  if (elapsed >= wifiScanWait.timeoutMs) {
    setDeviceProvisionStatus("扫描超时，请确认设备在线并靠近路由器", "fault");
    notify("扫描失败：设备未在规定时间内返回 WiFi 列表", "fault");
    wifiScanWait = null;
    updateButtonBusy($("deviceScanWifi"), false, "", "扫描附近 WiFi");
    return;
  }
  setDeviceProvisionStatus(`正在扫描周围 WiFi ${Math.ceil((wifiScanWait.timeoutMs - elapsed) / 1000)}s`, "temp_high");
}
function deviceProvisionBody() {
  const ssid = $("deviceProvisionSsid").value.trim();
  const password = $("deviceProvisionPassword").value;
  if (!ssid) {
    setDeviceProvisionStatus("缺少 WiFi 名称", "warning");
    notify("连接失败：请先选择或输入 WiFi 名称", "warning");
    $("deviceProvisionSsid").focus();
    return null;
  }
  return { ssid, password };
}

async function onlineProvisionCurrentDevice() {
  const targetId = selectedId;
  const targetDevice = snapshot.selected;
  if (!targetId || !(targetDevice && targetDevice.online)) {
    setDeviceProvisionStatus("请先选择一个在线设备", "warning");
    notify("操作失败：请先选择一个在线设备", "warning");
    return;
  }
  const body = deviceProvisionBody();
  if (!body) return;
  const ok = window.confirm(`确定让「${deviceName(targetId)}」切换到 WiFi：${body.ssid} 吗？终端会保存后重启。`);
  if (!ok) return;

  const provisionButton = $("deviceOnlineProvision");
  setDeviceProvisionStatus("正在下发", "temp_high");
  updateButtonBusy(provisionButton, true, "连接中...", "连接此 WiFi");
  try {
    const result = await postJson("/api/provision/online-update", { device_id: targetId, ...body });
    if (result.ok) {
      startProvisionWait("online", targetId);
      notify("WiFi 已下发，正在等待设备重连", "temp_high");
      await refresh();
    } else {
      setDeviceProvisionStatus("下发失败", "fault");
      notify(`连接失败：${result.error || "设备未在线"}`, "fault");
      window.alert(`在线改 WiFi 失败：${result.error || "设备未在线"}`);
    }
  } catch (error) {
    setDeviceProvisionStatus("下发失败", "fault");
    notify("连接失败：命令没有成功发送到设备", "fault");
    window.alert("在线改 WiFi 失败，请确认设备仍然在线。");
  } finally {
    updateButtonBusy(provisionButton, false, "", "连接此 WiFi");
  }
}

async function clearCurrentDeviceWifi() {
  if (!selectedId || !(snapshot.selected && snapshot.selected.online)) {
    setDeviceProvisionStatus("请先选择一个在线设备", "warning");
    notify("操作失败：请先选择一个在线设备", "warning");
    return;
  }
  const ok = window.confirm(`确定清除「${deviceName(selectedId)}」保存的 WiFi 吗？设备会重启并进入配网。`);
  if (!ok) return;
  setDeviceProvisionStatus("正在下发", "temp_high");
  try {
    await postJson("/api/command", { command: "wifi_clear", device_id: selectedId });
    notify("已发送清除 WiFi 命令，等待设备重启", "temp_high");
    startProvisionWait("online", selectedId);
    await refresh();
  } catch (error) {
    setDeviceProvisionStatus("下发失败", "fault");
    window.alert("清除当前设备 WiFi 失败，请确认设备仍然在线。");
  }
}

const themeToggle = $("themeToggle");
themeToggle.onclick = () => {
  const currentTheme = document.documentElement.getAttribute("data-theme");
  const newTheme = currentTheme === "dark" ? "light" : "dark";
  document.documentElement.setAttribute("data-theme", newTheme);
  localStorage.setItem("graesp-theme", newTheme);
  drawChart();
};

const savedTheme = localStorage.getItem("graesp-theme") || "light";
document.documentElement.setAttribute("data-theme", savedTheme);

window.addEventListener("resize", drawChart);

$("homeNav").onclick = showHome;
$("backHome").onclick = showHome;
$("clearOffline").onclick = () => clearOfflineDevices().catch(() => {});
$("renameSelected").onclick = renameSelectedDevice;
$("deleteSelected").onclick = () => {
  if (selectedId) deleteDevice(selectedId).catch(() => {});
};
$("deviceSearch").oninput = event => {
  deviceSearch = event.target.value;
  renderDevices();
  renderHome();
};
$("filterAll").onclick = () => setFilter("all");
$("filterOnline").onclick = () => setFilter("online");
$("filterAlert").onclick = () => setFilter("alert");
$("deviceOnlineProvision").onclick = () => onlineProvisionCurrentDevice().catch(() => {});
$("deviceClearWifi").onclick = () => clearCurrentDeviceWifi().catch(() => {});
$("deviceScanWifi").onclick = () => scanCurrentDeviceWifi().catch(() => {});

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

$("cmdSelfTest").onclick = () => sendCommand("self_test");
$("cmdResetStats").onclick = () => sendCommand("reset_stats");
$("cmdBroadcast").onclick = () => sendCommand("refresh", true);

setInterval(() => refresh().catch(() => {}), 1000);
refresh().catch(() => {});










