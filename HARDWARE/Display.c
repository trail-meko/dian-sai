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

static void Display_FormatFreqKhz(char *buf, uint16_t hz)
{
    uint16_t whole = hz / 1000U;
    uint16_t frac = (hz % 1000U) / 100U;

    buf[0] = (char)('0' + (whole % 10U));
    buf[1] = '.';
    buf[2] = (char)('0' + (frac % 10U));
    buf[3] = 'k';
    buf[4] = 'H';
    buf[5] = 'z';
    buf[6] = '\0';
}

static void Display_FormatUi(char *buf, float ui_v)
{
    if (ui_v < 1.0f)
    {
        uint32_t tenths_mv = (uint32_t)(ui_v * 10000.0f + 0.5f);
        uint32_t whole = tenths_mv / 10U;
        uint32_t frac = tenths_mv % 10U;

        buf[0] = 'U';
        buf[1] = 'i';
        buf[2] = ':';
        buf[3] = (char)('0' + (whole / 100U) % 10U);
        buf[4] = (char)('0' + (whole / 10U) % 10U);
        buf[5] = (char)('0' + whole % 10U);
        buf[6] = '.';
        buf[7] = (char)('0' + frac);
        buf[8] = 'm';
        buf[9] = 'V';
        buf[10] = '\0';
    }
    else
    {
        uint32_t mv = (uint32_t)(ui_v * 1000.0f + 0.5f);

        buf[0] = 'U';
        buf[1] = 'i';
        buf[2] = ':';
        buf[3] = (char)('0' + (mv / 1000U) % 10U);
        buf[4] = '.';
        buf[5] = (char)('0' + (mv / 100U) % 10U);
        buf[6] = (char)('0' + (mv / 10U) % 10U);
        buf[7] = (char)('0' + mv % 10U);
        buf[8] = 'V';
        buf[9] = '\0';
    }
}

static void Display_FormatVolts3(char *buf, const char *prefix, float v)
{
    uint32_t mv = (uint32_t)(v * 1000.0f + 0.5f);
    uint8_t i = 0U;
    uint8_t j;

    for (j = 0U; prefix[j] != '\0'; j++)
    {
        buf[i++] = prefix[j];
    }
    buf[i++] = (char)('0' + (mv / 1000U) % 10U);
    buf[i++] = '.';
    buf[i++] = (char)('0' + (mv / 100U) % 10U);
    buf[i++] = (char)('0' + (mv / 10U) % 10U);
    buf[i++] = (char)('0' + mv % 10U);
    buf[i++] = 'V';
    buf[i] = '\0';
}

static void Display_FormatAv(char *buf, float av)
{
    int32_t centi = (int32_t)(av * 100.0f + ((av >= 0.0f) ? 0.5f : -0.5f));
    uint32_t abs_centi;
    uint8_t i = 0U;

    buf[i++] = 'A';
    buf[i++] = 'v';
    buf[i++] = ':';
    if (centi < 0)
    {
        abs_centi = (uint32_t)(-centi);
        buf[i++] = '-';
    }
    else
    {
        abs_centi = (uint32_t)centi;
    }
    /* XX.XX：原先仅取 (centi/100)%10，≥10 的增益会丢掉十位（如 14.96→4.96） */
    buf[i++] = (char)('0' + (abs_centi / 1000U) % 10U);
    buf[i++] = (char)('0' + (abs_centi / 100U) % 10U);
    buf[i++] = '.';
    buf[i++] = (char)('0' + (abs_centi / 10U) % 10U);
    buf[i++] = (char)('0' + abs_centi % 10U);
    buf[i] = '\0';
}

static void Display_FormatPhase(char *buf, float deg)
{
    int32_t deci = (int32_t)(deg * 10.0f + ((deg >= 0.0f) ? 0.5f : -0.5f));
    uint32_t abs_deci;
    uint8_t i = 0U;

    buf[i++] = 'P';
    buf[i++] = 'h';
    buf[i++] = ':';
    if (deci < 0)
    {
        abs_deci = (uint32_t)(-deci);
        buf[i++] = '-';
    }
    else
    {
        abs_deci = (uint32_t)deci;
    }
    /* XXX.X：原先仅 (deci/100)%10，≥100° 时百位丢失（180°→80°，100°→00°） */
    buf[i++] = (char)('0' + (abs_deci / 1000U) % 10U);
    buf[i++] = (char)('0' + (abs_deci / 100U) % 10U);
    buf[i++] = (char)('0' + (abs_deci / 10U) % 10U);
    buf[i++] = '.';
    buf[i++] = (char)('0' + abs_deci % 10U);
    buf[i++] = 'd';
    buf[i++] = 'e';
    buf[i++] = 'g';
    buf[i] = '\0';
}

static void Display_FormatLine8(char *buf, const MeasureResult_t *m)
{
    if (m->fault_flags != 0U)
    {
        buf[0] = 'F';
        buf[1] = 'L';
        buf[2] = 'T';
        buf[3] = ':';
        buf[4] = '0';
        buf[5] = 'x';
        buf[6] = (char)(((m->fault_flags >> 4) & 0x0FU) <= 9U ?
                        ('0' + ((m->fault_flags >> 4) & 0x0FU)) :
                        ('A' + ((m->fault_flags >> 4) & 0x0FU) - 10U));
        buf[7] = (char)((m->fault_flags & 0x0FU) <= 9U ?
                        ('0' + (m->fault_flags & 0x0FU)) :
                        ('A' + (m->fault_flags & 0x0FU) - 10U));
        buf[8] = '\0';
    }
    else if (m->pid_saturated != 0U)
    {
        (void)strcpy(buf, "SAT!");
    }
    else if (m->cfg_state == CFG_CHANGED)
    {
        (void)strcpy(buf, "CFG:CHANGED");
    }
    else
    {
        (void)strcpy(buf, "CFG:STABLE");
    }
}

static void Display_FormatHarmonic(char *buf, float h1, float h3, float h5, float phase_deg)
{
    uint8_t i = 0U;

    buf[i++] = 'H';
    buf[i++] = ':';

    /* h1 — 基波 RMS */
    {
        int32_t h1_mv = (int32_t)(h1 * 1000.0f + 0.5f);

        if (h1_mv >= 1000)
        {
            /* >= 1V，显示 V.x.xxx */
            buf[i++] = (char)('0' + (h1_mv / 1000U) % 10U);
            buf[i++] = '.';
            buf[i++] = (char)('0' + (h1_mv / 100U) % 10U);
            buf[i++] = (char)('0' + (h1_mv / 10U) % 10U);
            buf[i++] = (char)('0' + h1_mv % 10U);
        }
        else
        {
            /* 显示整数 mV */
            buf[i++] = (char)('0' + (h1_mv / 100U) % 10U);
            buf[i++] = (char)('0' + (h1_mv / 10U) % 10U);
            buf[i++] = (char)('0' + h1_mv % 10U);
        }
    }

    /* h3 — 3次谐波 RMS (mV) */
    {
        int32_t h3_mv = (int32_t)(h3 * 1000.0f + 0.5f);

        buf[i++] = '/';
        if (h3_mv >= 100)
        {
            buf[i++] = (char)('0' + (h3_mv / 100U) % 10U);
            buf[i++] = (char)('0' + (h3_mv / 10U) % 10U);
            buf[i++] = '.';
            buf[i++] = (char)('0' + h3_mv % 10U);
        }
        else if (h3_mv >= 10)
        {
            buf[i++] = (char)('0' + (h3_mv / 10U) % 10U);
            buf[i++] = '.';
            buf[i++] = (char)('0' + h3_mv % 10U);
        }
        else
        {
            buf[i++] = (char)('0' + h3_mv % 10U);
            buf[i++] = '.';
            buf[i++] = '0';
        }
    }

    /* h5 — 5次谐波 RMS (mV) */
    {
        int32_t h5_mv = (int32_t)(h5 * 1000.0f + 0.5f);

        buf[i++] = '/';
        if (h5_mv >= 100)
        {
            buf[i++] = (char)('0' + (h5_mv / 100U) % 10U);
            buf[i++] = (char)('0' + (h5_mv / 10U) % 10U);
            buf[i++] = '.';
            buf[i++] = (char)('0' + h5_mv % 10U);
        }
        else if (h5_mv >= 10)
        {
            buf[i++] = (char)('0' + (h5_mv / 10U) % 10U);
            buf[i++] = '.';
            buf[i++] = (char)('0' + h5_mv % 10U);
        }
        else
        {
            buf[i++] = (char)('0' + h5_mv % 10U);
            buf[i++] = '.';
            buf[i++] = '0';
        }
    }

    /* 相位 (四舍五入到整数度) */
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
            uint32_t whole = (abs_deci + 5U) / 10U;  /* deci → 四舍五入到整数度 */

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

static void Display_FormatFreqLine(char *buf, float meas_freq_hz, uint8_t freq_ok)
{
    uint16_t hz = (uint16_t)(meas_freq_hz + 0.5f);
    uint8_t i = 0U;

    buf[i++] = 'f';
    buf[i++] = ':';
    if (hz >= 10000U)
    {
        buf[i++] = (char)('0' + (hz / 10000U) % 10U);
    }
    buf[i++] = (char)('0' + (hz / 1000U) % 10U);
    buf[i++] = (char)('0' + (hz / 100U) % 10U);
    buf[i++] = (char)('0' + (hz / 10U) % 10U);
    buf[i++] = (char)('0' + hz % 10U);
    buf[i++] = 'H';
    buf[i++] = 'z';
    buf[i++] = ' ';
    if (freq_ok != 0U)
    {
        buf[i++] = 'O';
        buf[i++] = 'K';
    }
    else
    {
        buf[i++] = 'E';
        buf[i++] = 'R';
        buf[i++] = 'R';
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

    /* Line 1: I = input RMS voltage */
    {
        line[0] = 'I';
        line[1] = ':';
        if (snap.ui_rms_v >= 1.0f)
        {
            uint32_t mv = (uint32_t)(snap.ui_rms_v * 1000.0f + 0.5f);

            line[2] = (char)('0' + (mv / 1000U) % 10U);
            line[3] = '.';
            line[4] = (char)('0' + (mv / 100U) % 10U);
            line[5] = (char)('0' + (mv / 10U) % 10U);
            line[6] = (char)('0' + mv % 10U);
            line[7] = 'V';
            line[8] = '\0';
        }
        else
        {
            uint32_t tenths_mv = (uint32_t)(snap.ui_rms_v * 10000.0f + 0.5f);
            uint32_t whole = tenths_mv / 10U;
            uint32_t frac = tenths_mv % 10U;

            line[2] = (char)('0' + (whole / 100U) % 10U);
            line[3] = (char)('0' + (whole / 10U) % 10U);
            line[4] = (char)('0' + whole % 10U);
            line[5] = '.';
            line[6] = (char)('0' + frac);
            line[7] = 'm';
            line[8] = 'V';
            line[9] = '\0';
        }
        OLED_ShowString(0, y, line, OLED_6X8);
        y = (uint8_t)(y + DISPLAY_LINE_H);
    }

    /* Line 2: V = output RMS voltage */
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

    /* Line 3: harmonics + phase */
    if (snap.fault_flags != 0U)
    {
        line[0] = 'F'; line[1] = 'L'; line[2] = 'T'; line[3] = ':';
        line[4] = (char)(((snap.fault_flags >> 4) & 0x0FU) <= 9U ?
                         ('0' + ((snap.fault_flags >> 4) & 0x0FU)) :
                         ('A' + ((snap.fault_flags >> 4) & 0x0FU) - 10U));
        line[5] = (char)((snap.fault_flags & 0x0FU) <= 9U ?
                         ('0' + (snap.fault_flags & 0x0FU)) :
                         ('A' + (snap.fault_flags & 0x0FU) - 10U));
        line[6] = '\0';
    }
    else if (snap.fft_fail_code != 0U && snap.meas_freq_hz < 1.0f)
    {
        (void)strcpy(line, "SIG LOSS");
    }
    else
    {
        Display_FormatHarmonic(line, snap.h1_v, snap.h3_v, snap.h5_v, snap.phase_deg);
    }
    OLED_ShowString(0, y, line, OLED_6X8);
    y = (uint8_t)(y + DISPLAY_LINE_H);

    /* Line 4: frequency */
    Display_FormatFreqLine(line, snap.meas_freq_hz, snap.freq_ok);
    OLED_ShowString(0, y, line, OLED_6X8);

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
