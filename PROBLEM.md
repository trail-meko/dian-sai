# 407运放增益测试电路第2版 — 开发踩坑记录

> 最后更新：2026-06-19
> 条目：6 条 | 未修复：0 条

本文档记录实战开发中遇到的问题与修复结论。静态架构约束见 `Docs/REBUILD_PROMPT.md`。

---

## ADC/DMA

### 首帧测量正常、之后 ADC 几乎不更新

| 字段 | 内容 |
|------|------|
| 现象 | 上电瞬间 `g_MeasureResult` / Watch 中 ADC 缓冲有效；随后 `g_AD_Ui` 与测量值几乎不再变化 |
| 根因 | ① `EndProcess` 中 `ADC_Dual_RestartCapture()` 禁用并重配 DMA2_Stream0，F407 上第二次采集链易断；② `App_BackgroundProcess` 入口过早清 `g_ADC_FrameReady`，与 DMA ISR 竞态；③ TIM7 每 20ms 可重入帧处理，第一次 `EndProcess` 已重启 TIM2 时第二次仍在读 `g_AD_Ui`，与拆包冲突 |
| 错误做法 | `EndProcess` 里 `DMA_Cmd(DISABLE)` + 改 `NDTR/M0AR` 全量重启；在 `App` 入口清 `g_ADC_FrameReady`；无 `s_AppProcessing` 重入保护；FFT 直接指向 `g_AD_Ui` 不做快照 |
| 正确做法 | `EndProcess` 仅清 TC 标志、`TIM_SetCounter(0)`、`TIM_Cmd(ENABLE)`；ISR 在整帧边界停 TIM2 且要求 `g_ADC_FrameReady==0` 才接新帧；`s_AppProcessing` 防重入；`BeginProcess` 复制到 `s_SnapUi/Uo` 再送 FFT |
| 涉及文件 | `HARDWARE/ADC_Dual.c`, `HARDWARE/App_Main.c` |
| 是否已修复 | 是 |

---

## OLED

### 主循环空转导致 OLED 一直显示 0

| 字段 | 内容 |
|------|------|
| 现象 | Keil Watch 中 `g_MeasureResult` 正常更新，OLED L3～L7 一直为 0 或停在初值 |
| 根因 | 阶段 4 将 `main` 的 `while(1)` 改为空循环后，无人消费 `s_DisplayDirty`；`Display_Init()` 仅上电刷新一次，彼时测量尚未就绪 |
| 错误做法 | TIM4 只置 `s_DisplayDirty`，主循环不调用 `Display_Poll()` 或等效服务 |
| 正确做法 | 由定时器中断驱动刷新：`TIM4` 置 dirty，`TIM7` 末尾调用 `Display_ServicePending()`；测量成功后 `Display_Invalidate()` |
| 涉及文件 | `HARDWARE/Display.c`, `HARDWARE/Display.h`, `USER/stm32f4xx_it.c`, `HARDWARE/App_Main.c`, `USER/main.c` |
| 是否已修复 | 是 |

### TIM4 ISR 内执行完整 I2C 刷屏后不再更新

| 字段 | 内容 |
|------|------|
| 现象 | 上电约 1s 后 OLED 更新一次，之后数值冻结；或长时间无刷新 |
| 根因 | `Display_Refresh()` 含软件 I2C，在 TIM4 ISR 中执行时被高优先级 DMA2_Stream0 中断抢占，I2C 时序/状态异常，`s_DisplayBusy` 可能卡死为 1；另：`Display_SetRefreshEnable(0)` 同时关闭 TIM4 中断，FFT 期间丢失 200ms 节拍 |
| 错误做法 | 在 `Display_OnTim4Irq` 内直接调用 `Display_Refresh()`；FFT 期间 `TIM_ITConfig(TIM4, DISABLE)` |
| 正确做法 | TIM4 仅 `s_DisplayDirty=1`；`Display_SetRefreshEnable` 只控制 `s_RefreshEnabled` 标志，不关 TIM4；在 TIM7（FFT 结束后）调用 `Display_ServicePending()` 执行刷屏 |
| 涉及文件 | `HARDWARE/Display.c`, `USER/stm32f4xx_it.c`, `HARDWARE/App_Main.c` |
| 是否已修复 | 是 |

---

## 调度架构

### TIM7 帧处理重入

| 字段 | 内容 |
|------|------|
| 现象 | 与「首帧后 ADC 停更」同时出现；FFT 偶发异常或采集/显示时序混乱 |
| 根因 | `g_ADC_FrameReady==1` 持续期间 TIM7 每 20ms 再次进入 `App_BackgroundProcess`；若 FFT 耗时接近或超过 20ms，两次处理重叠 |
| 错误做法 | 仅靠 `g_ADC_FrameReady` 门控，无 `s_AppProcessing` |
| 正确做法 | `s_AppProcessing` 为 1 时直接返回；帧标志由 `EndProcess` 清除，不在 `App` 入口抢先清除 |
| 涉及文件 | `HARDWARE/App_Main.c`, `USER/stm32f4xx_it.c` |
| 是否已修复 | 是 |

### 初始化顺序：OLED 与 ADC 并发

| 字段 | 内容 |
|------|------|
| 现象 | 上电阶段偶发首帧异常或显示/采集_startup 竞态（与 OLED 长时间 I2C 重叠） |
| 根因 | `ADC_Dual_Init` 与 `Key_StartScan` 已启动中断后，`Display_Init` 内 `OLED_Init`/`Display_Refresh` 阻塞较久 |
| 错误做法 | `ADC_Dual_Init` → `Key_StartScan` → `Display_Init` |
| 正确做法 | `Display_Init` 放在 `ADC_Dual_Init` 之前，先完成 OLED 初始化再启动采集与 TIM7 |
| 涉及文件 | `USER/main.c` |
| 是否已修复 | 是 |

---

## 调试 / Keil

### Watch 仅在 Stop 时看到变量变化

| 字段 | 内容 |
|------|------|
| 现象 | Run 全速时 Watch 数值不变，Halt 后才变化 |
| 根因 | µVision Watch 默认仅在 CPU 停住时采样目标机内存，非程序未运行 |
| 错误做法 | 误以为 ADC/FFT 未工作，因 Run 时 Watch 不动而反复改采集代码 |
| 正确做法 | 验收时 Run 1～2s 后 Halt 再看 Watch；或 SWV/逻辑分析仪做在线监视 |
| 涉及文件 | — |
| 是否已修复 | 待确认（调试认知，非代码缺陷） |
