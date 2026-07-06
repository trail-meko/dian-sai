# 附录 B：模块划分、API 与数据流

## B.1 模块总览

| 模块 | 建议文件 | 职责 | 验收编号 |
|------|----------|------|----------|
| 板级配置 | `app_config.h` | 引脚宏、FFT/ADC/PI 参数、调试开关 | — |
| 全局状态 | `app_state.c/h` | `g_app` 运行参数与测量结果 | — |
| Flash 校准 | `app_calib.c/h` | Sector 11 加载/保存 `g_calib` | D.12 |
| 协作调度 | `app_scheduler.c/h` | SysTick 1ms + PendSV 任务队列 | D.10 |
| 波形输出 | `wave_gen.c/h` | ROM 正弦表 + TIM2/DMA1/DAC | D.2, D.3 |
| 信号采集 | `adc_dma.c/h` | ADC1 扫描 + TIM3 + DMA2_Stream0 | D.4 |
| 频域解算 | `app_measure.c/h` | Hann 窗 FFT、RMS/Av/相位/频率 | D.5, D.6 |
| 幅值控制 | `app_control.c/h` | 手动固定 Vpp / 自动 PI | D.7 |
| CFG 检测 | `app_cfg.c/h` 或并入 `app_control.c` | **重建须新增** | D.8 |
| 故障指示 | `app_fault.c/h` 或并入 `app_scheduler.c` | **重建须新增** PF9 闪烁 | D.11 |
| 显示 | `app_display.c/h` + `oled_i2c.c/h` | 5 行 OLED 刷新 | D.9 |
| 按键 | `app_keys.c/h` + `key.c` | SysTick 去抖 + 参数切换 | D.3, D.7 |
| DWT 延时 | `dwt_delay.c/h` | 软件 I2C 微秒延时 | — |
| LED 驱动 | `led.c/h` | PF9/PF10 GPIO 初始化 | D.11 |
| 主程序 | `main.c` | 初始化链 + `__WFI()` | D.1 |

**不要实现/编入**：`adc.c`、`dac.c`、`timer.c`（遗留）、`lcd.c`、`oled.c`（旧驱动）、`DISP.c`。

## B.2 信号流

```
wave_gen ──PA4──→ [运放] ──PA5/PA6──→ adc_dma
    ↑ dac_vpp                              │ adc_dma_buf[4096]
    │                                      ▼
    └──────── app_control ←── PendSV ←── app_measure
                              │
                              ├── app_display → oled_i2c
                              └── app_cfg（增强）
```

**帧处理路径**：

```
DMA2_Stream0 TC ISR
  → AdcDma_StopCapture()
  → AppScheduler_SampleReady()   // 置 s_sample_pending
  → PendSV_Handler
       → Measure_Process()
       → Control_Update()
       → App_CfgUpdate()         // 增强，重建须新增
       → Display_Invalidate()
       → 启动 500ms 后 AdcDma_ResumeSampling()
```

## B.3 核心全局状态

### `AppState_t`（`g_app`）

```c
typedef enum { WAVE_SINE = 0, WAVE_TRIANGLE } WaveType_t;
typedef enum { MODE_MANUAL = 0, MODE_AUTO } AmpMode_t;

typedef struct {
    WaveType_t wave;
    AmpMode_t  amp_mode;
    uint16_t   freq_hz;          /* 1000~2000，步进 100 */
    float      dac_vpp;          /* 当前 DAC 峰峰值 (V) */
    float      ui_rms_mv;        /* 输入 RMS (mV) */
    float      uo_rms_v;         /* 输出 RMS (V) */
    float      freq_meas_hz;     /* FFT 实测频率 */
    float      gain;             /* Av，增强：|Ph|>90° 时为负 */
    float      phase_deg;        /* 相对相位 (deg) */
    uint8_t    display_dirty;
    /* --- 重建须新增 --- */
    uint8_t    cfg_state;        /* CFG_STABLE / CFG_CHANGED */
    uint8_t    fault_flags;      /* 见 C.6 */
    uint8_t    measure_ok;       /* 本帧 FFT 是否有效 */
} AppState_t;
```

### `CalibData_t`（`g_calib`，Flash Sector 11）

```c
typedef struct {
    uint32_t magic;              /* CALIB_MAGIC 0xCA1B1234 */
    float    k_ui;               /* Ui ADC→物理量比例，默认 1.0 */
    float    k_uo;               /* Uo ADC→物理量比例，默认 1.0 */
    float    phase_cal_deg;      /* 相位零点校准，默认 0 */
    float    dac_vpp_100mV;      /* 手动模式目标 Ui≈100mV 的 DAC Vpp，默认 0.5 */
} CalibData_t;
```

### 故障标志位（重建须新增）

```c
#define FAULT_ADC         (1u << 0)   /* ADC 缓冲异常 */
#define FAULT_FFT         (1u << 1)   /* FFT 无有效峰 */
#define FAULT_SIGNAL_LOSS (1u << 2)   /* Ui RMS 过低 */
#define FAULT_PI_SAT      (1u << 3)   /* PI 输出触及 DAC 限幅 */
```

### CFG 状态（重建须新增）

```c
#define CFG_STABLE   0
#define CFG_CHANGED  1
```

## B.4 模块对外 API（最小集合）

### wave_gen

```c
void WaveGen_Init(uint16_t freq_hz, WaveType_t type, float dac_vpp);
void WaveGen_SetFreq(uint16_t freq_hz);
void WaveGen_SetWave(WaveType_t type);
void WaveGen_SetAmplitude(float dac_vpp);
float WaveGen_GetAmplitude(void);
uint16_t WaveGen_GetFreq(void);
void WaveGen_Start(void);
void WaveGen_Stop(void);
void WaveGen_Pause(void);
void WaveGen_Resume(void);
```

双缓冲 `wave_table[2][256]`；改幅值/波形时填充非活动缓冲后切换 DMA M0AR。

### adc_dma

```c
extern volatile uint16_t adc_dma_buf[ADC_BUF_LEN];  /* 4096 半字 */

void AdcDma_Init(void);
const volatile uint16_t *AdcDma_GetReadBuf(void);
void AdcDma_PauseSampling(void);
void AdcDma_ResumeSampling(void);
/* DMA2_Stream0_IRQHandler: 停 TIM3/ADC → AppScheduler_SampleReady()（不跑 FFT） */
```

解交织：`Ui[i] = adc_dma_buf[2*i]`，`Uo[i] = adc_dma_buf[2*i+1]`。

### app_measure

```c
void Measure_Init(void);   /* 构建 Hann 窗表 */
void Measure_Process(void); /* PendSV 中调用；更新 g_app 测量字段 */
```

### app_control

```c
void Control_Init(void);
void Control_Update(void);   /* PendSV 中，Measure_Process 之后 */
void Control_ResetPi(void);  /* 切自动模式时清零积分 */
```

### app_calib

```c
void Calib_Load(void);
void Calib_Save(void);
void Calib_SetDefaults(void);
```

### app_scheduler

```c
#define SCHED_F_SAMPLE    (1u << 0)
#define SCHED_F_KEY       (1u << 1)
#define SCHED_F_DISP      (1u << 2)
#define SCHED_F_OLED_INIT (1u << 3)

void AppScheduler_Init(void);
void AppScheduler_SetPending(u32 flags);
void AppScheduler_SampleReady(void);
/* SysTick_Handler / PendSV_Handler 在本文件实现 */
```

### app_display

```c
void Display_Invalidate(void);  /* 置 dirty + 挂 SCHED_F_DISP */
void Display_Update(void);      /* PendSV 中刷新 OLED */
```

### app_keys

```c
void AppKeys_Init(void);
void AppKeys_Tick1ms(void);   /* SysTick 中调用 */
void AppKeys_Process(void);   /* PendSV 中调用 */
```

### 增强 API（重建须新增）

```c
void App_CfgInit(void);
void App_CfgUpdate(float av_gain);     /* 每帧测量后调用 */
void App_CheckFault(void);             /* SysTick 或 PendSV；控 PF9 闪烁 */
void Fault_Set(uint8_t flags);
void Fault_Clear(uint8_t flags);
```

## B.5 中断分工

| 中断源 | 周期/触发 | 优先级建议 | 职责 |
|--------|-----------|------------|------|
| DMA2_Stream0 TC | ~20 ms/帧 | 2/0 | 停采样 → `AppScheduler_SampleReady()`（**禁止 FFT**） |
| SysTick | 1 ms | 2/0 | OLED 上电等待；按键 tick；ADC 启动延时；采集间隔；`App_CheckFault()` |
| PendSV | 软件触发 | 最低 | OLED 初始化 / 采样处理 / 按键 / 显示 |

**无 TIM2/TIM3/DMA1_Stream5 中断**：DAC 与 ADC 触发均由硬件完成。

**主循环**：

```c
while (1) {
    __WFI();
}
```

**NVIC**：`NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2)`。

## B.6 PendSV_Handler 逻辑链

1. **OLED 延迟初始化**（上电 100 ms 后，`SCHED_F_OLED_INIT`）
   - `OLED_I2C_Init()` → `Display_Invalidate()`
2. **采样处理**（`s_sample_pending`）
   - `Measure_Process()` → 更新 `g_app`；设置 `fault_flags` / `measure_ok`
   - **增益极性（增强）**：`if (fabsf(g_app.phase_deg) > 90.0f) g_app.gain = -fabsf(g_app.gain);`
   - `Control_Update()` → 手动/自动 PI 调 `dac_vpp`
   - `App_CfgUpdate(g_app.gain)` → CFG 状态
   - `Display_Invalidate()`
   - 置 500 ms 采集间隔 → `AdcDma_ResumeSampling()`
3. **按键**（`SCHED_F_KEY`）→ `AppKeys_Process()`
4. **显示**（`SCHED_F_DISP` + `display_dirty`）→ `Display_Update()`

## B.7 按键行为

| 按键 | 引脚 | 动作 |
|------|------|------|
| KEY 频率 | PE3 | `freq_hz += 100`；>2000 回绕 1000；`WaveGen_SetFreq()` |
| KEY 模式 | PE5 | 手动↔自动；进自动时 `Control_ResetPi()` |
| KEY 波形 | PE2 | 正弦↔三角；`WaveGen_SetWave()` |

防抖：SysTick 1 ms 计数，连续 **10 ms** 低电平有效；释放前不重复触发（latched）。

## B.8 OLED 5 行显示规格

| 行 | Y 坐标 | 内容示例 |
|----|--------|----------|
| L1 | 0 | `SIN 1000Hz MAN` 或 `TRI 1500Hz AUT` |
| L2 | 14 | `Ui:100mV Uo:1.50V` |
| L3 | 28 | `Gn:-10.5 Ph:-180`（反相时增益为负） |
| L4 | 42 | `Fm:1000Hz` |
| L5 | 54 | 见下表 |

**L5 显示优先级**（重建须实现）：

| 条件 | L5 内容 |
|------|---------|
| `fault_flags != 0` | `FLT:0xNN`（十六进制故障掩码） |
| PI 饱和（`FAULT_PI_SAT`） | 可简写 `SAT!` |
| 手动模式且无故障 | `CFG:STABLE` 或 `CFG:CHANGED` |
| 自动模式且无故障 | `Auto->1.5V` |

字体：12 点阵；刷新由 `display_dirty` 事件驱动（非固定周期轮询）。

## B.9 main 初始化链

```c
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
Dwt_Init();
LED_Init();
AppState_Init();
Calib_Load();
Control_Init();
App_CfgInit();                    /* 增强 */
WaveGen_Init(g_app.freq_hz, g_app.wave, g_calib.dac_vpp_100mV);
g_app.dac_vpp = g_calib.dac_vpp_100mV;
Measure_Init();
AppKeys_Init();
AppScheduler_Init();              /* 启动 SysTick */
while (1) { __WFI(); }
```

**注意**：OLED 不在 `main` 中初始化，由调度器上电 100 ms 后延迟初始化。
