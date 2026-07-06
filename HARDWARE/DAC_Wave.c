#include "stm32f4xx.h"
#include "BoardConfig.h"
#include "DAC_Wave.h"

#include "DAC_SinTables.inc"
#include "DAC_TriTables.inc"

static int16_t s_BaseWave[WAVE_TABLE_MAX_POINTS];
static uint16_t s_DacDmaBuf[WAVE_TABLE_MAX_POINTS];
static uint8_t s_FreqIndex;
static uint8_t s_WaveType;
static uint16_t s_GainScaleQ15;
static uint16_t s_ActivePoints;
static uint8_t s_HwReady;

static const int16_t *DAC_Wave_LookupSinTable(uint16_t n_points)
{
    switch (n_points)
    {
    case 242U: return DAC_Sin_N242;
    case 249U: return DAC_Sin_N249;
    case 250U: return DAC_Sin_N250;
    case 251U: return DAC_Sin_N251;
    case 252U: return DAC_Sin_N252;
    case 254U: return DAC_Sin_N254;
    case 256U: return DAC_Sin_N256;
    default:   return DAC_Sin_N250;
    }
}

static const int16_t *DAC_Wave_LookupTriTable(uint16_t n_points)
{
    switch (n_points)
    {
    case 242U: return DAC_Tri_N242;
    case 249U: return DAC_Tri_N249;
    case 250U: return DAC_Tri_N250;
    case 251U: return DAC_Tri_N251;
    case 252U: return DAC_Tri_N252;
    case 254U: return DAC_Tri_N254;
    case 256U: return DAC_Tri_N256;
    default:   return DAC_Tri_N250;
    }
}

static void DAC_Wave_ClampCode(uint16_t *code)
{
    if (*code > 4095U)
    {
        *code = 4095U;
    }
}

static void DAC_Wave_CopyBaseWave(uint16_t n_points)
{
    const int16_t *src;
    uint16_t i;

    if (s_WaveType == WAVE_TRIANGLE)
    {
        src = DAC_Wave_LookupTriTable(n_points);
    }
    else
    {
        src = DAC_Wave_LookupSinTable(n_points);
    }

    for (i = 0U; i < n_points; i++)
    {
        s_BaseWave[i] = src[i];
    }
}

static void DAC_Wave_FillBuffer(uint16_t n_points)
{
    uint16_t i;

    DAC_Wave_CopyBaseWave(n_points);

    for (i = 0U; i < n_points; i++)
    {
        int32_t scaled = ((int32_t)s_BaseWave[i] * (int32_t)s_GainScaleQ15) >> 15;
        uint16_t code = (uint16_t)((int32_t)DAC_MID_CODE + scaled);

        DAC_Wave_ClampCode(&code);
        s_DacDmaBuf[i] = code;
    }
}

static void DAC_Wave_OutputMidpoint(void)
{
    DAC_SetChannel1Data(DAC_Align_12b_R, DAC_MID_CODE);
}

static void DAC_Wave_StopHw(void)
{
    TIM_Cmd(TIM6, DISABLE);
    DMA_Cmd(DAC_DMA_STREAM, DISABLE);
    DAC_Wave_OutputMidpoint();
}

static void DAC_Wave_ConfigTim6(uint16_t psc, uint16_t arr)
{
    TIM_TimeBaseInitTypeDef tim;

    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Prescaler = psc;
    tim.TIM_Period = arr;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM6, &tim);

    TIM_SelectOutputTrigger(TIM6, TIM_TRGOSource_Update);
}

static void DAC_Wave_ConfigDma(uint16_t n_points)
{
    DMA_InitTypeDef dma;

    DMA_DeInit(DAC_DMA_STREAM);

    DMA_StructInit(&dma);
    dma.DMA_Channel = DAC_DMA_CHANNEL;
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->DHR12R1;
    dma.DMA_Memory0BaseAddr = (uint32_t)s_DacDmaBuf;
    dma.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    dma.DMA_BufferSize = n_points;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_VeryHigh;
    dma.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_Init(DAC_DMA_STREAM, &dma);
}

static void DAC_Wave_StartHw(void)
{
    TIM_SetCounter(TIM6, 0);
    DAC_SetChannel1Data(DAC_Align_12b_R, s_DacDmaBuf[0]);
    TIM_Cmd(TIM6, ENABLE);
    DMA_Cmd(DAC_DMA_STREAM, ENABLE);
}

static void DAC_Wave_ApplyGear(uint8_t index)
{
    const DacFreqGear_t *gear;

    if (index >= FREQ_GEAR_COUNT)
    {
        index = 0U;
    }

    gear = &g_DacFreqGear[index];
    s_FreqIndex = index;
    s_ActivePoints = gear->n_points;

    DAC_Wave_StopHw();
    DAC_Wave_FillBuffer(s_ActivePoints);
    DAC_Wave_ConfigTim6(gear->psc, gear->arr);
    DAC_Wave_ConfigDma(s_ActivePoints);
    DAC_Wave_StartHw();
}

static void DAC_Wave_InitHardware(void)
{
    GPIO_InitTypeDef gpio;
    DAC_InitTypeDef dac;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC | RCC_APB1Periph_TIM6, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

    gpio.GPIO_Pin = PIN_DAC_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AN;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(PIN_DAC_GPIO, &gpio);

    DAC_StructInit(&dac);
    dac.DAC_Trigger = DAC_Trigger_T6_TRGO;
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_ACTIVE_CHANNEL, &dac);
    DAC_Cmd(DAC_ACTIVE_CHANNEL, ENABLE);
    DAC_DMACmd(DAC_ACTIVE_CHANNEL, ENABLE);

    DAC_Wave_OutputMidpoint();
    s_HwReady = 1U;
}

void DAC_Wave_SetGainScaleQ15(uint16_t scale)
{
    if (scale < GAIN_SCALE_MIN_Q15)
    {
        scale = GAIN_SCALE_MIN_Q15;
    }
    else if (scale > GAIN_SCALE_MAX_Q15)
    {
        scale = GAIN_SCALE_MAX_Q15;
    }

    s_GainScaleQ15 = scale;

    if (s_HwReady)
    {
        DAC_Wave_ApplyGear(s_FreqIndex);
    }
}

void DAC_Wave_ApplyManualGain(void)
{
    if (s_WaveType == WAVE_TRIANGLE)
    {
        DAC_Wave_SetGainScaleQ15(MANUAL_GAIN_SCALE_TRIANGLE_Q15);
    }
    else
    {
        DAC_Wave_SetGainScaleQ15(MANUAL_GAIN_SCALE_Q15);
    }
}

uint16_t DAC_Wave_GetGainScaleQ15(void)
{
    return s_GainScaleQ15;
}

void DAC_Wave_SetWaveType(uint8_t wave)
{
    s_WaveType = (wave == WAVE_TRIANGLE) ? WAVE_TRIANGLE : WAVE_SINE;

    if (s_HwReady)
    {
        DAC_Wave_ApplyGear(s_FreqIndex);
    }
}

void DAC_Wave_SetFrequencyIndex(uint8_t index)
{
    if (index >= FREQ_GEAR_COUNT)
    {
        index = 0U;
    }

    if (s_HwReady)
    {
        DAC_Wave_ApplyGear(index);
    }
    else
    {
        s_FreqIndex = index;
    }
}

void DAC_Wave_Init(void)
{
    s_FreqIndex = 0U;
    s_WaveType = WAVE_SINE;
    s_GainScaleQ15 = MANUAL_GAIN_SCALE_Q15;
    s_ActivePoints = g_DacFreqGear[0].n_points;
    s_HwReady = 0U;

    DAC_Wave_InitHardware();
    DAC_Wave_ApplyGear(0U);
}
