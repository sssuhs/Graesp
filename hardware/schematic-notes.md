# GraEsp 下位机原理图设计说明

本文件用于指导第一版 PCB 原理图绘制。
目标模块：ESP32-S3-WROOM-1-N16R8。

## 1. 系统模块

下位机终端 PCB 建议包含以下模块：

| 模块 | 作用 |
|---|---|
| ESP32-S3 最小系统 | 主控、ADC 采样、Wi-Fi、报警逻辑 |
| USB-C 接口 | 5 V 输入、锂电池充电输入、程序下载和调试 |
| 锂电池供电模块 | 单节锂电池供电 |
| 3.3 V 稳压模块 | 给 ESP32-S3 和传感器供电 |
| 3 路 NTC 测温前端 | 2 路导线外皮温度，1 路环境温度 |
| 电池电压检测 | 采集电池电压，用于电量显示和低压判断 |
| LED 报警 | 本地状态指示 |
| 蜂鸣器报警 | 本地声音报警 |
| 测试点 | 下载、调试、测量和后期标定 |

第一版 PCB 不做这些内容：

- 不接入交流强电。
- 不使用电流互感器或霍尔电流传感器作为算法输入。
- 不做 OLED 显示屏。
- 不做 SD 卡。
- 不做有线连接上位机的数据链路，数据通过 Wi-Fi 传输。

## 2. ESP32-S3 最小系统

主控使用 ESP32-S3-WROOM-1-N16R8 模块。

必要连接如下：

| 功能 | 连接方式 |
|---|---|
| 3V3 | 接 3.3 V 稳压输出，旁边放去耦电容 |
| GND | 接整板地，建议铺地平面 |
| EN | 10 kOhm 上拉到 3.3 V，并接复位按键到 GND；按手册外围设计图建议，可加 1 uF 到 GND 形成上电 RC 延时 |
| GPIO0 | BOOT 按键到 GND，用于下载模式 |
| GPIO19 | USB D- |
| GPIO20 | USB D+ |
| GPIO43 | UART0 TX 测试点 |
| GPIO44 | UART0 RX 测试点 |

第一版 PCB 尽量不要把下面这些脚当普通 IO 用：

| 引脚 | 原因 |
|---|---|
| GPIO0、GPIO3、GPIO45、GPIO46 | 启动配置脚，外部电路复杂容易影响启动 |
| GPIO19、GPIO20 | 原生 USB D- / D+ |
| GPIO22-GPIO34 | ESP32-S3-WROOM-1 模组未引出这些 GPIO，不要在原理图中分配 |
| GPIO35-GPIO37 | 手册注明在集成 Octal SPI PSRAM 的模组中已连接到内部 PSRAM，N16R8 不可用于其他功能 |
| GPIO43、GPIO44 | UART0，建议留作下载和调试测试点 |

## 3. USB-C 接口

USB-C 用于供电、充电输入、程序下载和调试。

第一版推荐连接：

| USB-C 引脚 | 连接方式 |
|---|---|
| VBUS | 作为 5 V 输入，可以加自恢复保险丝 |
| GND | 接系统地 |
| CC1、CC2 | 各接 5.1 kOhm 到 GND，表示设备模式 |
| D- | 接 ESP32-S3 GPIO19，中间可串 22 Ohm 电阻 |
| D+ | 接 ESP32-S3 GPIO20，中间可串 22 Ohm 电阻 |
| Shield | 可直接接地，或按 ESD/外壳接地策略处理 |

建议：

- USB 口附近加 ESD 保护器件。
- D+、D- 尽量短，并按差分线思路布线。
- USB 数据线不要有长支路。

## 4. 锂电池与 3.3 V 供电

下位机使用单节锂电池供电。

第一版推荐电源链路：

```text
USB-C 5V -> TP4056 充电 -> 锂电池 BAT+
BAT+ -> 电源路径 -> Vout -> 小型滑动电源开关 SW2 -> Vin -> RT9013-33GB -> 3V3 -> ESP32-S3 和传感器
```

器件选择建议：

| 功能 | 便宜方案 | 更稳方案 |
|---|---|---|
| 充电芯片 | TP4056，PROG 电阻 12 kOhm，约 100 mA 充电电流 | MCP73831 / MCP73871 / 带电源路径管理的充电芯片 |
| 电池保护 | 带保护板电池，或 DW01A + FS8205A | 集成保护和电源路径方案 |
| 3.3 V 稳压 | RT9013-33GB | 低静态电流 Buck 或 Buck-Boost |
| 电源开关 | 小型滑动开关 DSHP01TSGER，串在 Vout 到 Vin 之间 | 负载开关或高边开关 |

注意：

- ESP32-S3 打开 Wi-Fi 时电流峰值较大，3.3 V 稳压芯片要留足余量。
- 电池供电版本不建议用 AMS1117，因为压差和静态电流都不理想。
- 本版电源开关位于 RT9013 输入前，开关断开时 USB 仍可通过 TP4056 给电池充电，但 ESP32-S3 不上电。
- ESP32-S3 模组附近放 10 uF 和 100 nF 去耦电容。
- 3.3 V 稳压输出端建议放 22 uF 到 47 uF 电容。

## 5. NTC 测温前端

固件当前假设 NTC 分压方向如下：

```text
GPIO15/IO15 -> 10 kOhm 固定电阻 -> ADC 采样点 -> 10 kOhm NTC -> GND
```

对应引脚：

| 通道 | GPIO | ADC 通道 | 含义 |
|---|---:|---|---|
| NTC1 | GPIO4 | ADC1_CH3 | 导线外皮测温点 1 |
| NTC2 | GPIO5 | ADC1_CH4 | 导线外皮测温点 2 |
| NTC3 | GPIO6 | ADC1_CH5 | 环境温度 |

每一路推荐电路：

```text
IO15 -- 10 kOhm -- ADC采样点 -- 10 kOhm NTC -- GND
                         |
                       100 nF
                         |
                        GND
```

建议：

- ADC 采样点到 ESP32-S3 ADC 引脚之间可串 100 Ohm 电阻。
- ADC 采样点到 GND 放 100 nF 电容，电容靠近 ESP32-S3 ADC 引脚。
- NTC 模拟走线远离蜂鸣器、USB 和电源开关节点。
- 三路固定电阻尽量使用同一阻值和同一精度。
- 固定电阻建议选 1% 精度。

## 6. NTC 分压供电控制

GPIO15 已分配为 `APP_NTC_POWER_GPIO`。

固件逻辑：

```text
GPIO15 高电平 -> 打开 NTC 分压供电
GPIO15 低电平 -> 关闭 NTC 分压供电
```

本版采用 GPIO15 直接给三路 NTC 分压电路供电，不再额外增加 MOS 或负载开关。

```text
GPIO15/IO15 -> R_FIXED1 -> IO4 ADC 节点 -> NTC1 -> GND
GPIO15/IO15 -> R_FIXED2 -> IO5 ADC 节点 -> NTC2 -> GND
GPIO15/IO15 -> R_FIXED3 -> IO6 ADC 节点 -> NTC3 -> GND
```

说明：

- 三路 10 kOhm 固定电阻与 10 kOhm NTC 在常温下总电流约 0.5 mA，GPIO15 可以直接驱动。
- GPIO15 只给 NTC 分压供电，不再连接其他负载。
- 固件采样前将 GPIO15 置高，等待 2 ms 后读取 ADC，采样完成后将 GPIO15 置低。
- 如果后续追求更规范的电源隔离，可升级为 PMOS + NMOS 高边开关或负载开关芯片。

## 7. 电池电压检测

固件当前假设电池分压如下：

```text
BAT+ -> 200 kOhm -> ADC采样点 -> 100 kOhm -> GND
```

对应引脚：

| 功能 | GPIO | ADC 通道 |
|---|---:|---|
| 电池电压检测 | GPIO7 | ADC1_CH6 |

说明：

- 分压比为 3:1，4.2 V 电池电压进入 ADC 后约为 1.4 V。
- 4.2 V 时分压电流约 14 uA，第一版可以接受。
- ADC 采样点对 GND 可加 100 nF 电容。
- 如果后面要进一步降低功耗，可以给电池分压也加 MOS 控制。

## 8. LED 报警电路

对应引脚：

| 功能 | GPIO |
|---|---:|
| 状态 LED | GPIO18 |

推荐电路：

```text
GPIO18 -> 1 kOhm -> LED -> GND
```

固件状态：

| 状态 | LED 表现 |
|---|---|
| normal | 熄灭 |
| warning | 慢闪 |
| overload | 快闪 |
| fault | 很慢闪烁 |

## 9. 蜂鸣器报警电路

对应引脚：

| 功能 | GPIO |
|---|---:|
| 蜂鸣器 | GPIO17 |

如果使用有源蜂鸣器，推荐电路：

```text
3.3 V -> 有源蜂鸣器正极
有源蜂鸣器负极 -> NMOS 漏极
NMOS 源极 -> GND
GPIO17 -> 100 Ohm 到 1 kOhm -> NMOS 栅极
NMOS 栅极 -> 100 kOhm -> GND
```

注意：

- 蜂鸣器不要直接由 ESP32-S3 GPIO 驱动，建议用 NMOS 或 NPN 三极管。
- 如果是电磁式或感性蜂鸣器，需要加续流二极管，或者选择内部带驱动保护的蜂鸣器。
- 如果使用无源蜂鸣器，后续固件需要把 GPIO17 改成 LEDC PWM 输出。

## 10. 测试点

建议增加以下测试点，方便焊接后调试：

| 信号 | 用途 |
|---|---|
| 3V3 | 检查 3.3 V 电源 |
| GND | 示波器/万用表地线 |
| EN | 复位和调试 |
| GPIO0 | 下载模式 |
| GPIO43 UART0 TX | 串口日志 |
| GPIO44 UART0 RX | 串口下载/调试 |
| GPIO4/GPIO5/GPIO6/GPIO7 | ADC 调试 |
| GPIO15 | NTC 供电控制调试 |
| GPIO17 | 蜂鸣器调试 |
| GPIO18 | LED 调试 |

## 11. 测温夹具和传感器线

PCB 不进入插排内部。NTC 安装在外置测温夹具上，夹具贴合或夹持在插排输入线外皮。

如果使用线束连接夹具和 PCB，建议信号如下：

| 线序 | 信号 |
|---|---|
| 1 | IO15/NTC_PWR |
| 2 | NTC1_ADC |
| 3 | NTC2_ADC |
| 4 | NTC_ENV_ADC |
| 5 | GND |

注意：

- 如果不想用连接器，也可以预留焊盘。
- 夹具线建议使用柔软硅胶线。
- 夹具入口和外壳入口要做应力释放，避免拉断焊点。
- 两个导线测温 NTC 尽量结构对称，保证测点温差特征有意义。

## 12. 手册校对结论

根据 docs/esp32-s3-wroom-1_wroom-1u_datasheet_cn.pdf：

- ESP32-S3-WROOM-1-N16R8 为 16 MB Flash + 8 MB Octal SPI PSRAM 版本，推荐工作环境温度为 -40 到 65 摄氏度。
- GPIO4、GPIO5、GPIO6、GPIO7 分别对应 ADC1_CH3、ADC1_CH4、ADC1_CH5、ADC1_CH6。
- GPIO19 / GPIO20 分别对应 USB_D- / USB_D+。
- GPIO0、GPIO3、GPIO45、GPIO46 是启动配置相关管脚，原理图中避免复杂外部负载。
- GPIO35、GPIO36、GPIO37 在 N16R8 这类集成 Octal SPI PSRAM 的模组中已连接至内部 PSRAM，不可用于其他功能。
- 手册外围设计图建议 EN 管脚增加 RC 延时，典型参考为 10 kOhm 上拉和 1 uF 到 GND。

## 13. 打板前检查清单

下单 PCB 前检查：

- ESP32-S3 型号是 N16R8，封装和模组方向正确。
- USB D+ / D- 分别接 GPIO20 / GPIO19。
- GPIO0 只作为 BOOT 按键使用，没有被复杂电路拖住。
- EN 有 10 kOhm 上拉、复位按键，并预留 1 uF 到 GND 的 RC 延时电容。
- NTC 分压方向与固件公式一致，三路固定电阻上端统一接 GPIO15/IO15。
- GPIO15 高电平直接给三路 NTC 分压电路供电，低电平关闭分压电路。
- 电池分压阻值与固件常量一致：200 kOhm / 100 kOhm，ADC 接 GPIO7。
- LED 和蜂鸣器都是高电平有效，与固件逻辑一致。
- 未把禁用或不推荐引脚当普通 IO 使用。
- 3V3、GND、EN、GPIO0、UART0、ADC、LED、蜂鸣器都有测试点。


## 14. 最新网表检查结论

根据 `docs/Netlist_Schematic1_2026-05-25.tel`，当前原理图主体连接如下：

- USB-C D- / D+ 分别接 ESP32-S3 的 GPIO19 / GPIO20。
- CC1 / CC2 通过 5.1 kOhm 下拉到 GND。
- EN 网络包含 10 kOhm 上拉、1 uF 到 GND、复位按键和 ESP32-S3 EN 引脚。
- BOOT 网络包含 GPIO0、10 kOhm 上拉和 2P 跳线帽到 GND。
- GPIO15 直接给三路 NTC 固定电阻供电。
- GPIO4、GPIO5、GPIO6 分别接三路 NTC ADC 采样点。
- GPIO7 接 200 kOhm / 100 kOhm 电池电压分压节点，并并联 100 nF 滤波电容。
- GPIO17 接蜂鸣器驱动网络，GPIO18 接状态 LED，LED 限流电阻 R18 为 1 kOhm。
- H2 串口调试排针已接出 RXD、TXD 和 GND，用于外接 USB-TTL 调试；当前 Pin1=RXD/GPIO44，Pin2=TXD/GPIO43，Pin3=GND。
- RT9013-33GB 输出 3V3 给 ESP32-S3 和外围电路。
- 小型滑动开关 SW2 串在 Vout 到 Vin 之间，用于控制整机 3.3 V 供电。

待最终确认：

- 嘉立创库中 U2 暂用 ESP32-S3-WROOM-1-N8R8，同封装下实际焊接和 BOM 型号使用 ESP32-S3-WROOM-1-N16R8。
- 当前 NTC 符号/封装可作为热敏电阻封装使用，实际焊接需选择与固件 `APP_NTC_BETA` 一致的 10 kOhm NTC。


