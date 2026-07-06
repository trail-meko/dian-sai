#include "stm32f4xx.h"
#include "BoardConfig.h"
#include "Display.h"
#include "App_Main.h"
#include "OLED.h"
#include "DAC_Wave.h"
#include <string.h>

#define DISPLAY_LINE_H        8U

static volatile uint8_t s_RefreshEnabled = 1U;
static volatile uint8_t s_DisplayDirty;
static volatile uint8_t s_DisplayBusy;

/* ── 单行谐波格式化： "1: xxx P:xxxd" 或 "3: xxx" ── */
static void Display_FormatMvLine(char *buf, char prefix, float v, int8_t show_phase, float phase_deg)
{
    uint8_t i = 0U;
    int32_t uv = (int32_t)(v * 1000000.0f + 0.5f);
    int32_t mv_whole = uv / 1000;
    int32_t mv_frac = (uv / 100) % 10;

    buf[i++] = prefix;
    buf[i++] = ':';
    buf[i++] = ' ';

    if (mv_whole >= 1000)
    {
        /* >= 1V */
        buf[i++] = (char)('0' + (mv_whole / 1000) % 10);
        buf[i++] = '.';
        buf[i++] = (char)('0' + (mv_whole / 100) % 10);
        buf[i++] = (char)('0' + (mv_whole / 10) % 10);
        buf[i++] = 'V';
    }
    else
    {
        buf[i++] = (char)('0' + (mv_whole / 100) % 10);
        buf[i++] = (char)('0' + (mv_whole / 10) % 10);
        buf[i++] = (char)('0' + mv_whole % 10);
        buf[i++] = '.';
        buf[i++] = (char)('0' + mv_frac);
        buf[i++] = 'm';
        buf[i++] = 'V';
    }

    if (show_phase != 0)
    {
        int32_t deci = (int32_t)(phase_deg * 10.0f + ((phase_deg >= 0.0f) ? 0.5f : -0.5f));
        uint32_t abs_deci;

        buf[i++] = ' ';
        if (deci < 0)
        {
            abs_deci = (uint32_t)(-deci);
            buf[i++] = '-';
        }
        else
        {
            abs_deci = (uint32_t)deci;
        }
        {
            uint32_t whole = (abs_deci + 5U) / 10U;

            if (whole >= 100U)
            {
                buf[i++] = (char)('0' + (whole / 100U) % 10U);
            }
            buf[i++] = (char)('0' + (whole / 10U) % 10U);
            buf[i++] = (char)('0' + whole % 10U);
        }
        buf[i++] = 'd';
    }

    buf[i] = '\0';
}

static void Display_Refresh(void)
{
    char line[24];
    uint8_t y;
    MeasureResult_t snap;
    uint32_t primask;

    if (s_RefreshEnabled == 0U)
    {
        return;
    }

    s_DisplayBusy = 1U;

    primask = __get_PRIMASK();
    __disable_irq();
    snap = g_MeasureResult;
    __set_PRIMASK(primask);

    OLED_Clear();
    y = 0U;

    /* Line 1: I = input RMS (显示 mA，数值=电压值×1000视为mA) */
    {
        uint32_t tenths_ma = (uint32_t)(snap.ui_rms_v * 10000.0f + 0.5f);
        uint32_t whole = tenths_ma / 10U;
        uint32_t frac = tenths_ma % 10U;

        line[0] = 'I';
        line[1] = ':';
        line[2] = (char)('0' + (whole / 100U) % 10U);
        line[3] = (char)('0' + (whole / 10U) % 10U);
        line[4] = (char)('0' + whole % 10U);
        line[5] = '.';
        line[6] = (char)('0' + frac);
        line[7] = 'm';
        line[8] = 'A';
        line[9] = '\0';
        OLED_ShowString(0, y, line, OLED_6X8);
        y = (uint8_t)(y + DISPLAY_LINE_H);
    }

    /* Line 2: V = output RMS */
    {
        uint32_t mv = (uint32_t)(snap.uo_rms_v * 1000.0f + 0.5f);

        line[0] = 'V';
        line[1] = ':';
        line[2] = (char)('0' + (mv / 1000U) % 10U);
        line[3] = '.';
        line[4] = (char)('0' + (mv / 100U) % 10U);
        line[5] = (char)('0' + (mv / 10U) % 10U);
        line[6] = (char)('0' + mv % 10U);
        line[7] = 'V';
        line[8] = '\0';
        OLED_ShowString(0, y, line, OLED_6X8);
        y = (uint8_t)(y + DISPLAY_LINE_H);
    }

    /* Lines 3-5: 谐波三行 */
    if (snap.fault_flags != 0U)
    {
        line[0] = 'F'; line[1] = 'L'; line[2] = 'T'; line[3] = ':';
        line[4] = (char)(((snap.fault_flags >> 4) & 0x0FU) <= 9U ?
                         ('0' + ((snap.fault_flags >> 4) & 0x0FU)) :
                         ('A' + ((snap.fault_flags >> 4) & 0x0FU) - 10U));
        line[5] = (char)((snap.fault_flags & 0x0FU) <= 9U ?
                         ('0' + (snap.fault_flags & 0x0FU)) :
                         ('A' + ((snap.fault_flags & 0x0FU) - 10U));
        line[6] = '\0';
        OLED_ShowString(0, y, line, OLED_6X8);
        y = (uint8_t)(y + DISPLAY_LINE_H);
    }
    else if (snap.fft_fail_code != 0U && snap.meas_freq_hz < 1.0f)
    {
        (void)strcpy(line, "SIG LOSS");
        OLED_ShowString(0, y, line, OLED_6X8);
        y = (uint8_t)(y + DISPLAY_LINE_H);
    }
    else
    {
        Display_FormatMvLine(line, '1', snap.h1_v, 1, snap.phase_deg);
        OLED_ShowString(0, y, line, OLED_6X8);
        y = (uint8_t)(y + DISPLAY_LINE_H);

        Display_FormatMvLine(line, '3', snap.h3_v, 0, 0.0f);
        OLED_ShowString(0, y, line, OLED_6X8);
        y = (uint8_t)(y + DISPLAY_LINE_H);

        Display_FormatMvLine(line, '5', snap.h5_v, 0, 0.0f);
        OLED_ShowString(0, y, line, OLED_6X8);
        y = (uint8_t)(y + DISPLAY_LINE_H);
    }

    /* Line 6: frequency (去掉 OK/ERR) */
    {
        uint16_t hz = (uint16_t)(snap.meas_freq_hz + 0.5f);
        uint8_t i = 0U;

        line[i++] = 'f';
        line[i++] = ':';
        if (hz >= 10000U)
        {
            line[i++] = (char)('0' + (hz / 10000U) % 10U);
        }
        line[i++] = (char)('0' + (hz / 1000U) % 10U);
        line[i++] = (char)('0' + (hz / 100U) % 10U);
        line[i++] = (char)('0' + (hz / 10U) % 10U);
        line[i++] = (char)('0' + hz % 10U);
        line[i++] = 'H';
        line[i++] = 'z';
        line[i] = '\0';
        OLED_ShowString(0, y, line, OLED_6X8);
    }

    OLED_Update();

    s_DisplayBusy = 0U;
}

static void Display_StartTim4(void)
{
    TIM_TimeBaseInitTypeDef tim;
    NVIC_InitTypeDef nvic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Prescaler = 8399U;
    tim.TIM_Period = 1999U;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &tim);

    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel = TIM4_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = IRQ_PRIO_TIM4;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM4, ENABLE);
}

void Display_Init(void)
{
    s_DisplayDirty = 0U;
    OLED_Init();
    Display_Refresh();
    Display_StartTim4();
}

void Display_SetRefreshEnable(uint8_t enable)
{
    s_RefreshEnabled = enable;

    if (enable != 0U)
    {
        Display_ServicePending();
    }
}

void Display_OnTim4Irq(void)
{
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
        s_DisplayDirty = 1U;
    }
}

void Display_ServicePending(void)
{
    if (s_RefreshEnabled == 0U)
    {
        return;
    }

    if (s_DisplayDirty == 0U)
    {
        return;
    }

    if (s_DisplayBusy != 0U)
    {
        return;
    }

    s_DisplayDirty = 0U;
    Display_Refresh();
}

void Display_Invalidate(void)
{
    s_DisplayDirty = 1U;
}

uint8_t Display_IsBusy(void)
{
    return s_DisplayBusy;
}

void Display_Poll(void)
{
    /* 刷新由 TIM4 中断驱动；保留 API 供兼容，主循环无需调用 */
}
