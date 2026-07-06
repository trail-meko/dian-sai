# 附录 A：硬件接线拓扑与引脚

## A.1 被测对象（定性，不写阻值）

板载 **反相比例运算放大器** 增益测试电路：

```
                    耦合电容（隔直流）
TP_DAC (PA4) ──────────────────────→ 运放输入网络
                                          │
                                     [反相放大器]
                                          │
TP_Ui  (PA2) ←── 探测运放输入端交流信号 ──┤
TP_Uo  (PA3) ←── 探测运放输出端交流信号 ──┘
```

- **不要求**在固件中写入 Rf/Ri 或理论增益；Av 完全由 **Uo_RMS / Ui_RMS** 实测。
- 跳线 **S2 / S3 / S4** 改变外围电阻配置，从而改变实测增益；**软件不读这些 GPIO**。

## A.2 MCU 与引脚表（与 `BoardConfig.h` 一致）

| 项目 | 规格 |
|------|------|
| MCU | STM32F407ZGT6（LQFP144），Cortex-M4F |
| 系统时钟 | 168 MHz（`SYSCLK_HZ`） |
| DAC/ADC 定时器时钟 | 84 MHz（`TIM6_CLK_HZ` / `TIM2_CLK_HZ`） |

| 测试点/外设 | MCU 引脚 | 说明 |
|-------------|----------|------|
| TP_DAC | PA4 | DAC1 模拟输出，12-bit，中点偏置 2048 |
| TP_Ui | PA2 | ADC1_IN2，运放输入端交流信号 |
| TP_Uo | PA3 | ADC1_IN3，运放输出端交流信号 |
| KEY1 | PB10 | 频率切换，低电平有效，内部上拉 |
| KEY2 | PB11 | 手动/自动模式切换 |
| KEY3 | PB12 | 正弦/三角波切换 |
| OLED SCL | PB8 | 软件 I2C |
| OLED SDA | PB9 | 软件 I2C |
| 故障 LED | PA8 | 有故障标志时闪烁，无故障时常亮/灭（低有效点亮） |

## A.3 模拟前端假设

- DAC 输出经 **交流耦合** 后进入运放；ADC 采集的是 **交流分量**（允许存在中点偏置，FFT 前去直流）。
- ADC 参考电压 **Vref = 3.3 V**，12-bit（0～4095）。
- Ui 手动模式目标有效值约 **100 mV**；自动模式 PID 目标 **Uo 有效值 1.5 V**。

## A.4 F103 遗留描述（勿照搬）

以下出现在旧版 F103 文档/早期 `DESIGN.md`，与现板/现码不一致，**忽略**：

| 过时描述 | 现工程实际（F407） |
|----------|-------------------|
| STM32F103RCT6 / 72 MHz | **STM32F407ZGT6 / 168 MHz SYSCLK** |
| Ui/Uo = PA0/PA1 | PA2/PA3 |
| DAC 用 TIM3 + DMA1_Ch7 | **TIM6 + DMA1_Stream5 Ch7** |
| ADC DMA1_Ch1 | **DMA2_Stream0 Ch0** |
| ADC1+ADC2 规则同步模式 | ADC1 扫描 CH2→CH3 |
| `cr4_fft_1024_stm32` | **CMSIS-DSP `arm_cfft_f32`** |
| FFT 乘汉宁窗 | 去直流 + 自动增益（28000.0f）后送 f32 FFT，无窗函数 |
| FFT 在 DMA ISR 内执行 | DMA 仅拆包；FFT 在 **TIM7** 中经 `App_BackgroundProcess` 执行 |
| 主循环轮询 `g_ADC_FrameReady` | `while(1)` **空循环**；帧处理在 TIM7 |
| F103 频率表 PSC/ARR | 见附录 C **F407 11 档表**（`TIM6_CLK` = 84 MHz） |

## A.5 接线验收（上电前）

- [ ] TP_DAC 经耦合电容接运放输入端
- [ ] TP_Ui、TP_Uo 探针接在运放输入/输出测试点
- [ ] OLED、三键、LED 已按上表连接
- [ ] 示波器地线与板子共地
