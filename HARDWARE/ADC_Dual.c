#include "stm32f4xx.h"
#include "BoardConfig.h"
#include "ADC_Dual.h"

uint16_t g_AD_Ui[FFT_SIZE];
uint16_t g_AD_Uo[FFT_SIZE];
volatile uint8_t g_ADC_FrameReady;

static uint16_t s_ScanRaw[ADC_SCAN_RAW_LEN];
static uint16_t s_SnapUi[FFT_SIZE];
static uint16_t s_SnapUo[FFT_SIZE];
static volatile uint8_t s_FramePaused;

static void ADC_Dual_UnpackFrame(void)
{
    uint16_t i;

    for (i = 0U; i < FFT_SIZE; i++)
    {
        g_AD_Ui[i] = s_ScanRaw[(uint16_t)(2U * i)];
        g_AD_Uo[i] = s_ScanRaw[(uint16_t)(2U * i + 1U)];
    }
}

static void ADC_Dual_ConfigTim2(void)
{
    TIM_TimeBaseInitTypeDef tim_base;
    TIM_OCInitTypeDef tim_oc;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseStructInit(&tim_base);
    tim_base.TIM_Prescaler = ADC_SAMPLE_PSC;
    tim_base.TIM_Period = ADC_SAMPLE_ARR;
    tim_base.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_base.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tim_base);

    TIM_OCStructInit(&tim_oc);
    tim_oc.TIM_OCMode = TIM_OCMode_PWM1;
    tim_oc.TIM_OutputState = TIM_OutputState_Enable;
    tim_oc.TIM_Pulse = (ADC_SAMPLE_ARR + 1U) / 2U;
    tim_oc.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC2Init(TIM2, &tim_oc);
    TIM_OC2PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM2, ENABLE);
}

static void ADC_Dual_ConfigDma(void)
{
    DMA_InitTypeDef dma;
    NVIC_InitTypeDef nvic;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);

    DMA_DeInit(DMA2_Stream0);
    DMA_StructInit(&dma);
    dma.DMA_Channel = DMA_Channel_0;
    dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
    dma.DMA_Memory0BaseAddr = (uint32_t)s_ScanRaw;
    dma.DMA_DIR = DMA_DIR_PeripheralToMemory;
    dma.DMA_BufferSize = ADC_SCAN_RAW_LEN;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_Init(DMA2_Stream0, &dma);

    DMA_ITConfig(DMA2_Stream0, DMA_IT_TC, ENABLE);

    nvic.NVIC_IRQChannel = DMA2_Stream0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = IRQ_PRIO_DMA2_STREAM0;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
}

static void ADC_Dual_ConfigAdc(void)
{
    GPIO_InitTypeDef gpio;
    ADC_CommonInitTypeDef adc_common;
    ADC_InitTypeDef adc;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    gpio.GPIO_Pin = PIN_ADC_UI_PIN | PIN_ADC_UO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AN;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_ADC_UI_GPIO, &gpio);

    ADC_CommonStructInit(&adc_common);
    adc_common.ADC_Mode = ADC_Mode_Independent;
    adc_common.ADC_Prescaler = ADC_Prescaler_Div4;
    adc_common.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
    adc_common.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
    ADC_CommonInit(&adc_common);

    ADC_StructInit(&adc);
    adc.ADC_Resolution = ADC_Resolution_12b;
    adc.ADC_ScanConvMode = ENABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T2_CC2;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfConversion = 2;
    ADC_Init(ADC1, &adc);

    ADC_RegularChannelConfig(ADC1, ADC_CHANNEL_UI, 1, ADC_SampleTime_480Cycles);
    ADC_RegularChannelConfig(ADC1, ADC_CHANNEL_UO, 2, ADC_SampleTime_480Cycles);

    /* 扫描 Rank1→Rank2 须逐通道 EOC 触发 DMA，否则 DR 仅末次(Uo)进缓冲 */
    ADC_EOCOnEachRegularChannelCmd(ADC1, ENABLE);
    ADC_DMARequestAfterLastTransferCmd(ADC1, ENABLE);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
}

void ADC_Dual_Init(void)
{
    s_FramePaused = 0U;
    g_ADC_FrameReady = 0U;

    ADC_Dual_ConfigTim2();
    ADC_Dual_ConfigDma();
    ADC_Dual_ConfigAdc();

    DMA_Cmd(DMA2_Stream0, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
}

float ADC_Dual_GetSampleRateHz(void)
{
    float fs_tim;
    float fs_adc_max;

    fs_tim = (float)TIM2_CLK_HZ /
             (float)((ADC_SAMPLE_PSC + 1U) * (ADC_SAMPLE_ARR + 1U));

    fs_adc_max = (float)ADC_CLK_HZ / 492.0f / 2.0f;

    if (fs_tim > fs_adc_max)
    {
        return fs_adc_max;
    }

    return fs_tim;
}

void ADC_Dual_BeginProcess(uint16_t **snap_ui, uint16_t **snap_uo)
{
    uint16_t i;

    TIM_Cmd(TIM2, DISABLE);
    DMA_ITConfig(DMA2_Stream0, DMA_IT_TC, DISABLE);
    s_FramePaused = 1U;

    for (i = 0U; i < FFT_SIZE; i++)
    {
        s_SnapUi[i] = g_AD_Ui[i];
        s_SnapUo[i] = g_AD_Uo[i];
    }

    if (snap_ui != 0)
    {
        *snap_ui = s_SnapUi;
    }

    if (snap_uo != 0)
    {
        *snap_uo = s_SnapUo;
    }
}

void ADC_Dual_EndProcess(void)
{
    s_FramePaused = 0U;
    g_ADC_FrameReady = 0U;

    DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);
    DMA_ITConfig(DMA2_Stream0, DMA_IT_TC, ENABLE);

    TIM_SetCounter(TIM2, 0);
    TIM_Cmd(TIM2, ENABLE);
}

void DMA2_Stream0_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA2_Stream0, DMA_IT_TCIF0) != RESET)
    {
        DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);

        /* 整帧边界停表；上一帧未处理完则丢弃新 TC，避免覆盖 g_AD_Ui */
        if ((s_FramePaused == 0U) && (g_ADC_FrameReady == 0U))
        {
            TIM_Cmd(TIM2, DISABLE);
            ADC_Dual_UnpackFrame();
            g_ADC_FrameReady = 1U;
        }
    }
}
