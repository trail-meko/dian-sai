#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>
#include "stm32f4xx.h"

/* -------------------------------------------------------------------------- */
/* A.2 Pin map                                                                */
/* -------------------------------------------------------------------------- */
#define PIN_DAC_GPIO          GPIOA
#define PIN_DAC_PIN           GPIO_Pin_4

#define DAC_ACTIVE_CHANNEL    DAC_Channel_1
#define DAC_DMA_STREAM        DMA1_Stream5
#define DAC_DMA_CHANNEL       DMA_Channel_7

#define PIN_ADC_UI_GPIO       GPIOB
#define PIN_ADC_UI_PIN        GPIO_Pin_0
#define PIN_ADC_UO_GPIO       GPIOB
#define PIN_ADC_UO_PIN        GPIO_Pin_1

#define PIN_KEY1_GPIO         GPIOE
#define PIN_KEY1_PIN          GPIO_Pin_7
#define PIN_KEY2_GPIO         GPIOE
#define PIN_KEY2_PIN          GPIO_Pin_8
#define PIN_KEY3_GPIO         GPIOE
#define PIN_KEY3_PIN          GPIO_Pin_9

#define PIN_OLED_SCL_GPIO     GPIOB
#define PIN_OLED_SCL_PIN      GPIO_Pin_6
#define PIN_OLED_SDA_GPIO     GPIOB
#define PIN_OLED_SDA_PIN      GPIO_Pin_7

/* SSD1306 software I2C: 8-bit write addr (7-bit 0x3C); alternate 0x7A if SA0=1 */
#define OLED_I2C_ADDR_WRITE   0x78U
#define OLED_I2C_ADDR_ALT     0x7AU
#define OLED_CTRL_SSD1309     0U   /* 0=SSD1306, 1=SSD1309 */

#define PIN_FAULT_LED_GPIO    GPIOA
#define PIN_FAULT_LED_PIN     GPIO_Pin_8

#define ADC_CHANNEL_UI        ADC_Channel_8
#define ADC_CHANNEL_UO        ADC_Channel_9

/* -------------------------------------------------------------------------- */
/* C.1 System clock                                                           */
/* -------------------------------------------------------------------------- */
#define SYSCLK_HZ             168000000UL
#define TIM6_CLK_HZ           84000000UL
#define TIM2_CLK_HZ           84000000UL
#define ADC_CLK_HZ            21000000UL

/* -------------------------------------------------------------------------- */
/* C.2 Waveform / DAC                                                         */
/* -------------------------------------------------------------------------- */
#define FREQ_MIN_HZ           1000U
#define FREQ_MAX_HZ           2000U
#define FREQ_STEP_HZ          100U
#define FREQ_GEAR_COUNT       11U

#define WAVE_TABLE_MIN_POINTS 128U
#define WAVE_TABLE_MAX_POINTS 256U

#define DAC_MID_CODE          2048U
#define DAC_WAVE_PEAK         2047

#define MANUAL_GAIN_SCALE_Q15           2862U
#define MANUAL_GAIN_SCALE_TRIANGLE_Q15  3504U

#define TARGET_UO_RMS_V       0.75f
#define TARGET_UI_RMS_V       0.10f

#define GAIN_SCALE_MIN_Q15    328U
#define GAIN_SCALE_MAX_Q15    31129U

#define FREQ_ERROR_PERMIL_MAX 0.5f

typedef struct
{
    uint16_t target_hz;
    uint16_t n_points;
    uint16_t psc;
    uint16_t arr;
} DacFreqGear_t;

extern const DacFreqGear_t g_DacFreqGear[FREQ_GEAR_COUNT];

/* -------------------------------------------------------------------------- */
/* C.3 ADC / sampling                                                         */
/* -------------------------------------------------------------------------- */
#define FFT_SIZE              1024U
#define ADC_SCAN_RAW_LEN      (FFT_SIZE * 2U)

#define ADC_SAMPLE_PSC        0U
#define ADC_SAMPLE_ARR        4199U   /* 84MHz/4200=20KHz，低于ADC上限(21KHz)，确保无触发丢失 */
#define ADC_SAMPLE_RATE_HZ    ((float)TIM2_CLK_HZ / (float)(ADC_SAMPLE_ARR + 1U))
#define ADC_INTERCH_DELAY_SEC (492.0f / 21000000.0f)

#define ADC_VREF              3.3f
#define ADC_MAX_CODE          4095U

/* -------------------------------------------------------------------------- */
/* C.4 FFT analysis                                                           */
/* -------------------------------------------------------------------------- */
#define FFT_AVG_COUNT         10U  /* 10帧×200ms=2s滑动窗口，提高频率/RMS/增益稳定性 */
#define FFT_BIN_SEARCH_RADIUS 3U
#define FFT_BIN_TARGET_TOLERANCE 4U
#define FFT_BIN_MIN_MAG       40.0f
#define FFT_AUTO_GAIN_PEAK    28000.0f
#define UI_RMS_LOSS_V         0.003f
/* Ui ADC RMS 校准：显示 101.2mV，实测 98.74mV，目标 100.0mV（配合 DAC 系数） */
#define UI_RMS_CAL_SCALE       3.1535f
/* Uo ADC RMS 校准：初校 1.496V/0.309V，二次微调 1.502V/1.494V */
#define UO_RMS_CAL_SCALE      18.0f

/* -------------------------------------------------------------------------- */
/* C.5 PID（分段调节，100 ms 周期）                                            */
/* -------------------------------------------------------------------------- */
#define PID_INTEGRAL_LIMIT    500.0f
#define PID_FRAME_DIV         5U

/* |error| 分区阈值（V） */
#define PID_SEG_THRESH_FINE_V 0.35f
#define PID_SEG_THRESH_MID_V  0.55f

/* 粗调 |e| > 0.55 V */
#define PID_KP_COARSE         0.10f
#define PID_KI_COARSE         0.03f
#define PID_KD_COARSE         0.0f

/* 中调 0.35 V < |e| <= 0.55 V */
#define PID_KP_MID            0.07f
#define PID_KI_MID            0.0f
#define PID_KD_MID            0.0f

/* 精调 |e| <= 0.35 V — 纯 P（I/D=0） */
#define PID_KP_FINE           0.03f
#define PID_KI_FINE           0.0f
#define PID_KD_FINE           0.0f

/* -------------------------------------------------------------------------- */
/* C.6 CFG detect                                                             */
/* -------------------------------------------------------------------------- */
#define CFG_CHANGE_THRESH     0.05f
#define CFG_CHANGE_COUNT      3U

/* -------------------------------------------------------------------------- */
/* C.7 Bad frame drop                                                         */
/* -------------------------------------------------------------------------- */
#define BAD_FRAME_RATIO       0.30f

/* -------------------------------------------------------------------------- */
/* B.3 Fault flags                                                            */
/* -------------------------------------------------------------------------- */
#define FAULT_ADC             (1U << 0)
#define FAULT_FFT             (1U << 1)
#define FAULT_SIGNAL_LOSS     (1U << 2)
#define FAULT_PID_SAT         (1U << 3)

#define CFG_STABLE            0U
#define CFG_CHANGED           1U

/* -------------------------------------------------------------------------- */
/* Interrupt priority (lower number = higher priority on STM32 NVIC)          */
/* -------------------------------------------------------------------------- */
#define IRQ_PRIO_DMA2_STREAM0 0U
#define IRQ_PRIO_TIM4         2U
#define IRQ_PRIO_TIM7         2U

#define DISPLAY_REFRESH_MS    200U
#define KEY_SCAN_MS           20U

/* 阶段 6/7 功能开关：全 0 = 仅 DAC（等同改前 Stage1 桩行为），用于控制变量排查 */
#define FEATURE_DISPLAY_ENABLE  1U
#define FEATURE_KEY_ENABLE      1U

#endif /* BOARD_CONFIG_H */
