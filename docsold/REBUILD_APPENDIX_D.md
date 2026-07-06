# 附录 D：验收测试清单（完成门禁）

> **规则**：全部 13 项通过后，工程方可交付。每项注明操作步骤与预期结果。  
> **完成态**：`APP_MEASURE_ENABLE=1`，`APP_OLED_ENABLE=1`。

---

## D.1 工程编译与启动（阶段 1）

| 步骤 | 预期 |
|------|------|
| Keil 打开 `USER/DSP_FFT.uvprojx`，Target = STM32F407ZG，编译 | **0 Error**，可生成 axf/hex |
| 烧录后复位 | PF9/PF10 有明确初始状态（常灭） |
| 查看 `main` | 完成 `Dwt_Init` → `AppState_Init` → `Calib_Load` → `Control_Init` → `WaveGen_Init` → `Measure_Init` → `AppKeys_Init` → `AppScheduler_Init` 后进入 **`while(1) { __WFI(); }`** |
| 确认 `app_config.h` | `APP_MEASURE_ENABLE=1`，`APP_OLED_ENABLE=1` |

---

## D.2 DAC 波形输出（阶段 2）

| 步骤 | 预期 |
|------|------|
| 上电默认 1 kHz 正弦 | 示波器测 PA4：单条正弦，频率约 **1 kHz** |
| 峰峰值 | 未校准时约 **0.5 V**（`DAC_VPP_DEFAULT`） |
| DAC 中点 | 直流偏置约在 **1.65 V**（2048/4095×3.3V）附近 |
| 波形来源 | ROM 表 `sine_ref_256[]`，非运行时 `sinf()` |
| 硬件绑定 | TIM2 TRGO + DMA1_Stream5 → DAC1 CH1 |

---

## D.3 频率与波形切换（阶段 2/7）

| 步骤 | 预期 |
|------|------|
| 连按 PE3（频率键） | OLED L1 频率从 1.0kHz 步进到 2.0kHz 再回到 1.0kHz，共 11 档 |
| 按 PE2（波形键） | L1 在 `SIN` / `TRI` 间切换；PA4 波形形状相应变化 |
| 各档 L4 | 稳定后 `Fm:xxxxHz` 接近设定频率（误差 <1%） |

---

## D.4 双通道 ADC 采集（阶段 3）

| 步骤 | 预期 |
|------|------|
| 运放电路正常接入，上电 ≥1 s | `adc_dma_buf[]` 非常数；约每 500 ms 完成一帧采集 |
| DMA ISR 审查 | `DMA2_Stream0_IRQHandler` **仅**停采样 + `AppScheduler_SampleReady()`，**不含** `Measure_Process` |
| 帧处理入口 | `PendSV_Handler` 在 `s_sample_pending` 时调用 `Measure_Process` |
| 断开 PA5 探头 | Ui 相关码值下降；后续可置 `FAULT_SIGNAL_LOSS` |

---

## D.5 FFT 测量 — 基本值（阶段 4）

> **验收方式**：本阶段以 **Keil Watch** 为准，**不要求** OLED 全参稳定显示（属 **D.9 / 阶段 6**）。  
> 等待至少 1 帧采集完成（上电后 ≥1.5 s）。

### 操作步骤

1. 烧录后 **Run** 约 2 s（等待首帧 FFT 完成）
2. **Halt** 调试暂停
3. 在 **Watch** 中添加下列表达式

### Watch 必看项与通过标准

| Watch 表达式 | 通过标准 |
|--------------|----------|
| `g_app.amp_mode` | `0`（MODE_MANUAL） |
| `g_app.freq_hz` | `1000` |
| `g_app.ui_rms_mv` | **80～120**（mV） |
| `g_app.uo_rms_v` | 与运放增益一致，非 0 |
| `g_app.gain` | ≈ `uo_rms_v / (ui_rms_mv/1000)`；反相板为**负**（阶段 5/6 后） |
| `g_app.phase_deg` | **±160°～±180°**（±20° 噪声可接受） |
| `g_app.freq_meas_hz` | **990～1010** |
| `g_app.measure_ok` | `1`（增强字段） |
| `g_app.fault_flags` | `0` |

### 旁路自检（可选）

将 **PA4 直连 PA5**（断开运放板），Run 2 s → Halt：`ui_rms_mv` 应有合理读数，`measure_ok=1`。

---

## D.6 增益极性与相位（阶段 5，增强）

| 步骤 | 预期 |
|------|------|
| 确认反相放大配置 | \|Ph\| > 90° 时，L3 `Gn:` 显示 **负值** |
| Watch `g_app.gain` | 反相板为负，同相板（若可配置）为正 |

---

## D.7 三键人机交互与 PI 自动稳幅（阶段 7/5）

| 步骤 | 预期 |
|------|------|
| PE3 | 仅改变频率，模式/波形不变 |
| PE5 | L1 后缀 `MAN`↔`AUT`；L5 在 `CFG:xxx`↔`Auto->1.5V` 间切换 |
| PE2 | 波形切换；手动模式下 Ui 仍约 100 mV 量级 |
| PE5 进入 AUTO，等待 10～30 s | L2 `Uo:` 收敛到 **1.35～1.65 V** |
| 快速连按 | 无死机、无卡键；参数与显示一致 |

---

## D.8 CFG 跳线检测（阶段 8，增强）

| 步骤 | 预期 |
|------|------|
| 系统稳定，手动模式 L5 | `CFG:STABLE` |
| 拨动 S2/S3/S4 改变增益 | 数帧内 L3 `Gn:` 明显变化 |
| 持续观察 | L5 变为 **`CFG:CHANGED`** |
| 电路稳定不动 | 再度收敛后 L5 回 **`CFG:STABLE`** |

---

## D.9 OLED 5 行全参数显示（阶段 6）

| 步骤 | 预期 |
|------|------|
| 观察 5 行 | L1～L5 内容与附录 B.8 格式一致 |
| 刷新 | 参数变化后及时更新，无严重闪烁 |
| 故障时 L5 | 显示 `FLT:0xNN` 或 `SAT!`（优先级见附录 B.8） |

---

## D.10 调度架构（阶段 4）

| 步骤 | 预期 |
|------|------|
| 代码审查 | DMA2_Stream0 / SysTick / PendSV 各有独立处理逻辑 |
| DMA ISR | **无** `arm_cfft_f32` / `Measure_Process` 调用 |
| 运行中按键 | 10 ms 内响应；FFT 不阻塞 SysTick |
| 主循环 | **仅** `__WFI()`，无业务逻辑 |

---

## D.11 故障 LED（阶段 8，增强）

| 步骤 | 预期 |
|------|------|
| 正常测量 | PF9 **常灭**，不闪烁 |
| 断开 PA5 或 PA6 信号 | 数帧内 PF9 **闪烁**（约 1 Hz） |
| 恢复连接 | 闪烁停止，PF9 常灭 |

---

## D.12 Flash 校准（阶段 8）

| 步骤 | 预期 |
|------|------|
| 首次上电（Flash 无有效 magic） | `g_calib.dac_vpp_100mV = 0.5` |
| 调试器修改 `g_calib.dac_vpp_100mV` 后调用 `Calib_Save()` | 复位后 `Calib_Load()` 保留新值 |
| 校准后手动模式 | Ui RMS ≈ 100 mV（示波器确认） |

---

## D.13 整机长时间运行

| 步骤 | 预期 |
|------|------|
| 连续运行 ≥ 10 min | 无死机、无 HardFault |
| 全程自动模式 | Uo 维持 1.5 V 附近；无增益发散 |
| 随机切换频率/波形/模式 | 系统可恢复稳定测量 |

---

## 验收签字表（Agent 自填）

| 编号 | 通过 | 备注 |
|------|------|------|
| D.1 | ☐ | |
| D.2 | ☐ | |
| D.3 | ☐ | |
| D.4 | ☐ | |
| D.5 | ☐ | |
| D.6 | ☐ | |
| D.7 | ☐ | |
| D.8 | ☐ | |
| D.9 | ☐ | |
| D.10 | ☐ | |
| D.11 | ☐ | |
| D.12 | ☐ | |
| D.13 | ☐ | |

**13/13 通过 → 任务完成**
