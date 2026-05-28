# GraEsp 云端主机网页

这个目录是一个纯静态云端网页，用浏览器 MQTT over WebSocket 直接连接 Broker。打开后先进入主机总览页面，设备通过 MQTT 上报后自动出现在设备列表，点击某个设备再进入详情和远程控制。

## 当前连接

- Broker: `wss://broker.emqx.io:8084/mqtt`
- Topic 前缀: `graesp/lin`
- 遥测订阅: `graesp/lin/+/telemetry`
- 设备命令: `graesp/lin/<device_id>/cmd`

## 本地预览

直接打开 `index.html`，或用任意静态服务器打开本目录。

当前本地预览地址：

`http://127.0.0.1:8090`

这只是本机预览地址，不是云端地址。真正演示云端网页时，把本目录里的 `index.html`、`app.js`、`style.css` 和 `README.md` 上传到 GitHub Pages、Cloudflare Pages、Vercel、Netlify 或自己的服务器即可。

## 部署建议

可以部署到 GitHub Pages、Cloudflare Pages、Vercel、Netlify 等静态托管平台。因为页面是 HTTPS，默认使用 `wss://broker.emqx.io:8084/mqtt`，避免浏览器拦截普通 `ws://`。

项目根目录已包含 `.github/workflows/pages.yml`。把整个 `D:\EspProjects\GraEsp` 仓库推到 GitHub 后，在仓库 `Settings -> Pages` 里选择 GitHub Actions，后续推送 `cloud-web` 改动会自动部署。

## 功能

- 云端主页面总览：终端总数、在线、离线、异常
- MQTT 连接配置：Broker WebSocket 地址和 Topic 前缀
- 首次/离线配网入口：提示连接 `GraEsp-XXXX` 并打开 `http://192.168.4.1`
- 设备列表和在线/延迟/离线判断
- 点击设备进入详情页
- 报警状态、温度、电流、电量、自检故障显示
- 实时趋势曲线
- MQTT 下发自检、清统计、扫描 WiFi、清除 WiFi、改 WiFi

## 页面规则

- 主页面保留首次/离线配网入口，但配网动作由设备热点页面 `http://192.168.4.1` 完成。
- 每个设备的 WiFi 扫描、改网、清除 WiFi 都在该设备详情页里做。
- 这是云端 MQTT 网页；本地 `desktop-app` 仍保留 MQTT + UDP 兼容主机服务。

注意：公共 Broker 仅用于毕业设计测试演示，不要发送隐私数据。
