# 附录 C：算法、宏参数与频率档位

## C.1 系统时钟

| 宏 | 值 |
|----|-----|
| MCU | STM32F407ZGT6，1024 KB Flash，192 KB RAM |
| HSE | 25 MHz（`system_stm32f4xx.c` 默认） |
| SYSCLK | 168 MHz |
| APB1 | 42 MHz（TIM2/TIM3 时钟 ×2 = **84 MHz**） |
| DAC_TIM_CLK_HZ | 84000000（`wave_gen.c` 硬编码） |

## C.2 调试开关（完成态）

| 宏 | 完成态值 | 说明 |
|----|----------|------|
| `APP_MEASURE_ENABLE` | **1** | 1=启用 ADC/FFT；0=仅 DAC+OLED 调试 |
| `APP_OLED_ENABLE` | **1** | 1=启用 OLED；0=跳过显示 |

## C.3 波形 / DAC

| 宏 | 值 | 说明 |
|----|-----|------|
| `FREQ_MIN_HZ` | 1000 | 最低档 |
| `FREQ_MAX_HZ` | 2000 | 最高档 |
| `FREQ_STEP_HZ` | 100 | 步进 |
| `FREQ_DEFAULT_HZ` | 1000 | 上电默认 |
| `WAVE_TABLE_SIZE` | 256 | 每周期固定点数 |
| `DAC_MID_CODE` | 2048 | 12-bit 中点 |
| `DAC_VREF` | 3.3 | V |
| `DAC_VPP_MIN` | 0.01 | V |
| `DAC_VPP_MAX` | 3.0 | V |
| `DAC_VPP_DEFAULT` | 0.5 | V（未校准时手动模式默认） |
| `TARGET_US_RMS_MV` | 100.0 | 手动模式 Ui 目标 (mV) |
| `TARGET_UO_RMS_V` | 1.5 | 自动模式 Uo 目标 (V) |

### 频率公式

```
f = TIM_CLK / [(ARR+1) × N]
  = 84 MHz / [(ARR+1) × 256]
```

实现宏（`wave_gen.c`）：

```c
#define RELOAD_VAL(hz)  ((uint16_t)(84000000UL / 256UL / (hz)))
```

改频率时只更新 `TIM2->ARR = RELOAD_VAL(freq_hz)`，**不改**波形点数。

### 11 档 TIM2 ARR 预计算

| target_hz | ARR (`RELOAD_VAL`) | 实测频率误差 |
|-----------|-------------------|--------------|
| 1000 | 328 | <0.04% |
| 1100 | 298 | <0.04% |
| 1200 | 273 | <0.04% |
| 1300 | 252 | <0.04% |
| 1400 | 234 | <0.04% |
| 1500 | 218 | <0.04% |
| 1600 | 205 | <0.04% |
| 1700 | 193 | <0.04% |
| 1800 | 182 | <0.04% |
| 1900 | 172 | <0.04% |
| 2000 | 164 | <0.04% |

### 幅值缩放

正弦：以 ROM 表 `sine_ref_256[i]`（峰值 ±2047，中点 2048）为基准：

```
DAC_code[i] = 2048 + (sine_ref_256[i] - 2048) × (dac_vpp / DAC_VREF)
```

三角：运行时生成，`WaveGen_Triangle(i)` 峰峰值 ±1，映射到 `dac_vpp`。

### 正弦 ROM 表

完整 256 点数组定义在 `wave_gen.c` 的 `sine_ref_256[]`（正点原子验证过的 3.3V 满幅表）。前 10 点：

```
2048, 2098, 2148, 2198, 2248, 2298, 2348, 2398, 2447, 2496, ...
```

**运行时不调用 `sinf()`** 生成正弦；三角波在 `WaveGen_FillTable()` 中按公式生成。

### DAC 外设绑定（F407）

| 项目 | 配置 |
|------|------|
| 定时器 | TIM2，TRGO = Update |
| DMA | DMA1_Stream5 Ch7，Mem→`DAC->DHR12R1`，循环模式，优先级 **VeryHigh** |
| 触发 | `DAC_Trigger_T2_TRGO` |
| 输出缓冲 | Disable（降低失真） |
| 双缓冲切换 | 改幅值/波形时填充 `wave_table[1-active]`，停 TIM2/DMA，改 M0AR，重启 |

## C.4 ADC / 采样

| 宏 | 值 | 说明 |
|----|-----|------|
| `FFT_LENGTH` | 2048 | 每通道样本数 |
| `ADC_BUF_LEN` | 4096 | 2048×2 通道交织 |
| `ADC_SAMPLE_RATE` | 100000 | Hz |
| `ADC_CH_UI` | ADC_Channel_5 | PA5 |
| `ADC_CH_UO` | ADC_Channel_6 | PA6 |

### TIM3 配置

```
TIM3_ARR = (84000000 / ADC_SAMPLE_RATE) - 1 = 839
```

### ADC 配置要点

- **ADC1 独立模式 + 扫描**：Rank1=CH5(Ui)，Rank2=CH6(Uo)
- 分辨率 12-bit；采样时间 15 cycles；ADC 时钟 = PCLK2/4
- 外部触发：TIM3 TRGO；每次触发转换 **2 通道**
- DMA2_Stream0 Ch0：外设 `ADC1->DR`，内存 `adc_dma_buf[4096]`，**Normal** 模式（非循环）
- 单帧采集时间 ≈ 2048 / 100 kHz ≈ **20.5 ms**
- **帧间隔**：`MEASURE_CAPTURE_INTERVAL_MS = 500`（两次采集间隔）
- **启动延迟**：`MEASURE_START_DELAY_MS = 500`（上电后首次启动 ADC）

### ADC 异常判定（增强，重建须实现）

- 缓冲全近 0 或全近 4095 → `FAULT_ADC`，`measure_ok = 0`

## C.5 FFT 解算

| 宏 | 值 |
|----|-----|
| `ADC_VREF` | 3.3（复用 DAC_VREF） |
| 频率分辨率 | `ADC_SAMPLE_RATE / FFT_LENGTH` ≈ **48.8 Hz/bin** |
| 1 kHz 目标 bin | `1000 × 2048 / 100000` ≈ **20.48**（峰值在 bin 20 附近） |

### 处理流程

1. **解交织**：`ui[i]=buf[2*i]`，`uo[i]=buf[2*i+1]`，换算为电压 `× 3.3/4095`
2. **去直流**：减去时域均值
3. **Hann 窗**：`w[i] = 0.5 × (1 - cos(2π×i/(N-1)))`
4. **FFT**：`arm_cfft_f32(&arm_cfft_sR_f32_len2048, fft_buf, 0, 1)`（Ui、Uo 各一次）
5. **搜峰**：在 bin 1～N/2-1 找最大幅值 `peak`
6. **抛物线插值**：三点抛物线求 `delta`，`bin_f = peak + delta`
7. **频率**：`freq = bin_f × ADC_SAMPLE_RATE / FFT_LENGTH`
8. **RMS**：`vpeak = 2 × peak_mag / N × calib_k`；`vrms = vpeak / shape_factor`
   - 正弦 shape_factor = **1.4142136**
   - 三角 shape_factor = **1.7320508**
9. **相位**：`atan2(im, re)` 在 peak bin；输出相位 = `phase_uo - phase_ui - phase_cal_deg`，归一化到 ±180°
10. **增益**：`gain = uo_rms / ui_rms`（ui_rms > 0 时）
11. **增益极性（增强）**：`if (fabsf(phase_deg) > 90.0f) gain = -fabsf(gain);`

### 信号丢失判定（增强）

- `ui_rms_mv < 10`（10 mV）→ `FAULT_SIGNAL_LOSS`
- FFT 搜峰失败（peak 在边界）→ `FAULT_FFT`

## C.6 PI 控制器（位置式，每帧执行）

| 宏 | 值 |
|----|-----|
| `PI_KP` | 1.0 |
| `PI_KI` | 0.1 |
| `PI_INTEGRAL_MAX` | 2.0 |

- **手动模式**：`dac_vpp = g_calib.dac_vpp_100mV`（固定，靠 Flash 校准保证 Ui≈100 mV）
- **自动模式**：
  ```
  error = TARGET_UO_RMS_V - uo_rms_v
  integral += error（限幅 ±PI_INTEGRAL_MAX）
  dac_vpp = current_vpp + KP×error + KI×integral
  dac_vpp 限幅 [DAC_VPP_MIN, DAC_VPP_MAX]
  ```
- **更新门限**（减少抖动）：仅当 `|Δdac_vpp|/last > 1%` 或 `|error| > 0.05 V` 时才调用 `WaveGen_SetAmplitude()`
- 触及 `DAC_VPP_MIN` 或 `DAC_VPP_MAX` → `FAULT_PI_SAT`

## C.7 CFG 跳线检测（重建须新增）

| 宏 | 值 |
|----|-----|
| `CFG_CHANGE_THRESH` | 0.05（5%） |
| `CFG_CHANGE_COUNT` | 3 |

算法（与 F103 第3版相同）：

```
若 |Av - Av_stable| > |Av_stable| × 5% + 0.01：
    change_count++
    若 change_count >= 3：
        cfg_state = CFG_CHANGED
        Av_stable = Av（更新基准）
        change_count = 0
        Control_ResetPi()
否则：
    change_count = 0
    cfg_state = CFG_STABLE
    Av_stable = Av_stable × 0.9 + Av × 0.1（指数平滑）
```

## C.8 故障 LED（重建须新增）

| 项目 | 说明 |
|------|------|
| 引脚 | PF9，低电平点亮 |
| 正常 | `GPIO_SetBits(PF9)` 常灭 |
| 故障 | `fault_flags != 0` 时 500 ms 周期翻转（SysTick 计数） |
| 调用 | `App_CheckFault()` 在 SysTick 1 ms 中执行 |

## C.9 时序参数

| 宏 | 值 | 说明 |
|----|-----|------|
| `KEY_SCAN_MS` | 10 | 按键去抖周期 |
| `DISPLAY_UPDATE_MS` | 100 | 已定义，显示实际由 dirty 事件驱动 |
| `OLED_POWER_ON_MS` | 100 | 上电后延迟初始化 OLED |
| `MEASURE_START_DELAY_MS` | 500 | 上电后延迟启动 ADC |
| `MEASURE_CAPTURE_INTERVAL_MS` | 500 | 两次 DMA 采集间隔 |

## C.10 Flash 校准

| 宏 | 值 |
|----|-----|
| `CALIB_FLASH_ADDR` | 0x080E0000（Sector 11） |
| `CALIB_MAGIC` | 0xCA1B1234 |

**校准流程**（人工，无 UI 按钮）：

1. 上电默认 `dac_vpp_100mV = 0.5 V`
2. 手动模式，示波器测 Ui RMS
3. 调整 `g_calib.dac_vpp_100mV` 使 Ui ≈ 100 mV
4. 调试器调用 `Calib_Save()` 写入 Flash
5. 复位后 `Calib_Load()` 自动加载

可选：用 `k_ui`/`k_uo` 修正 ADC 幅值比例；`phase_cal_deg` 修正相位零点。

## C.11 设计指标（验收参考）

| 指标 | 目标 |
|------|------|
| 频率测量误差 | ≤ 1%（2048 点分辨率限制，1 kHz 附近约 ±0.5%） |
| RMS 误差 | ≤ 5%（需外参校准） |
| 相位误差 | ≤ 5°（需外参校准） |
| 自动模式 Uo | 1.5 V ±10% |

## C.12 第三方库

- **SPL**：`stm32f4xx_gpio/rcc/adc/dac/dma/tim/flash/misc` 等
- **CMSIS-DSP**：`DSP_LIB/arm_cortexM4lf_math.lib`（链接浮点库）
  - 头文件：`arm_math.h`、`arm_const_structs.h`
  - FFT：`arm_cfft_f32` + `arm_cfft_sR_f32_len2048`
- 启动文件：`CORE/startup_stm32f40_41xxx.s`
- 延时：`dwt_delay.c` 供 OLED 软件 I2C 使用
