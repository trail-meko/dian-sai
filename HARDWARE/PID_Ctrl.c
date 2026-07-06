#include "PID_Ctrl.h"
#include "BoardConfig.h"
#include "DAC_Wave.h"
#include <math.h>

static float s_Integral;
static float s_LastError;

static float PID_ClampF(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

static void PID_SelectGains(float abs_err, float *kp, float *ki, float *kd)
{
    if (abs_err > PID_SEG_THRESH_MID_V)
    {
        *kp = PID_KP_COARSE;
        *ki = PID_KI_COARSE;
        *kd = PID_KD_COARSE;
    }
    else if (abs_err > PID_SEG_THRESH_FINE_V)
    {
        *kp = PID_KP_MID;
        *ki = PID_KI_MID;
        *kd = PID_KD_MID;
    }
    else
    {
        *kp = PID_KP_FINE;
        *ki = PID_KI_FINE;
        *kd = PID_KD_FINE;
    }
}

void PID_Ctrl_Reset(void)
{
    s_Integral = 0.0f;
    s_LastError = 0.0f;
}

uint8_t PID_Ctrl_Update(float uo_rms_v, uint16_t *gain_scale_q15)
{
    float error;
    float deriv;
    float out;
    float scale;
    float kp;
    float ki;
    float kd;
    float abs_err;
    uint16_t q15;
    uint8_t saturated = 0U;

    error = TARGET_UO_RMS_V - uo_rms_v;
    abs_err = fabsf(error);

    s_Integral += error;
    s_Integral = PID_ClampF(s_Integral, -PID_INTEGRAL_LIMIT, PID_INTEGRAL_LIMIT);

    deriv = error - s_LastError;
    s_LastError = error;

    PID_SelectGains(abs_err, &kp, &ki, &kd);
    out = kp * error + ki * s_Integral + kd * deriv;

    scale = (float)(*gain_scale_q15) / 32768.0f + out;
    scale = PID_ClampF(scale,
                       (float)GAIN_SCALE_MIN_Q15 / 32768.0f,
                       (float)GAIN_SCALE_MAX_Q15 / 32768.0f);

    q15 = (uint16_t)(scale * 32768.0f);
    if (q15 < GAIN_SCALE_MIN_Q15)
    {
        q15 = GAIN_SCALE_MIN_Q15;
    }
    if (q15 > GAIN_SCALE_MAX_Q15)
    {
        q15 = GAIN_SCALE_MAX_Q15;
    }

    if (q15 == GAIN_SCALE_MIN_Q15 || q15 == GAIN_SCALE_MAX_Q15)
    {
        saturated = 1U;
    }

    *gain_scale_q15 = q15;
    DAC_Wave_SetGainScaleQ15(q15);

    return saturated;
}
