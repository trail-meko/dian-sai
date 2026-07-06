# 任务：从零重建 STM32F407 反向比例运放增益自动测试系统

## 角色

你是一名嵌入式固件工程师。请在 **Keil µVision** 上，为 **STM32F407ZGT6**（正点原子 F407 开发板 + 运放增益测试子板）从零搭建完整工程，实现运放增益自动测试功能。

## 还原精度说明

- **功能等价**：行为、指标、人机交互与验收附录 D 一致即可；源文件划分、函数命名、寄存器写法可与参考工程不同。
- **固定硬件**：必须能在现有实验板（见附录 A）上烧录运行。
- **技术栈不可更换**：Keil µVision + STM32F4 Standard Peripheral Library + `startup_stm32f40_41xxx.s` + CMSIS-DSP 预编译库 `arm_cortexM4lf_math.lib`（`arm_cfft_f32` len2048）。

## 唯一目标

实现 **附录 D 全部验收项（D.1～D.13）**。全部通过后，方可视为完成。

## 系统一句话

DAC(PA4) 输出 1～2 kHz 正弦/三角激励 → 经耦合电容驱动板载反相比例运放 → 同步采集 Ui/Uo(PA5/PA6) → 2048 点 FFT 解算 RMS/增益/相位 → OLED 5 行显示；手动模式 Flash 标定 DAC Vpp 使 Ui≈100 mV RMS；自动模式 PI 闭环维持 Uo 1.5 V RMS；跳线改变电路后软件自动识别 `CFG:CHANGED`。

## 架构硬约束（违反即返工）

| # | 约束 |
|---|------|
| 1 | **低功耗主循环**：`main()` 初始化后 `while(1) { __WFI(); }`；重业务在 **PendSV** 中执行，**禁止**在主循环或 DMA ISR 内跑 FFT/PI/显示逻辑 |
| 2 | **信号链**：DAC(PA4) → 耦合电容 → 运放 → Ui(PA5) / Uo(PA6) ADC 采集 |
| 3 | **DAC 方案**：TIM2 TRGO + DMA1_Stream5 → DAC1 CH1(PA4)；固定 **256 点/周期**；正弦用 Flash ROM 表 `sine_ref_256[256]`；改频率只改 TIM2 ARR |
| 4 | **ADC 方案**：TIM3 触发 ADC1 **扫描模式** CH5+CH6，DMA2_Stream0 搬运 2048×2 半字；DMA TC ISR **仅**停采样并通知调度器，**不跑 FFT** |
| 5 | **FFT**：2048 点 float + **Hann 窗** + `arm_cfft_f32`；RMS 由基波幅值 / 波形形状因子换算 |
| 6 | **增益符号（增强）**：\|Δφ\| > 90° 时，显示 Av 为负（反相运放物理语义） |
| 7 | **CFG 检测（增强）**：**不读** S2/S3/S4 的 GPIO；通过实测 Av 相对变化 ≥5% 连续 3 帧判定 `CFG:CHANGED` |
| 8 | **故障 LED（增强）**：PF9 低电平点亮；有故障标志时闪烁，正常时常灭 |
| 9 | **完成态配置**：`APP_MEASURE_ENABLE=1`，`APP_OLED_ENABLE=1`（上电 500 ms 后启动 ADC，帧间隔 500 ms） |
| 10 | **禁止编译**：`HARDWARE/ADC/adc.c`、`dac.c`、`TIMER/timer.c`、`LCD/*`、`OLED/oled.c`、`USER/DISP.c` 及工程中未编入的遗留文件 |

## 调度架构（与 F103 第3版不同，必须遵守）

```
SysTick 1ms  → 按键去抖计数、ADC 启动延时、采集间隔倒计时
DMA2_Stream0 TC → AppScheduler_SampleReady() → 挂起 PendSV
PendSV       → OLED 延迟初始化 / FFT+PI / 按键处理 / OLED 刷新
main         → __WFI() 等待中断
```

## 强制实现顺序

每完成一阶段，对照附录 D 中对应验收编号自检，**全部打勾后再进入下一阶段**。

```
阶段 1  工程骨架 + app_config.h + main + app_scheduler(SysTick/PendSV) + LED_Init
        main 初始化链；while(1) __WFI()
阶段 2  wave_gen — Flash ROM 正弦表 + TIM2/DMA1；默认 1 kHz 正弦；PA4 波形稳定
阶段 3  adc_dma — ADC1 扫描 CH5/CH6；DMA2 TC 仅通知调度器（不在此跑 FFT）
阶段 4  app_measure — PendSV 中调用；D.5 用 Keil Watch 验收（非 OLED 全参）
阶段 5  app_control — 手动固定 Vpp / 自动 PI + 增益极性逻辑
阶段 6  app_display + oled_i2c — 5 行全参数刷新
阶段 7  app_keys — 三键防抖（SysTick 10ms）+ 频率/模式/波形切换
阶段 8  增强 — CFG 检测 + PF9 故障 LED + Flash 校准说明
```

## 建议目录结构

```
USER/DSP_FFT.uvprojx
USER/           main.c, stm32f4xx_it.c, app_*.c/h, dwt_delay.c
HARDWARE/       wave_gen, adc_dma, oled_i2c, key, led
SYSTEM/         delay, sys, usart（可选，main 可不初始化）
CORE/           startup_stm32f40_41xxx.s, system_stm32f4xx.c
FWLIB/          F4 SPL 外设库
DSP_LIB/        arm_cortexM4lf_math.lib, Include/arm_math.h 等
Docs/           本重建文档包
```

## 完成定义（Definition of Done）

- [ ] Keil 编译 **0 Error**，目标芯片 **STM32F407ZG**，Flash 算法 `STM32F4xx_1024`
- [ ] 附录 D 验收项 **D.1～D.13 全部通过**
- [ ] `APP_MEASURE_ENABLE=1`，`APP_OLED_ENABLE=1`
- [ ] 手动模式 Ui RMS ≈ **100 mV**（示波器校准后 `Calib_Save()`）
- [ ] 自动模式 Uo RMS 收敛至 **1.5 V ±10%**
- [ ] 反相运放下 Av 为负、Ph ≈ **±180°**（允许测量噪声）
- [ ] 拨动跳线 S2/S3/S4 后 OLED L5 显示 **CFG:CHANGED**
- [ ] 故障时 PF9 LED **闪烁**

## 工作方式（必须遵守）

1. **先输出**「模块清单 + 8 阶段计划 + 风险点」，再写代码。
2. **每阶段结束**列出本阶段验收项及自测方法（示波器/OLED/Keil Watch/操作步骤）。
3. **不得跳过** ADC/FFT 直接写 OLED 占位数据。
4. 引脚、宏、算法参数以 **附录 A/C** 为准；**不要**照搬 F103 第3版的 PA2/PA3、TIM6、cr4_fft、TIM7 后台调度或 11 档 N/PSC/ARR 频率表。
5. 附录 B 中标注「重建须新增」的增强模块（CFG、故障 LED、增益极性）必须在阶段 8 前完成。
6. 交付物：可编译 Keil 工程 + 简短 `README` 说明烧录与验收步骤。

## 易错点（单独列出）

| 错误做法 | 正确做法 |
|----------|----------|
| 使用 F103 `cr4_fft_1024_stm32` | 使用 CMSIS-DSP `arm_cfft_f32` len2048 |
| PA2/PA3 ADC | **PA5/PA6** (ADC1_IN5/IN6) |
| TIM6 + DMA2_Ch3 DAC | **TIM2 + DMA1_Stream5** DAC |
| TIM7 后台帧处理 + 空 while(1) | **SysTick + PendSV** + `__WFI()` |
| 11 档独立波形点数 N 查表 | **固定 256 点**，改 TIM2 ARR 换频 |
| `APP_MEASURE_ENABLE=0` 作为完成态 | 完成态必须为 **1** |
| 在 DMA ISR 内跑 FFT | FFT 仅在 **PendSV** 中执行 |

## 附录索引

| 文件 | 内容 |
|------|------|
| [REBUILD_APPENDIX_A.md](REBUILD_APPENDIX_A.md) | 硬件接线拓扑与引脚表 |
| [REBUILD_APPENDIX_B.md](REBUILD_APPENDIX_B.md) | 模块划分、API、数据流、中断分工 |
| [REBUILD_APPENDIX_C.md](REBUILD_APPENDIX_C.md) | 算法、宏参数、频率公式、增强逻辑 |
| [REBUILD_APPENDIX_D.md](REBUILD_APPENDIX_D.md) | **验收测试清单（完成门禁）** |

---

> 将本文件与四个附录一并交给 Agent。Agent 应先通读附录 D，再按阶段实现。
