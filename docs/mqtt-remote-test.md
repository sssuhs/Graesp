# MQTT 远程控制测试说明

## 作用

MQTT 用来解决“上位机和下位机不在同一个 WiFi 也能通信”的问题。ESP32-S3 下位机主动连接公共 Broker，上位机、网页或 App 也连接同一个 Broker。

当前测试 Broker：

```text
broker.emqx.io
端口：1883
用户名/密码：无
```

当前 topic 前缀：

```text
graesp/lin
```

## Topic

假设设备 ID 是 `GRAESP001`，则：

```text
graesp/lin/GRAESP001/telemetry    下位机上传精简遥测
graesp/lin/GRAESP001/cmd          指定设备命令
graesp/lin/broadcast/cmd          广播命令
```

实际设备 ID 以串口日志里的 `device_id` 为准。

## 订阅遥测

Windows 已安装 Mosquitto 客户端时，可以在 PowerShell 里运行：

```powershell
mosquitto_sub -h broker.emqx.io -p 1883 -t "graesp/lin/+/telemetry" -v
```

看到类似下面内容表示远程遥测通了：

```json
{"device_id":"GRAESP...","state":"normal","rise":2.30,"current":7.80,"prob":0.12,"battery":88}
```

## 下发命令

系统自检：

```powershell
mosquitto_pub -h broker.emqx.io -p 1883 -t "graesp/lin/GRAESP001/cmd" -m '{"command":"self_test"}'
```

清除统计：

```powershell
mosquitto_pub -h broker.emqx.io -p 1883 -t "graesp/lin/GRAESP001/cmd" -m '{"command":"reset_stats"}'
```

扫描附近 WiFi：

```powershell
mosquitto_pub -h broker.emqx.io -p 1883 -t "graesp/lin/GRAESP001/cmd" -m '{"command":"wifi_scan"}'
```

清除 WiFi 并重启进入配网：

```powershell
mosquitto_pub -h broker.emqx.io -p 1883 -t "graesp/lin/GRAESP001/cmd" -m '{"command":"wifi_clear"}'
```

广播自检：

```powershell
mosquitto_pub -h broker.emqx.io -p 1883 -t "graesp/lin/broadcast/cmd" -m '{"command":"self_test"}'
```

## 注意

公共 Broker 免费但不保证稳定，也没有隐私保护。毕业设计演示可以用，正式产品应换成私有 Broker 或带账号认证的云服务。
