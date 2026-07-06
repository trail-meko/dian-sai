# 任务：从零重建 STM32F407 反向比例运放增益自动测试系统

## 角色

你是一名嵌入式固件工程师。请在 **Keil µVision** 上，为 **STM32F407ZGT6** 从零搭建完整工程，实现一块固定实验板上的运放增益自动测试功能。

## 还原精度说明

- **功能等价**：行为、指标、人机交互与验收附录一致即可；源文件划分、函数命名、寄存器写法可与参考工程不同。
- **固定硬件**：必须能在现有实验板（见附录 A）上烧录运行。
- **技术栈不可更换**：Keil µVision + **STM32F4xx Standard Peripheral Library** + `startup_stm32f40_41xxx.s` + **CMSIS-DSP `arm_cfft_f32`（1024 点）**；编译器 **必须开启 FPU**（`--fpu=FPv4-SP-D16`）。

## 唯一目标

实现 **附录 D 全部验收项（D.1～D.14）**。12 项模块验收 + 2 项整机验收全部通过，方可视为完成。

## 系统一句话

DAC 输出 1～2 kHz 正弦/三角激励 → 经耦合电容驱动板载反相比例运放 → 同步采集 Ui/Uo → FFT 解算 RMS/增益/相位 → OLED 显示；可选 PID 自动稳幅；跳线改变电路后软件自动识别 `CFG:CHANGED`。

## 架构硬约束（违反即返工）

| # | 约束 |
|---|------|
| 1 | **中断驱动**：`main()` 初始化后 `while(1)` **空转**；重业务在定时器/帧中断路径执行，**禁止**在主循环塞 FFT/PID/显示逻辑 |
| 2 | **信号链**：DAC(PA4) → 耦合电容 → 运放 → Ui(PA2) / Uo(PA3) ADC 采集 |
| 3 | **ADC 方案**：ADC1 **扫描模式** CH2+CH3，TIM2 触发，**DMA2_Stream0 Ch0** 搬运；**不要**使用遗留 `FFTDMAAD.c` 的单通道方案，**不要**强依赖 ADC1+ADC2 规则同步（参考实现已改为扫描） |
| 4 | **DAC 方案**：TIM6_TRGO + **DMA1_Stream5 Ch7** → DAC1(PA4)；11 档独立波形点数 N；**Flash 预存正弦/三角查表**（`DAC_SinTables.inc` / `DAC_TriTables.inc`，含 N=242/249/250/251/252/254/256），运行时不算 `sinf`；切频时先输出中点 2048 再停 DMA；`StartOutput` 须清零 TIM6 并预载首样 |
| 5 | **FFT**：1024 点 **`arm_cfft_f32`**；频率/相位/增益用频域；RMS 用去直流后的 **时域 AC-RMS**；FFT 输入为去直流后自动增益至峰值 ≈ **28000.0f** 的浮点缓冲 |
| 6 | **增益符号**：\|Δφ\| > 90° 时，显示 Av 为负（反相运放物理语义） |
| 7 | **CFG 检测**：**不读** S2/S3/S4 的 GPIO；通过实测 Av 相对变化 ≥5% 连续 3 帧判定 `CFG:CHANGED` |
| 8 | **禁止编译**：`FFTDMAAD.c`、`PWM.c` 及工程中未编入的遗留文件 |
| 9 | **时钟**：SYSCLK = **168 MHz**（标准 HSE+PLL）；**DAC/ADC 定时器计数时钟** `TIM6_CLK_HZ` / `TIM2_CLK_HZ` = **84 MHz**（APB1 定时器 ×2）；频率公式用 `TIMx_CLK_HZ`，**不得**误用 SYSCLK |

## 强制实现顺序

每完成一阶段，对照附录 D 中对应验收编号自检，**全部打勾后再进入下一阶段**。

```
阶段 1  工程骨架 + BoardConfig.h + PeriphInit(NVIC) + lit(LED) + Delay
        main 初始化链；while(1) 空循环；Keil Target=STM32F407ZG，FPU 开启
阶段 2  DAC_Wave — Flash 查表正弦/三角 + TIM6/DMA1；默认 1 kHz 正弦；PA4 波形稳定无重影
阶段 3  ADC_Dual — ADC1 扫描 CH2/CH3；DMA2 Stream0 TC 仅拆包并置 g_ADC_FrameReady（不在此跑 FFT）
阶段 4  FFT_Analyze + Key(TIM7) — TIM7 调用 App_BackgroundProcess；**D.5 用 Keil Watch 验收**（非 OLED）
阶段 5  App_Main + main — 帧处理链路贯通，增益极性逻辑
阶段 6  Display + OLED — 8 行全参数刷新（TIM4 @ 200ms）
阶段 7  Key — 三键防抖（TIM7 @ 20ms）+ 频率/模式/波形切换
阶段 8  PID_Ctrl — 自动模式 Uo 稳 1.5V + CFG 检测 + 故障 LED
```

## 建议目录结构

```
Project.uvprojx
User/           main.c, stm32f4xx_it.c, stm32f4xx_conf.h
Hardware/       BoardConfig.h, PeriphInit, DAC_Wave, DAC_SinTables.inc,
                DAC_TriTables.inc, ADC_Dual, FFT_Analyze, ...
System/         Delay, lit
Start/          startup_stm32f40_41xxx.s, system_stm32f4xx.c, stm32f4xx.h
Library/        STM32F4xx_StdPeriph_Driver
CMSIS/          Core + DSP（arm_cfft_f32 等）
Docs/           本重建文档包
```

## 完成定义（Definition of Done）

- [ ] Keil 编译 **0 Error**，目标芯片 **STM32F407ZG**，Flash 算法 **STM32F4xx 1 MB**，FPU **FPv4-SP-D16** 已启用
- [ ] 附录 D 验收项 **D.1～D.14 全部通过**
- [ ] 手动模式 Ui RMS ≈ **100 mV**（正弦/三角分别校准）
- [ ] 自动模式 Uo RMS 收敛至 **1.5 V ±1%**
- [ ] 反相运放下 Av 为负、Ph ≈ **±180°**（允许测量噪声）
- [ ] 拨动跳线 S2/S3/S4 后 OLED 显示 **CFG:CHANGED**
- [ ] 故障时 PA8 LED **闪烁**

## 工作方式（必须遵守）

1. **先输出**「模块清单 + 8 阶段计划 + 风险点」，再写代码。
2. **每阶段结束**列出本阶段验收项及自测方法（示波器/OLED/操作步骤）。
3. **不得跳过** ADC/FFT 直接写 OLED 占位数据。
4. 引脚、宏、算法参数以 **附录 A/C** 为准；`DESIGN.md` 与重建包一致，描述 F407 + TIM6/DMA1 + ADC1 扫描 + `arm_cfft_f32` 现行架构。
5. 交付物：可编译 Keil 工程 + 简短 `README` 说明烧录与验收步骤。

## 附录索引

| 文件 | 内容 |
|------|------|
| [REBUILD_APPENDIX_A.md](REBUILD_APPENDIX_A.md) | 硬件接线拓扑与引脚表 |
| [REBUILD_APPENDIX_B.md](REBUILD_APPENDIX_B.md) | 模块划分、API、数据流、中断分工 |
| [REBUILD_APPENDIX_C.md](REBUILD_APPENDIX_C.md) | 算法、宏参数、频率档位表 |
| [REBUILD_APPENDIX_D.md](REBUILD_APPENDIX_D.md) | **验收测试清单（完成门禁）** |

---

> 将本文件与四个附录一并交给 Agent。Agent 应先通读附录 D，再按阶段实现。
