# GraEsp 下位机引脚分配表

目标模块：ESP32-S3-WROOM-1-N16R8。

本文件用于固定第一版 PCB 的 ESP32-S3 引脚分配。固件中的引脚定义位于：

```text
firmware/main/include/app_config.h
```

## 1. 已分配引脚

| 功能 | GPIO | ESP-IDF 定义 | 说明 |
|---|---:|---|---|
| 导线测温 NTC1 | GPIO4 | ADC1_CH3 | 插排输入线外皮测温点 1 |
| 导线测温 NTC2 | GPIO5 | ADC1_CH4 | 插排输入线外皮测温点 2 |
| 环境温度 NTC3 | GPIO6 | ADC1_CH5 | 环境温度补偿 |
| 电池电压检测 | GPIO7 | ADC1_CH6 | 锂电池分压采样输入 |
| NTC 分压供电控制 | GPIO15 | GPIO 输出 | 控制 NTC 分压供电，用于低功耗 |
| 状态 LED | GPIO18 | GPIO 输出 | 正常、预警、过载状态指示 |
| 蜂鸣器 | GPIO17 | GPIO 输出 / LEDC PWM | 本地声音报警 |
| BOOT 按键 | GPIO0 | 启动配置脚 | 仅用于下载模式，不接复杂负载 |
| USB D- | GPIO19 | 原生 USB | 连接 USB-C D- |
| USB D+ | GPIO20 | 原生 USB | 连接 USB-C D+ |
| UART0 TX | GPIO43 | UART0 TX | 串口日志和调试测试点 |
| UART0 RX | GPIO44 | UART0 RX | 串口下载和调试测试点 |

## 2. 不建议作为普通 IO 使用的引脚

| 引脚 | 原因 |
|---|---|
| GPIO0、GPIO3、GPIO45、GPIO46 | 启动配置脚，外部电路复杂时可能导致启动失败 |
| GPIO19、GPIO20 | ESP32-S3 原生 USB D- / D+，需要留给 USB-C |
| GPIO22-GPIO34 | ESP32-S3-WROOM-1 模组未引出这些 GPIO，不要在原理图中分配 |
| GPIO35-GPIO37 | 手册注明在集成 Octal SPI PSRAM 的模组中已连接到内部 PSRAM，N16R8 不可用于其他功能 |
| GPIO43、GPIO44 | UART0，建议保留为调试和下载测试点 |

## 3. ADC 分配说明

第一版使用 ESP32-S3 自带 ADC1，不外接 ADS1115 等 ADC 芯片。

| ADC 输入 | GPIO | 用途 |
|---|---:|---|
| ADC1_CH3 | GPIO4 | NTC1，导线外皮测温点 1 |
| ADC1_CH4 | GPIO5 | NTC2，导线外皮测温点 2 |
| ADC1_CH5 | GPIO6 | NTC3，环境温度 |
| ADC1_CH6 | GPIO7 | 电池电压检测 |

选择 ADC1 的原因：

- ESP32 使用 Wi-Fi 时 ADC2 往往会受限制，ADC1 更适合长期采样。
- 本项目只需要 4 路模拟输入，ESP32-S3 自带 ADC 资源足够。
- 第一版重点是跑通测温、热特征、电流反演和过载判断，不增加外置 ADC 复杂度。

## 4. NTC 分压采样说明

三路 NTC 均采用 10k B3950 热敏电阻。

固件默认分压方向：

```text
NTC_3V3 -> 10 kOhm 固定电阻 -> ADC采样点 -> 10 kOhm B3950 NTC -> GND
```

对应固件参数：

```c
#define APP_NTC_R_FIXED_OHM 10000.0f
#define APP_NTC_R0_OHM 10000.0f
#define APP_NTC_BETA 3950.0f
#define APP_NTC_T0_K 298.15f
```

注意：

- 如果后续原理图把 NTC 和固定电阻位置反过来，固件换算公式也要跟着改。
- 固定电阻建议使用 1% 精度。
- 每个 ADC 采样点建议预留 100 nF 滤波电容。
- ADC 采样点到 ESP32-S3 引脚之间可以串 100 Ohm 电阻，增强抗干扰能力。

## 5. NTC 分压供电控制

GPIO15 用于直接给三路 NTC 分压电路供电。

固件逻辑：

```text
GPIO15 = 1：IO15 输出 3.3 V，打开三路 NTC 分压供电
GPIO15 = 0：IO15 输出低电平，关闭三路 NTC 分压供电
```

固件相关定义：

```c
#define APP_NTC_POWER_GPIO GPIO_NUM_15
#define APP_NTC_POWER_SETTLE_US 2000
```

说明：

- 每次采样前，固件会打开 NTC 分压供电。
- 打开后等待 2000 us，等分压节点稳定后再读 ADC。
- 采样完成后关闭 NTC 分压供电，减少长期功耗。
- 当前第一版原理图采用 GPIO15 直接驱动三路 10 kOhm NTC 分压，总电流约 0.5 mA。

## 6. 电池电压检测说明

电池电压检测使用 GPIO7 / ADC1_CH6。

固件默认电池分压：

```text
BAT+ -> 200 kOhm -> ADC采样点 -> 100 kOhm -> GND
```

固件相关定义：

```c
#define APP_BAT_R_TOP_OHM 200000.0f
#define APP_BAT_R_BOTTOM_OHM 100000.0f
```

说明：

- 分压比例为 3:1。
- 4.2 V 电池满电时，ADC 采样点约为 1.4 V。
- 如果后续修改分压电阻，必须同步修改固件常量。

## 7. LED 与蜂鸣器说明

| 功能 | GPIO | 默认逻辑 |
|---|---:|---|
| 状态 LED | GPIO18 | 高电平点亮 |
| 蜂鸣器 | GPIO17 | 高电平响 |

建议：

- LED 可用 `GPIO18 -> 1 kOhm -> LED -> GND`。
- 蜂鸣器不要直接接 GPIO，建议用 NMOS 或 NPN 三极管驱动。
- 如果选有源蜂鸣器，固件当前普通 GPIO 翻转即可。
- 如果选无源蜂鸣器，后续要把 GPIO17 改成 LEDC PWM 输出。

## 8. USB 与调试说明

| 功能 | GPIO | 说明 |
|---|---:|---|
| USB D- | GPIO19 | 接 USB-C D- |
| USB D+ | GPIO20 | 接 USB-C D+ |
| UART0 TX | GPIO43 | 预留测试点 |
| UART0 RX | GPIO44 | 预留测试点 |
| BOOT | GPIO0 | 接 BOOT 按键 |
| EN | EN 引脚 | 接复位按键和上拉电阻 |

建议：

- USB D+/D- 尽量短，靠近 USB-C 接口放 ESD 保护。
- UART0 TX/RX 即使用原生 USB 下载，也建议保留测试点，方便救板和串口调试。
- BOOT 和 EN 必须方便按到或至少有测试点。EN 推荐采用 10 kOhm 上拉和 1 uF 到 GND 的 RC 延时。

## 9. 手册校对结论

根据 docs/esp32-s3-wroom-1_wroom-1u_datasheet_cn.pdf：

- ESP32-S3-WROOM-1-N16R8 为 16 MB Flash + 8 MB Octal SPI PSRAM 版本。
- GPIO4、GPIO5、GPIO6、GPIO7 分别支持 ADC1_CH3、ADC1_CH4、ADC1_CH5、ADC1_CH6，适合本项目 3 路 NTC 和 1 路电池电压检测。
- GPIO19 / GPIO20 分别为 USB_D- / USB_D+。
- Strapping 管脚包括 GPIO0、GPIO3、GPIO45、GPIO46，原理图中不要给这些管脚增加复杂外部负载。
- 对于集成 Octal SPI PSRAM 的模组，GPIO35、GPIO36、GPIO37 已连接至内部 PSRAM，不可用于其他功能。
- 模组引出了 GPIO1-GPIO21、GPIO35-GPIO48，未引出 GPIO22-GPIO34。

## 10. 与固件的一致性检查

当前固件引脚定义应保持如下：

```c
#define APP_NTC1_ADC_CHANNEL ADC_CHANNEL_3
#define APP_NTC2_ADC_CHANNEL ADC_CHANNEL_4
#define APP_NTC_ENV_ADC_CHANNEL ADC_CHANNEL_5
#define APP_BAT_ADC_CHANNEL ADC_CHANNEL_6

#define APP_NTC_POWER_GPIO GPIO_NUM_15
#define APP_LED_GPIO GPIO_NUM_18
#define APP_BUZZER_GPIO GPIO_NUM_17

#define APP_USB_D_MINUS_GPIO GPIO_NUM_19
#define APP_USB_D_PLUS_GPIO GPIO_NUM_20
#define APP_BOOT_GPIO GPIO_NUM_0
#define APP_UART0_TX_GPIO GPIO_NUM_43
#define APP_UART0_RX_GPIO GPIO_NUM_44
```

画原理图或 PCB 前，要确认本文件、`schematic-notes.md` 和 `app_config.h` 三处一致。


## 11. 最新原理图一致性结论

根据 `docs/Netlist_Schematic1_2026-05-25.tel`：

- GPIO4、GPIO5、GPIO6 已接三路 NTC 分压采样点。
- GPIO7 已接电池电压检测分压点，分压为 200 kOhm / 100 kOhm。
- GPIO15 已接三路 NTC 固定电阻上端，用于直接控制 NTC 分压供电。
- GPIO17 已接蜂鸣器驱动，GPIO18 已接状态 LED，LED 限流电阻为 1 kOhm。
- GPIO0 已接 BOOT 跳线帽与 10 kOhm 上拉。
- EN 已接 10 kOhm 上拉、1 uF 到 GND 和复位按键。
- GPIO19 / GPIO20 已接 USB-C D- / D+。
- H2 串口调试排针：Pin1 接 RXD/GPIO44，Pin2 接 TXD/GPIO43，Pin3 接 GND。

待确认事项：

- 原理图库中 ESP32-S3 模组型号仍显示 N8R8；封装一致时，最终焊接和 BOM 使用 N16R8。
- NTC 当前符号/封装可沿用，实际焊接时使用与固件 Beta 参数一致的 10 kOhm NTC。



