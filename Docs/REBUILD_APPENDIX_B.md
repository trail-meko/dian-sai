# 附录 B：模块划分、API 与数据流

## B.1 模块总览

| 模块 | 建议文件 | 职责 | 验收编号 |
|------|----------|------|----------|
| 板级配置 | `BoardConfig.h` | 引脚、采样率、FFT/PID 宏 | — |
| 系统初始化 | `PeriphInit.c` | NVIC 优先级分组与 IRQ 优先级 | D.1 |
| LED | `System/lit.c` | PA8 故障指示 GPIO 初始化 | D.13 |
| 延时 | `System/Delay.c` | ms 延时 | — |
| 波形输出 | `DAC_Wave.c` + `DAC_SinTables.inc` + `DAC_TriTables.inc` | Flash 查表 + TIM6/DMA1_Stream5/DAC | D.2, D.3 |
| 信号采集 | `ADC_Dual.c` | ADC1 扫描 + TIM2 + DMA2_Stream0 | D.4 |
| 频域解算 | `FFT_Analyze.c` | RMS/Av/相位/频率 | D.5, D.6 |
| PID | `PID_Ctrl.c` | 自动模式稳幅（阶段 8） | D.8 |
| 业务调度 | `App_Main.c` | 帧处理（由 TIM7 调用） | D.5（阶段 4 起） |
| 按键/调度 | `Key.c` | TIM7 20ms：帧处理入口 + 按键防抖（阶段 7 扩展） | D.4, D.5 |
| 显示 | `Display.c` + `OLED.c` | TIM4 200ms 刷新 8 行 | D.11 |
| 主程序 | `User/main.c` | 初始化 + 空循环 | D.12 |

**不要实现**：`FFTDMAAD.c`、`PWM.c`（遗留单 ADC / PWM 激励方案）。

## B.2 信号流

```
DAC_Wave ──PA4──→ [运放] ──PA2/PA3──→ ADC_Dual
    ↑ gain_q15                              │ g_AD_Ui/Uo[1024]
    │                                       ▼
    └──────── PID_Ctrl ←── App_Main ←── FFT_Analyze
                              │
                              ├── Display → OLED
                              └── Key (TIM7) → App_BackgroundProcess → FFT_Analyze
```

阶段 4 数据路径：`DMA2 Stream0 TC` 置位 `g_ADC_FrameReady` → `TIM7` 调用 `App_BackgroundProcess()` → `FFT_Analyze_Process`。

## B.3 核心全局状态

### `AppParams_t`（应用参数）

```c
typedef enum { WORK_MODE_MANUAL = 0, WORK_MODE_AUTO } WorkMode_t;
typedef enum { WAVE_SINE = 0, WAVE_TRIANGLE } WaveType_t;

typedef struct {
    WorkMode_t  mode;
    WaveType_t  wave;
    uint8_t     freq_index;      /* 0~10 → 1.0~2.0 kHz */
    uint16_t    target_freq_hz;
} AppParams_t;
```

### `MeasureResult_t`（测量结果，供 OLED）

```c
typedef struct {
    float ui_rms_v, uo_rms_v, av_gain, phase_deg, meas_freq_hz;
    float target_out_v;          /* 显示：手动 0.1V / 自动 1.5V */
    uint8_t freq_ok, fault_flags, fft_fail_code;
    uint16_t adc_ui_min, adc_ui_max, adc_uo_min, adc_uo_max;
    uint8_t cfg_state;           /* CFG_STABLE / CFG_CHANGED */
    uint8_t pid_saturated;
} MeasureResult_t;
```

### 故障标志位

```c
FAULT_ADC         = 1<<0   /* ADC 异常帧 */
FAULT_FFT         = 1<<1   /* FFT 无有效峰 */
FAULT_SIGNAL_LOSS = 1<<2   /* Ui RMS 过低 */
FAULT_PID_SAT     = 1<<3   /* PID 输出饱和 */
```

## B.4 模块对外 API（最小集合）

### DAC_Wave

```c
void DAC_Wave_Init(void);
void DAC_Wave_SetFrequencyIndex(uint8_t index);  /* 0~10 */
void DAC_Wave_SetWaveType(uint8_t wave);         /* 0=正弦 1=三角 */
void DAC_Wave_SetGainScaleQ15(uint16_t scale);
void DAC_Wave_ApplyManualGain(void);             /* 正弦/三角用不同 Q15 */
uint16_t DAC_Wave_GetGainScaleQ15(void);
```

### ADC_Dual

```c
extern uint16_t g_AD_Ui[1024], g_AD_Uo[1024];
extern volatile uint8_t g_ADC_FrameReady;

void ADC_Dual_Init(void);
void ADC_Dual_BeginProcess(uint16_t **snap_ui, uint16_t **snap_uo);
void ADC_Dual_EndProcess(void);
/* DMA2_Stream0_IRQHandler: 停 TIM2 → 拆包 scan_raw → g_AD_Ui/Uo → 置 g_ADC_FrameReady（不跑 FFT） */
```

**帧处理注意**：DMA ISR 只做轻量拆包；`App_BackgroundProcess` 在 **TIM7** 中调用。`BeginProcess` 期间暂停 ADC 触发/DMA 中断，快照后处理；`EndProcess` 恢复 TIM2。

### FFT_Analyze

```c
uint8_t FFT_Analyze_Process(uint16_t *raw_ui, uint16_t *raw_uo,
                            uint16_t target_freq_hz, FFTMeasure_t *out,
                            uint8_t *fail_code);
/* fail_code: 1=Ui异常 2=Uo异常 3=Ui无峰 4=Uo无峰 */
void FFT_Analyze_ResetAvg(void);   /* 切频/切波形/切模式时调用 */
```

### PID_Ctrl

```c
void PID_Ctrl_Reset(void);
uint8_t PID_Ctrl_Update(float uo_rms_v, uint16_t *gain_scale_q15);
/* 返回 1 表示增益触及上下限饱和 */
```

### App_Main

```c
void App_Init(void);
void App_BackgroundProcess(void);  /* TIM7 中 g_ADC_FrameReady 时调用 */
void App_OnKeyEvent(uint8_t key_id); /* 1=频率 2=模式 3=波形（阶段 7） */
void App_CheckFault(void);         /* TIM7 中调用，控 LED（阶段 8） */
```

### Key（阶段 4 起）

```c
void Key_Init(void);      /* 启动 TIM7 @ KEY_SCAN_MS */
void Key_Tick20ms(void);  /* 阶段 7 扩展按键扫描 */
/* TIM7_IRQHandler: Key_Tick20ms + App_CheckFault + App_BackgroundProcess（帧就绪时） */
```

## B.5 中断分工（阶段 1～4 现行实现）

| 中断源 | 周期 | 优先级建议 | 职责 |
|--------|------|------------|------|
| DMA2_Stream0 | ~20 ms/帧 | 0（最高） | 停 TIM2 → 拆包 Ui/Uo → 置 `g_ADC_FrameReady`（**禁止**在此跑 FFT） |
| TIM7 | 20 ms | 2 | `Key_Tick20ms()` + `App_CheckFault()`；若 `g_ADC_FrameReady` 则 `App_BackgroundProcess()` |
| TIM4 | 200 ms | 2 | `Display_Refresh()`（阶段 6） |

**主循环**（阶段 1～4）：

```c
while (1) {
    /* 空循环 — 禁止塞 FFT / PID / 显示等业务 */
}
```

**DAC 注意**：DMA1_Stream5 优先级 **VeryHigh**；`StartOutput` 须 `TIM_SetCounter(TIM6,0)` 并预载 `s_DacBuffer[0]`，避免 PA4 双影/相位跳动。

## B.6 App_BackgroundProcess 逻辑链

> **阶段 4 已落地**：下列步骤 2～5（含 Av 极性）及 `ADC_Dual_EndProcess`。  
> **阶段 5+ 扩展**：Display 暂停刷新、坏帧丢弃、CFG 检测、PID。

1. `Display_SetRefreshEnable(0)` — 防止 OLED 读到半更新数据（阶段 6）
2. `ADC_Dual_BeginProcess` → 获取快照指针
3. `FFT_Analyze_Process` → 失败则置 fault，跳过后续
4. **坏帧丢弃**：Ui/Uo RMS 突降 >70% 时保留上次示数
5. 更新 `g_MeasureResult`；\|Δφ\|>90° 时 Av 取负
6. `App_UpdateCfgDetect` — Av 变化检测跳线
7. 自动模式：每 `PID_FRAME_DIV` 帧调用 `PID_Ctrl_Update`
8. `ADC_Dual_EndProcess` + `Display_SetRefreshEnable(1)`

## B.7 按键行为

| 按键 | ID | 动作 |
|------|-----|------|
| KEY1 | 1 | `freq_index` 循环 0→10→0；更新 `target_freq_hz`；`DAC_Wave_SetFrequencyIndex`；`FFT_Analyze_ResetAvg` |
| KEY2 | 2 | 手动↔自动；进自动时 `PID_Ctrl_Reset`；`FFT_Analyze_ResetAvg` |
| KEY3 | 3 | 正弦↔三角；手动模式下 `DAC_Wave_ApplyManualGain`；`FFT_Analyze_ResetAvg` |

防抖：20 ms 周期，低电平有效，检测 **下降沿**（释放→按下）。

## B.8 OLED 8 行显示规格

| 行 | 内容示例 |
|----|----------|
| L1 | `M:MAN  SIN 1.0kHz` 或 `A:AUTO TRI 1.5kHz` |
| L2 | `Tar:100mV`（手动）或 `Tar:1.50V`（自动） |
| L3 | `Ui:100.0mV` 或 `Ui:x.xxxV` |
| L4 | `Uo:x.xxxV` |
| L5 | `Av:-10.00`（反相时为负） |
| L6 | `Ph:-180.0deg` |
| L7 | `f:1000Hz OK` 或 `f:xxxHz ERR` |
| L8 | 无故障：`CFG:STABLE` / `CFG:CHANGED`；PID 饱和：`SAT!`；有故障：`FLT:0xNN` |
