# GraEsp 云端网页 MVP

这个目录是一个纯静态云端网页原型，用浏览器 MQTT over WebSocket 直接连接公共 Broker。

## 当前连接

- Broker: `wss://broker.emqx.io:8084/mqtt`
- Topic 前缀: `graesp/lin`
- 遥测订阅: `graesp/lin/+/telemetry`
- 设备命令: `graesp/lin/<device_id>/cmd`

## 本地预览

直接打开 `index.html`，或用任意静态服务器打开本目录。

## 部署建议

可以部署到 GitHub Pages、Cloudflare Pages、Vercel、Netlify 等静态托管平台。因为页面是 HTTPS，默认使用 `wss://broker.emqx.io:8084/mqtt`，避免浏览器拦截普通 `ws://`。

## 功能

- 云端查看 MQTT 遥测
- 设备列表和在线/离线判断
- 报警状态、温度、电流、电量、自检故障显示
- MQTT 下发自检、清统计、扫描 WiFi、清除 WiFi、改 WiFi

注意：公共 Broker 仅用于毕业设计测试演示，不要发送隐私数据。
