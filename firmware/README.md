# GraEsp 下位机固件

这是插排外置式过载监测终端的 ESP-IDF 固件工程草稿，目标芯片为 `ESP32-S3-WROOM-1-N16R8`。

第一版实现范围：

- 3 路 NTC 分压采样
- 10k B3950 NTC 温度换算
- 简单滤波
- 热特征提取
- 电流反演接口占位
- 过载状态判断
- LED 与蜂鸣器报警
- 串口输出 JSON 遥测数据

暂未接入 Wi-Fi，先保证传感器读数和判断链路能跑通。

## 复制到项目

把本目录内容复制到：

```powershell
D:\EspProjects\GraEsp\firmware
```

## 构建

```powershell
cd D:\EspProjects\GraEsp\firmware
powershell -ExecutionPolicy Bypass -NoProfile -Command "& 'D:\Program\.espressif\v6.0\esp-idf\export.ps1'; idf.py set-target esp32s3; idf.py build"
```

## 需要按 PCB 修改的位置

引脚和分压参数在 `main/include/app_config.h` 中。

