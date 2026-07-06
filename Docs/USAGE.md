# 反向比例运放增益自动测试系统 — 使用说明

## 硬件连接

| 测试点 | MCU 引脚 | 说明 |
|--------|----------|------|
| TP_DAC | PA4 | DAC 输出，经耦合电容接运放输入 |
| TP_Ui  | PB0 | ADC1_IN8 采集运放输入交流信号 |
| TP_Uo  | PB1 | ADC1_IN9 采集运放输出交流信号 |
| KEY1   | PB10 | 频率切换 |
| KEY2   | PB11 | 手动/自动模式 |
| KEY3   | PB12 | 正弦/三角波 |
| OLED SCL | PB8 | I2C 时钟 |
| OLED SDA | PB9 | I2C 数据 |

MCU：**STM32F407ZGT6**（168 MHz SYSCLK；DAC/ADC 定时器 84 MHz）。

## 按键操作

- **KEY1**：频率循环 1.0kHz → 1.1kHz → … → 2.0kHz → 1.0kHz（步进 100Hz）
- **KEY2**：手动模式 ↔ 自动模式
- **KEY3**：正弦波 ↔ 三角波

## 工作模式

- **手动模式**：DAC 幅度固定（`MANUAL_GAIN_SCALE_Q15`），目标 Ui 有效值约 100mV
- **自动模式**：PID 闭环调节 DAC 幅度，目标 Uo 有效值 1.5V ±1%

## 跳线 S2/S3/S4

软件不读 GPIO，通过 FFT 实测增益 `Av` 变化自动识别电路配置改变，OLED 显示 `CFG:CHANGED`。

## 关键宏参数（BoardConfig.h）

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `SYSCLK_HZ` | 168000000 | 系统主频 |
| `TIM6_CLK_HZ` / `TIM2_CLK_HZ` | 84000000 | DAC/ADC 定时器计数时钟 |
| `WAVE_TABLE_MIN_POINTS` | 128 | 每档波形表点数下限（压谐波） |
| `WAVE_TABLE_MAX_POINTS` | 256 | DMA 缓冲区上限 |
| `PID_KP/KI/KD` | 0.8/0.15/0.02 | PID 系数 |
| `PID_FRAME_DIV` | 5 | DMA ~20ms/帧，PID 每 5 帧 = 100ms |
| `ADC_SAMPLE_ARR` | 1638 | 采样率约 51.2kHz |
| `FFT_SIZE` | 1024 | FFT 点数 |
| `FFT_AUTO_GAIN_PEAK` | 28000.0f | f32 FFT 前自动增益峰值 |

## 中断分工（阶段 1～4 现行）

| 中断源 | 周期 | 职责 |
|--------|------|------|
| DMA2_Stream0 | ~20ms | 拆包 Ui/Uo → 置 `g_ADC_FrameReady`（**不跑 FFT**） |
| TIM7 | 20ms | 帧就绪时 `App_BackgroundProcess`（含 FFT）；`Key_Tick20ms` / `App_CheckFault` 占位 |
| TIM4 | 200ms | OLED 刷新（阶段 6 全参显示） |

## 阶段 1～4 验收对照

| 阶段 | 验收项 | 代码模块 |
|------|--------|----------|
| 1 | 编译/启动/空主循环 | `PeriphInit.c`、`lit.c`、`main.c` |
| 2 | TIM+DMA+DAC 波形 | `DAC_Wave.c`、`DAC_SinTables.inc`、`DAC_TriTables.inc` |
| 3 | 双路 ADC 帧采集 | `ADC_Dual.c` |
| 4 | FFT 频域解算 | `FFT_Analyze.c`、`App_Main.c`、`Key.c`（TIM7 调度） | **Keil Watch（D.5）** |

### 阶段 4 Keil Watch 验收（D.5）

上电 Run ≈1 s → Halt，在 Watch 中查看 `g_MeasureResult.*` 与 `g_FFT_Dbg*`。  
**不要求** OLED 显示 Ui/Uo/Av（阶段 6 再验 L3～L7）。核心通过条件：

- `fft_fail_code == 0`
- `ui_rms_v` ∈ **0.08～0.12 V**
- `meas_freq_hz` ≈ **1000**，`freq_ok == 1`
- `g_FFT_DbgPeakBin` ∈ **18～22**

完整 Watch 列表见 [REBUILD_APPENDIX_D.md](REBUILD_APPENDIX_D.md) **D.5** 节。

## 验收步骤对照（阶段 5 起）

| 验收项 | 代码模块 |
|--------|----------|
| PID 自动稳幅 | `PID_Ctrl.c` + `App_Main.c`（DMA 触发，100ms 节流） |
| 三键人机交互 | `Key.c`（TIM7 @ 20ms，阶段 7 扩展） |
| OLED 全参数显示 | `Display.c`（TIM4 @ 200ms） |
| 主循环架构 | `User/main.c`（**空转**，业务在中断） |

## 编译

使用 Keil µVision 打开 `Project.uvprojx`，目标芯片 **STM32F407ZGT6**（工程内选 `STM32F407ZG`），启动文件 `startup_stm32f40_41xxx.s`，Flash 算法 **STM32F4xx 1 MB**，编译选项开启 **FPU FPv4-SP-D16**，链接 **CMSIS-DSP**（`arm_cfft_f32`）。
