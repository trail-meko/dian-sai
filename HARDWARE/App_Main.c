#include <math.h>
#include "stm32f4xx.h"
#include "BoardConfig.h"
#include "App_Main.h"
#include "ADC_Dual.h"
#include "FFT_Analyze.h"
#include "Display.h"
#include "DAC_Wave.h"
#include "PID_Ctrl.h"

AppParams_t g_AppParams;
MeasureResult_t g_MeasureResult;

static MeasureResult_t s_LastValidMeasure;
static uint8_t s_HasValidMeasure;
static volatile uint8_t s_AppProcessing;
static float s_AvStable;
static uint8_t s_AvStableValid;
static uint8_t s_CfgChangeCount;
static uint8_t s_PidDiv;
static uint8_t s_FaultLedTick;
static uint8_t s_FaultLedState;

#define FAULT_LED_HALF_PERIOD_MS  500U

static void App_CopyFftToMeasure(const FFTMeasure_t *fft, MeasureResult_t *dst)
{
    dst->ui_rms_v = fft->ui_rms_v;
    dst->uo_rms_v = fft->uo_rms_v;
    dst->av_gain = fft->av_gain;
    dst->phase_deg = fft->phase_deg;
    dst->meas_freq_hz = fft->meas_freq_hz;
    dst->freq_ok = fft->freq_ok;
    dst->adc_ui_min = fft->adc_ui_min;
    dst->adc_ui_max = fft->adc_ui_max;
    dst->adc_uo_min = fft->adc_uo_min;
    dst->adc_uo_max = fft->adc_uo_max;
    dst->target_out_v = (g_AppParams.mode == WORK_MODE_AUTO) ?
                        TARGET_UO_RMS_V : TARGET_UI_RMS_V;
    dst->pid_saturated = 0U;
}

static void App_ResetCfgDetect(void)
{
    s_AvStable = 0.0f;
    s_AvStableValid = 0U;
    s_CfgChangeCount = 0U;
    g_MeasureResult.cfg_state = CFG_STABLE;
}

static void App_UpdateCfgDetect(float av)
{
    float thresh;

    if (s_AvStableValid == 0U)
    {
        s_AvStable = av;
        s_AvStableValid = 1U;
        g_MeasureResult.cfg_state = CFG_STABLE;
        return;
    }

    thresh = fabsf(s_AvStable) * CFG_CHANGE_THRESH + 0.01f;

    if (fabsf(av - s_AvStable) > thresh)
    {
        s_CfgChangeCount++;

        if (s_CfgChangeCount >= CFG_CHANGE_COUNT)
        {
            g_MeasureResult.cfg_state = CFG_CHANGED;
            s_AvStable = av;
            s_CfgChangeCount = 0U;
            PID_Ctrl_Reset();
        }
    }
    else
    {
        s_CfgChangeCount = 0U;
        g_MeasureResult.cfg_state = CFG_STABLE;
        s_AvStable = s_AvStable * 0.9f + av * 0.1f;
    }
}

static void App_SetFftFault(uint8_t fail_code)
{
    g_MeasureResult.fft_fail_code = fail_code;

    if (fail_code == 1U || fail_code == 2U)
    {
        g_MeasureResult.fault_flags |= FAULT_ADC;
    }

    if (fail_code == 3U || fail_code == 4U)
    {
        g_MeasureResult.fault_flags |= FAULT_FFT;
    }
}

static uint8_t App_IsBadFrame(const FFTMeasure_t *fft)
{
    if (fft->ui_rms_v < UI_RMS_LOSS_V)
    {
        g_MeasureResult.fault_flags |= FAULT_SIGNAL_LOSS;
        return 1U;
    }

    if (s_HasValidMeasure != 0U)
    {
        if (fft->ui_rms_v < s_LastValidMeasure.ui_rms_v * BAD_FRAME_RATIO)
        {
            return 1U;
        }

        if (fft->uo_rms_v < s_LastValidMeasure.uo_rms_v * BAD_FRAME_RATIO)
        {
            return 1U;
        }
    }

    return 0U;
}

void App_Init(void)
{
    g_AppParams.mode = WORK_MODE_MANUAL;
    g_AppParams.wave = WAVE_SINE;
    g_AppParams.freq_index = 0U;
    g_AppParams.target_freq_hz = g_DacFreqGear[0].target_hz;

    g_MeasureResult.target_out_v = TARGET_UI_RMS_V;
    g_MeasureResult.fault_flags = 0U;
    g_MeasureResult.fft_fail_code = 0U;

    s_HasValidMeasure = 0U;
    s_PidDiv = 0U;
    s_FaultLedTick = 0U;
    s_FaultLedState = 0U;
    App_ResetCfgDetect();
    PID_Ctrl_Reset();
    FFT_Analyze_ResetAvg();
}

void App_ScheduleBackgroundProcess(void)
{
}

void App_BackgroundProcess(void)
{
    uint16_t *snap_ui;
    uint16_t *snap_uo;
    FFTMeasure_t fft;
    uint8_t fail_code;

    if (g_ADC_FrameReady == 0U)
    {
        return;
    }

    if (s_AppProcessing != 0U)
    {
        return;
    }

    s_AppProcessing = 1U;

#if (FEATURE_DISPLAY_ENABLE != 0U)
    Display_SetRefreshEnable(0U);
#endif
    ADC_Dual_BeginProcess(&snap_ui, &snap_uo);

    if (FFT_Analyze_Process(snap_ui,
                            snap_uo,
                            g_AppParams.target_freq_hz,
                            &fft,
                            &fail_code) != 0U)
    {
        App_SetFftFault(fail_code);
        ADC_Dual_EndProcess();
#if (FEATURE_DISPLAY_ENABLE != 0U)
        Display_SetRefreshEnable(1U);
#endif
        s_AppProcessing = 0U;
        return;
    }

    if (App_IsBadFrame(&fft) != 0U)
    {
        ADC_Dual_EndProcess();
#if (FEATURE_DISPLAY_ENABLE != 0U)
        Display_SetRefreshEnable(1U);
#endif
        s_AppProcessing = 0U;
        return;
    }

    g_MeasureResult.fault_flags &= (uint8_t)~(FAULT_ADC | FAULT_FFT | FAULT_SIGNAL_LOSS);
    g_MeasureResult.fft_fail_code = 0U;
    App_CopyFftToMeasure(&fft, &g_MeasureResult);
    App_UpdateCfgDetect(g_MeasureResult.av_gain);

    if (g_AppParams.mode == WORK_MODE_AUTO)
    {
        uint16_t gain_q15;

        g_MeasureResult.target_out_v = TARGET_UO_RMS_V;
        s_PidDiv++;

        if (s_PidDiv >= PID_FRAME_DIV)
        {
            s_PidDiv = 0U;
            gain_q15 = DAC_Wave_GetGainScaleQ15();

            if (PID_Ctrl_Update(g_MeasureResult.uo_rms_v, &gain_q15) != 0U)
            {
                g_MeasureResult.fault_flags |= FAULT_PID_SAT;
                g_MeasureResult.pid_saturated = 1U;
            }
            else
            {
                g_MeasureResult.fault_flags &= (uint8_t)~FAULT_PID_SAT;
                g_MeasureResult.pid_saturated = 0U;
            }
        }
    }
    else
    {
        g_MeasureResult.pid_saturated = 0U;
        g_MeasureResult.fault_flags &= (uint8_t)~FAULT_PID_SAT;
        s_PidDiv = 0U;
    }

    s_LastValidMeasure = g_MeasureResult;
    s_HasValidMeasure = 1U;

#if (FEATURE_DISPLAY_ENABLE != 0U)
    Display_Invalidate();
#endif

    ADC_Dual_EndProcess();
#if (FEATURE_DISPLAY_ENABLE != 0U)
    Display_SetRefreshEnable(1U);
#endif
    s_AppProcessing = 0U;
}

void App_OnKeyEvent(uint8_t key_id)
{
    switch (key_id)
    {
    case 1U:
        g_AppParams.freq_index++;
        if (g_AppParams.freq_index >= FREQ_GEAR_COUNT)
        {
            g_AppParams.freq_index = 0U;
        }
        g_AppParams.target_freq_hz = g_DacFreqGear[g_AppParams.freq_index].target_hz;
        DAC_Wave_SetFrequencyIndex(g_AppParams.freq_index);
        App_ResetCfgDetect();
        FFT_Analyze_ResetAvg();
        break;

    case 2U:
        if (g_AppParams.mode == WORK_MODE_MANUAL)
        {
            g_AppParams.mode = WORK_MODE_AUTO;
            g_MeasureResult.target_out_v = TARGET_UO_RMS_V;
            PID_Ctrl_Reset();
            s_PidDiv = 0U;
        }
        else
        {
            g_AppParams.mode = WORK_MODE_MANUAL;
            g_MeasureResult.target_out_v = TARGET_UI_RMS_V;
            g_MeasureResult.pid_saturated = 0U;
            g_MeasureResult.fault_flags &= (uint8_t)~FAULT_PID_SAT;
            DAC_Wave_ApplyManualGain();
            s_PidDiv = 0U;
        }
        App_ResetCfgDetect();
        FFT_Analyze_ResetAvg();
        break;

    case 3U:
        if (g_AppParams.wave == WAVE_SINE)
        {
            g_AppParams.wave = WAVE_TRIANGLE;
        }
        else
        {
            g_AppParams.wave = WAVE_SINE;
        }
        DAC_Wave_SetWaveType(g_AppParams.wave);
        if (g_AppParams.mode == WORK_MODE_MANUAL)
        {
            DAC_Wave_ApplyManualGain();
        }
        App_ResetCfgDetect();
        FFT_Analyze_ResetAvg();
        break;

    default:
        break;
    }
}

void App_CheckFault(void)
{
    if (g_MeasureResult.fault_flags != 0U)
    {
        s_FaultLedTick++;

        if (s_FaultLedTick >= (FAULT_LED_HALF_PERIOD_MS / KEY_SCAN_MS))
        {
            s_FaultLedTick = 0U;
            s_FaultLedState ^= 1U;

            if (s_FaultLedState != 0U)
            {
                GPIO_ResetBits(PIN_FAULT_LED_GPIO, PIN_FAULT_LED_PIN);
            }
            else
            {
                GPIO_SetBits(PIN_FAULT_LED_GPIO, PIN_FAULT_LED_PIN);
            }
        }
    }
    else
    {
        s_FaultLedTick = 0U;
        s_FaultLedState = 0U;
        GPIO_ResetBits(PIN_FAULT_LED_GPIO, PIN_FAULT_LED_PIN);
    }
}
