# 上下位机通信协议草案

## 1. 通信方式

下位机 ESP32-S3 通过 Wi-Fi 与 PC 上位机通信。初期建议采用局域网 TCP 或 WebSocket，便于上位机实时接收数据并绘制曲线。

USB-C 用于供电、充电、固件下载和调试，不作为主要上位机数据链路。

## 2. 数据上报

下位机周期上报 JSON 数据。默认采样周期建议为 1 s。

示例：

```json
{
  "type": "telemetry",
  "timestamp_ms": 12500,
  "ntc": [58.9, 59.5, 24.7],
  "wire_temp_avg": 59.2,
  "wire_temp_max": 59.5,
  "ambient_temp": 24.7,
  "delta_temp": 34.5,
  "temp_diff": 0.6,
  "rise_rate": 0.18,
  "current_est": 12.4,
  "overload_prob": 0.91,
  "state": "warning",
  "battery_mv": 3860
}
```

字段说明：

| 字段 | 含义 |
|---|---|
| `type` | 数据类型，实时数据固定为 `telemetry` |
| `timestamp_ms` | 终端启动后的毫秒时间 |
| `ntc` | 三路 NTC 温度，单位摄氏度 |
| `wire_temp_avg` | 导线外皮平均温度 |
| `wire_temp_max` | 导线外皮最高温度 |
| `ambient_temp` | 环境温度 |
| `delta_temp` | 导线平均温度相对环境温度的温升 |
| `temp_diff` | 两个导线测点的温差 |
| `rise_rate` | 导线温度升温速率 |
| `current_est` | 反演得到的估计电流 |
| `overload_prob` | AI 模型输出的过载概率 |
| `state` | 状态，取值 `normal`、`warning`、`overload`、`sensor_error` |
| `battery_mv` | 电池电压，单位 mV |

## 3. 参数下发

上位机可向下位机发送参数设置命令。

示例：

```json
{
  "type": "config",
  "sample_interval_ms": 1000,
  "warning_prob": 0.75,
  "overload_prob": 0.9,
  "buzzer_enable": true
}
```

字段说明：

| 字段 | 含义 |
|---|---|
| `sample_interval_ms` | 采样与上报周期 |
| `warning_prob` | 预警概率阈值 |
| `overload_prob` | 过载报警概率阈值 |
| `buzzer_enable` | 是否启用蜂鸣器 |

## 4. 状态约定

- `normal`：运行正常；
- `warning`：过载风险升高，LED 提示；
- `overload`：过载报警，LED 与蜂鸣器工作；
- `sensor_error`：NTC 断线、短路或读数异常。

## 5. CSV 保存字段

上位机保存实验数据时，建议按以下字段生成 CSV：

```text
time_s,ntc1,ntc2,ntc_env,wire_temp_avg,wire_temp_max,ambient_temp,delta_temp,temp_diff,rise_rate,current_est,overload_prob,state,battery_mv
```
