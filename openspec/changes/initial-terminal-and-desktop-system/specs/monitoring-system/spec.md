# Monitoring System Spec Delta

## ADDED Requirements

### Requirement: 终端非侵入式安装

系统 SHALL 不进入插排内部，不改变插排原有电气结构。

#### Scenario: 外置夹具安装

- GIVEN 插排处于正常可用状态
- WHEN 用户安装测温夹具
- THEN 测温夹具 SHALL 仅贴附或夹持在插排输入线外皮上
- AND 系统 SHALL 不切断、不剥开、不串接插排导线

### Requirement: 三路 NTC 温度采集

终端 SHALL 采集 3 路 NTC 温度数据，其中 2 路用于导线外皮测温，1 路用于环境温度测量。

#### Scenario: 周期采样

- GIVEN 终端已上电并完成初始化
- WHEN 到达采样周期
- THEN 终端 SHALL 读取 3 路 ADC 数据
- AND SHALL 将 ADC 数据换算为 NTC 电阻和摄氏温度

### Requirement: 热特征计算

终端 SHALL 根据 NTC 温度计算过载判断所需热特征。

#### Scenario: 特征提取

- GIVEN 终端已获得 3 路 NTC 温度
- WHEN 进行一次数据处理
- THEN 终端 SHALL 计算导线平均温度、导线最高温度、环境温度、温升、测点温差和升温速率

### Requirement: 电流反演接口

终端 SHALL 提供基于热特征的电流估计接口。

#### Scenario: 输出估计电流

- GIVEN 终端已完成热特征计算
- WHEN 调用电流反演模块
- THEN 终端 SHALL 输出估计电流值
- AND 估计结果 SHALL 能通过通信协议上传至上位机

### Requirement: 过载状态判断

终端 SHALL 根据热特征、估计电流或模型输出判断运行状态。

#### Scenario: 过载预警

- GIVEN 过载概率或阈值判断结果超过预警条件
- WHEN 终端完成一次过载判断
- THEN 终端 SHALL 将状态设置为 `warning` 或 `overload`
- AND SHALL 记录本次判断对应的关键特征值

### Requirement: 本地声光报警

终端 SHALL 在发现过载风险时进行本地声光报警。

#### Scenario: 上位机断开时报警

- GIVEN 终端未连接上位机
- WHEN 终端判断状态为 `warning` 或 `overload`
- THEN LED SHALL 进入对应报警状态
- AND 蜂鸣器 SHALL 按对应报警策略工作

### Requirement: Wi-Fi 数据上传

终端 SHALL 通过 Wi-Fi 向上位机发送实时监测数据。

#### Scenario: 实时数据上报

- GIVEN 终端已连接到上位机或同一局域网
- WHEN 完成一次采样和判断
- THEN 终端 SHALL 发送包含温度、热特征、估计电流、过载概率、状态和电池电压的 JSON 数据

### Requirement: 上位机实时显示

上位机 SHALL 实时显示终端上传的监测数据。

#### Scenario: 曲线刷新

- GIVEN 上位机正在接收有效 JSON 数据
- WHEN 新遥测数据到达
- THEN 上位机 SHALL 更新温度、温升、估计电流和过载概率曲线
- AND SHALL 更新连接状态、报警状态和电池电压显示

### Requirement: 上位机参数配置

上位机 SHALL 支持向终端下发运行参数。

#### Scenario: 修改报警阈值

- GIVEN 上位机已连接终端
- WHEN 用户修改报警阈值并确认发送
- THEN 上位机 SHALL 发送配置 JSON
- AND 终端 SHALL 校验参数并更新运行配置

### Requirement: 实验数据保存

上位机 SHALL 支持保存终端上传的实验数据。

#### Scenario: CSV 保存

- GIVEN 上位机正在接收实时数据
- WHEN 用户开启记录功能
- THEN 上位机 SHALL 将实时数据追加保存为 CSV 文件
- AND CSV SHALL 包含时间戳、3 路温度、热特征、估计电流、过载概率、状态和电池电压

### Requirement: 电池供电与低功耗

终端 SHALL 支持锂电池供电，并预留低功耗运行策略。

#### Scenario: 周期采样低功耗

- GIVEN 终端处于电池供电状态
- WHEN 系统未处于报警状态
- THEN 终端 SHALL 能按配置周期采样和上传
- AND SHALL 能在两次采样之间进入低功耗或降低 Wi-Fi 工作占空比
