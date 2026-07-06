# 附录 C：算法、宏参数与频率档位

## C.1 系统时钟

| 宏 | 值 |
|----|-----|
| MCU | STM32F407ZGT6，1 MB Flash，192 KB RAM，Cortex-M4F |
| `SYSCLK_HZ` | 168000000（HSE + PLL，标准树） |
| `TIM6_CLK_HZ` | 84000000（APB1 定时器时钟，PCLK1=42 MHz ×2） |
| `TIM2_CLK_HZ` | 84000000（同 TIM6，用于 ADC 触发） |
| `ADC_CLK_HZ` | 21000000（PCLK2/4，ADC 最高 36 MHz 内） |

> **注意**：DAC 与 ADC 触发定时器挂在 APB1，计数频率为 **84 MHz**，不是 SYSCLK 168 MHz。

## C.2 波形 / DAC

| 宏 | 值 | 说明 |
|----|-----|------|
| `FREQ_MIN_HZ` | 1000 | 最低档 |
| `FREQ_MAX_HZ` | 2000 | 最高档 |
| `FREQ_STEP_HZ` | 100 | 步进 |
| `FREQ_GEAR_COUNT` | 11 | 档位数 |
| `WAVE_TABLE_MIN_POINTS` | 128 | 每档波形点数下限 |
| `WAVE_TABLE_MAX_POINTS` | 256 | DMA 缓冲上限 |
| `MANUAL_GAIN_SCALE_Q15` | 2785 | 正弦手动模式，目标 Ui≈100 mV |
| `MANUAL_GAIN_SCALE_TRIANGLE_Q15` | 3524 | 三角手动模式，目标 Ui≈100 mV |
| `TARGET_UO_RMS_V` | 1.50 | 自动模式 PID 目标 |
| `GAIN_SCALE_MIN_Q15` | 328 | ≈0.01 |
| `GAIN_SCALE_MAX_Q15` | 31129 | ≈0.95 |

### 频率公式

```
f = TIM6_CLK_HZ / [(PSC+1) × (ARR+1) × N]
  = 84 MHz / [(PSC+1) × (ARR+1) × N]
```

`DAC_code[i] = 2048 + (WaveTable[i] × GainScaleQ15 >> 15)`，WaveTable 峰值 ±2047。

### 波形查表（Flash 预存，运行时不算三角函数）

| 文件 | 内容 |
|------|------|
| `DAC_SinTables.inc` | N=242/249/250/251/252/254/256 正弦表，`sin(2π·i/N)×2047` 离线生成 |
| `DAC_TriTables.inc` | 同上 N 的三角波表，峰峰值 ±2047 |
| `DAC_Wave.c` | 按档位 N 查表复制到 `s_BaseWave[]`，再 Q15 缩放写入 `s_DacBuffer[]` |

> F407 版相对 F103 新增 **N=242**（1.3 kHz 档）、**N=251**（1.8 kHz 档）查表；其余 N 与 F103 共用表结构。

### 切频流程（无毛刺）

1. DAC 输出中点 2048  
2. 停 TIM6、停 DMA1 Stream5  
3. 查表复制到 `s_BaseWave`、更新 DMA 缓冲  
4. 重配 TIM6 PSC/ARR、DMA 长度 N  
5. `TIM_SetCounter(TIM6,0)` → 预载 `s_DacBuffer[0]` → 启动 DMA1 Stream5 + TIM6  

### 11 档预计算表（N, PSC, ARR）

与 `BoardConfig.h` 中 `FREQ_GEAR_TABLE` 一致（`TIM6_CLK_HZ` = 84 MHz）：

| index | target_hz | N | PSC | ARR | 备注 |
|-------|-----------|---|-----|-----|------|
| 0 | 1000 | 250 | 0 | 335 | |
| 1 | 1100 | 252 | 0 | 302 | N 相对 F103 +3 |
| 2 | 1200 | 250 | 0 | 279 | N 相对 F103 −2 |
| 3 | 1300 | 242 | 0 | 266 | N 相对 F103 −12，**新增查表** |
| 4 | 1400 | 252 | 0 | 237 | |
| 5 | 1500 | 250 | 0 | 223 | N 相对 F103 −4 |
| 6 | 1600 | 250 | 0 | 209 | |
| 7 | 1700 | 252 | 0 | 195 | |
| 8 | 1800 | 251 | 0 | 185 | N 相对 F103 +1，**新增查表** |
| 9 | 1900 | 254 | 0 | 173 | N 相对 F103 −2 |
| 10 | 2000 | 250 | 0 | 167 | |

每档频率误差应 ≤ **0.5‰**（`FREQ_ERROR_PERMIL_MAX`）。

### DAC 外设绑定（F407ZG）

| 项目 | 配置 |
|------|------|
| 定时器 | TIM6，TRGO = Update |
| DMA | **DMA1_Stream5**，Channel **7**，Mem→`DAC->DHR12R1`，循环模式，优先级 **VeryHigh** |
| 触发 | `DAC_Trigger_T6_TRGO` |
| 中断 | `DMA1_Stream5_IRQn`（若使能 TC） |

## C.3 ADC / 采样

| 宏 | 值 | 说明 |
|----|-----|------|
| `FFT_SIZE` | 1024 | 每通道样本数 |
| `ADC_SAMPLE_PSC` | 0 | TIM2 预分频 |
| `ADC_SAMPLE_ARR` | 1638 | TIM2 周期 |
| `ADC_SAMPLE_RATE_HZ` | `TIM2_CLK_HZ/(ARR+1)` ≈51251 | 由宏表达式计算，84M/1639 |
| `ADC_INTERCH_DELAY_SEC` | 492/21e6 | Rank1→Rank2 扫描延迟补偿（见下） |

### ADC 配置要点

- **ADC1 独立模式 + 扫描**：Rank1=CH2(Ui)，Rank2=CH3(Uo)
- 采样时间：**480 cycles**（@ 21 MHz ADC 时钟 ≈ 22.9 µs，等效 F103 的 ~20 µs 窗口）
- 转换时间：+12 cycles → 单通道总周期 492 cycles
- ADC 时钟：PCLK2/4 = **21 MHz**
- 外部触发：TIM2 CC2；每次触发转换 **2 通道**
- **DMA2_Stream0 Ch0**：外设 `ADC1->DR`，内存 `scan_raw[2048]` 半字，循环
- 拆包：`Ui[i]=scan_raw[2*i]`，`Uo[i]=scan_raw[2*i+1]`
- 帧周期 ≈ 1024 / 51.2 kHz ≈ **20 ms**
- **DMA2 Stream0 TC 中断**：仅停 TIM2、拆包、置 `g_ADC_FrameReady`；**禁止**在中断内调用 FFT
- **帧处理**：`TIM7` @ 20 ms 检测 `g_ADC_FrameReady` 后调用 `App_BackgroundProcess`

### ADC 异常判定

- 缓冲全近 0 或全近 4095 → 无效帧（`fail_code` 1 或 2）

## C.4 FFT 解算

| 宏 | 值 |
|----|-----|
| `FFT_AVG_COUNT` | 4 |
| `ADC_VREF` | 3.3 |
| `ADC_MAX_CODE` | 4095 |
| `FREQ_ERROR_PERMIL_MAX` | 0.5 |
| `FFT_BIN_SEARCH_RADIUS` | 3 |
| `FFT_BIN_TARGET_TOLERANCE` | 4 |
| `FFT_BIN_MIN_MAG` | 40.0（自动增益后浮点幅值量纲，峰值≈28000 标尺） |
| `FFT_AUTO_GAIN_PEAK` | 28000.0f |
| `UI_RMS_LOSS_V` | 0.010 |

### 处理流程

1. **RMS**：去均值后时域 AC-RMS，`sqrt(Σ(x-mean)²/N) × Vref/4095`（无论 FFT 成败均更新 `out->ui_rms_v` / `uo_rms_v`）
2. **FFT 预处理**：去直流 + **自动增益**（峰值归一到约 `FFT_AUTO_GAIN_PEAK`），写入 `float` 缓冲送入 **`arm_cfft_f32`**（**无汉宁窗**）
3. `arm_cfft_f32(&arm_cfft_sR_f32_len1024, fft_buf, 0, 1)`（Ui、Uo 各一次；实例结构来自 CMSIS-DSP）
4. **搜峰**：在目标频率 bin **±`FFT_BIN_SEARCH_RADIUS`** 内搜 Ui 最大幅值（浮点复数模平方比较）
5. **Uo 有效性**：在 **`target_bin`**（非 `peak_bin`）检查 Uo 幅值 ≥ `FFT_BIN_MIN_MAG`
6. **频率细化**：以 **`target_bin`** 为中心的三点抛物线插值（避免旁瓣误锁 `peak_bin`）
7. **增益**：`Av = Uo_RMS / Ui_RMS`（逐帧，再 4 帧滑动平均）
8. **相位**：**`target_bin`** 处互谱法 `Uo·conj(Ui)`，减去扫描延迟校正：
   ```
   delay_corr_deg = 360 × f × ADC_INTERCH_DELAY_SEC
   phase = normalize(cross_phase - delay_corr_deg)
   ```
9. **滑动平均**：RMS/Av/频率算术平均；相位用 sin/cos 分量平均后 `atan2`
10. **频率 OK**：\|f_meas - f_target\| / f_target ≤ 0.5‰
11. **调试变量**（Keil Watch）：`g_FFT_DbgUiPeakMag`、`g_FFT_DbgPeakBin`、`g_FFT_DbgTargetBin`

### fail_code

| 值 | 含义 |
|----|------|
| 1 | Ui ADC 缓冲异常（贴 0 或贴满量程） |
| 2 | Uo ADC 缓冲异常 |
| 3 | Ui 在搜峰窗口内幅值 < `FFT_BIN_MIN_MAG` |
| 4 | Uo 在 `target_bin` 幅值 < `FFT_BIN_MIN_MAG` |

## C.5 PID（位置式，100 ms 周期）

| 宏 | 值 |
|----|-----|
| `PID_KP` | 0.8 |
| `PID_KI` | 0.15 |
| `PID_KD` | 0.02 |
| `PID_INTEGRAL_SEP_V` | 0.3 |
| `PID_INTEGRAL_LIMIT` | 500.0 |
| `PID_FRAME_DIV` | 5 |

DMA 帧 ≈20 ms × 5 = **100 ms** 调节一次。误差 = `TARGET_UO_RMS_V - uo_rms_v`；积分分离；输出累加到 `gain_scale_q15` 并限幅。

## C.6 CFG 跳线检测

| 宏 | 值 |
|----|-----|
| `CFG_CHANGE_THRESH` | 0.05（5%） |
| `CFG_CHANGE_COUNT` | 3 帧 |

`|Av - Av_stable| > |Av_stable|×5% + 0.01` 连续 3 次 → `CFG_CHANGED`，更新 stable Av，重置 PID。稳定时 Av_stable 做 0.9/0.1 指数平滑。

## C.7 坏帧丢弃（App_Main）

- `ui_rms < UI_RMS_LOSS_V` → 丢帧  
- 相对上次有效值，Ui 或 Uo RMS **< 30%** → 丢帧（保留上次 OLED 示数）

## C.8 设计指标（验收参考，非硬编码断言）

| 指标 | 目标 |
|------|------|
| 频率测量误差 | ≤ 0.5‰ |
| RMS 误差 | ≤ 1%（需外参校准） |
| 相位误差 | ≤ 3°（需外参校准） |
| 自动模式 Uo | 1.5 V ±1% |

## C.9 第三方库

- **SPL**：`stm32f4xx_gpio/rcc/adc/dac/dma/tim/misc` 等
- **CMSIS-DSP**：`arm_cfft_f32`（1024 点）、`arm_math.h`、`arm_const_structs.h`
- 启动文件：`startup_stm32f40_41xxx.s`
- **Keil 编译**：Target `STM32F407ZG`，FPU `FPv4-SP-D16`，Flash 算法 1 MB
