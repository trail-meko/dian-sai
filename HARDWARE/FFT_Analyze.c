#include "FFT_Analyze.h"
#include "BoardConfig.h"
#include "ADC_Dual.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#include <math.h>

#define FFT_PI              3.14159265358979323846f
#define FFT_MAG_SCALE       32768.0f

static float s_FftBufUi[FFT_SIZE * 2U];
static float s_FftBufUo[FFT_SIZE * 2U];

static float s_AvgUiRms[FFT_AVG_COUNT];
static float s_AvgUoRms[FFT_AVG_COUNT];
static float s_AvgAv[FFT_AVG_COUNT];
static float s_AvgFreq[FFT_AVG_COUNT];
static float s_AvgPhase[FFT_AVG_COUNT];
static uint8_t s_AvgCount;

volatile float g_FFT_DbgUiPeakMag;
volatile uint16_t g_FFT_DbgPeakBin;
volatile uint16_t g_FFT_DbgTargetBin;

static float FFT_NormalizeDeg(float deg)
{
    while (deg > 180.0f)
    {
        deg -= 360.0f;
    }

    while (deg < -180.0f)
    {
        deg += 360.0f;
    }

    return deg;
}

static void FFT_GetMinMax(uint16_t *raw, uint16_t *min_code, uint16_t *max_code)
{
    uint16_t i;
    uint16_t min_v = 4095U;
    uint16_t max_v = 0U;

    for (i = 0U; i < FFT_SIZE; i++)
    {
        if (raw[i] < min_v)
        {
            min_v = raw[i];
        }

        if (raw[i] > max_v)
        {
            max_v = raw[i];
        }
    }

    *min_code = min_v;
    *max_code = max_v;
}

static uint8_t FFT_IsAbnormalFrame(uint16_t min_code, uint16_t max_code)
{
    if (max_code <= 8U && min_code <= 8U)
    {
        return 1U;
    }

    if (min_code >= 4087U && max_code >= 4087U)
    {
        return 1U;
    }

    return 0U;
}

static float FFT_CalcAcRmsV(uint16_t *raw)
{
    uint32_t i;
    float mean = 0.0f;
    float sum_sq = 0.0f;

    for (i = 0U; i < FFT_SIZE; i++)
    {
        mean += (float)raw[i];
    }

    mean /= (float)FFT_SIZE;

    for (i = 0U; i < FFT_SIZE; i++)
    {
        float diff = (float)raw[i] - mean;
        sum_sq += diff * diff;
    }

    return sqrtf(sum_sq / (float)FFT_SIZE) * ADC_VREF / (float)ADC_MAX_CODE;
}

static void FFT_PackAcAutoGain(uint16_t *raw, float mean, float *fft_buf)
{
    uint32_t i;
    float max_abs = 1.0f;
    float gain;

    for (i = 0U; i < FFT_SIZE; i++)
    {
        float ac = (float)raw[i] - mean;
        float abs_ac = fabsf(ac);

        if (abs_ac > max_abs)
        {
            max_abs = abs_ac;
        }
    }

    gain = FFT_AUTO_GAIN_PEAK / max_abs;
    if (gain < 1.0f)
    {
        gain = 1.0f;
    }
    if (gain > 64.0f)
    {
        gain = 64.0f;
    }

    for (i = 0U; i < FFT_SIZE; i++)
    {
        float ac = ((float)raw[i] - mean) * gain;

        fft_buf[2U * i] = ac;
        fft_buf[2U * i + 1U] = 0.0f;
    }
}

static void FFT_RunCfft(float *fft_buf)
{
    arm_cfft_f32(&arm_cfft_sR_f32_len1024, fft_buf, 0, 1);
}

static float FFT_GetBinMagnitude(const float *fft_buf, uint16_t bin)
{
    float re = fft_buf[2U * bin];
    float im = fft_buf[2U * bin + 1U];

    return sqrtf(re * re + im * im) / (float)FFT_SIZE * FFT_MAG_SCALE;
}

static void FFT_GetBinComplex(const float *fft_buf, uint16_t bin, float *re, float *im)
{
    *re = fft_buf[2U * bin];
    *im = fft_buf[2U * bin + 1U];
}

static uint16_t FFT_FindPeakBin(const float *fft_buf, uint16_t target_bin, float *peak_mag)
{
    int32_t k;
    int32_t k_min;
    int32_t k_max;
    int32_t peak_k;
    float peak_mag_v = -1.0f;

    k_min = (int32_t)target_bin - (int32_t)FFT_BIN_SEARCH_RADIUS;
    k_max = (int32_t)target_bin + (int32_t)FFT_BIN_SEARCH_RADIUS;

    if (k_min < 1)
    {
        k_min = 1;
    }

    if (k_max > ((int32_t)FFT_SIZE / 2 - 1))
    {
        k_max = (int32_t)FFT_SIZE / 2 - 1;
    }

    peak_k = k_min;
    for (k = k_min; k <= k_max; k++)
    {
        float mag = FFT_GetBinMagnitude(fft_buf, (uint16_t)k);

        if (mag > peak_mag_v)
        {
            peak_mag_v = mag;
            peak_k = k;
        }
    }

    *peak_mag = peak_mag_v;
    return (uint16_t)peak_k;
}

static float FFT_RefineFrequencyHz(const float *fft_buf, uint16_t target_bin, float sample_rate_hz)
{
    float y1;
    float y2;
    float y3;
    float denom;
    float delta;

    if (sample_rate_hz < 1.0f)
    {
        sample_rate_hz = ADC_Dual_GetSampleRateHz();
    }

    if (target_bin < 1U || target_bin >= (FFT_SIZE / 2U))
    {
        return (float)target_bin * sample_rate_hz / (float)FFT_SIZE;
    }

    y1 = FFT_GetBinMagnitude(fft_buf, (uint16_t)(target_bin - 1U));
    y2 = FFT_GetBinMagnitude(fft_buf, target_bin);
    y3 = FFT_GetBinMagnitude(fft_buf, (uint16_t)(target_bin + 1U));
    denom = y1 - 2.0f * y2 + y3;

    if (fabsf(denom) < 1.0e-6f)
    {
        delta = 0.0f;
    }
    else
    {
        delta = 0.5f * (y1 - y3) / denom;
    }

    return ((float)target_bin + delta) * sample_rate_hz / (float)FFT_SIZE;
}

static float FFT_CalcCrossPhaseDeg(uint16_t bin,
                                   float ui_re,
                                   float ui_im,
                                   float uo_re,
                                   float uo_im,
                                   float meas_freq_hz)
{
    float cross_re;
    float cross_im;
    float phase_deg;
    float delay_corr_deg;

    (void)bin;

    cross_re = uo_re * ui_re + uo_im * ui_im;
    cross_im = uo_im * ui_re - uo_re * ui_im;
    phase_deg = atan2f(cross_im, cross_re) * 180.0f / FFT_PI;
    delay_corr_deg = 360.0f * meas_freq_hz * ADC_INTERCH_DELAY_SEC;

    return FFT_NormalizeDeg(phase_deg - delay_corr_deg - 180.0f);
}

static void FFT_ApplyAverage(float ui_rms,
                             float uo_rms,
                             float av_gain,
                             float meas_freq_hz,
                             float phase_deg,
                             FFTMeasure_t *out)
{
    uint8_t i;

    for (i = (uint8_t)(FFT_AVG_COUNT - 1U); i > 0U; i--)
    {
        s_AvgUiRms[i] = s_AvgUiRms[i - 1U];
        s_AvgUoRms[i] = s_AvgUoRms[i - 1U];
        s_AvgAv[i] = s_AvgAv[i - 1U];
        s_AvgFreq[i] = s_AvgFreq[i - 1U];
        s_AvgPhase[i] = s_AvgPhase[i - 1U];
    }

    s_AvgUiRms[0] = ui_rms;
    s_AvgUoRms[0] = uo_rms;
    s_AvgAv[0] = av_gain;
    s_AvgFreq[0] = meas_freq_hz;
    s_AvgPhase[0] = phase_deg;

    if (s_AvgCount < FFT_AVG_COUNT)
    {
        s_AvgCount++;
    }

    out->ui_rms_v = 0.0f;
    out->uo_rms_v = 0.0f;
    out->meas_freq_hz = 0.0f;

    for (i = 0U; i < s_AvgCount; i++)
    {
        out->ui_rms_v += s_AvgUiRms[i];
        out->uo_rms_v += s_AvgUoRms[i];
        out->meas_freq_hz += s_AvgFreq[i];
    }

    out->ui_rms_v /= (float)s_AvgCount;
    out->uo_rms_v /= (float)s_AvgCount;
    out->meas_freq_hz /= (float)s_AvgCount;

    {
        float sin_sum = 0.0f;
        float cos_sum = 0.0f;

        for (i = 0U; i < s_AvgCount; i++)
        {
            float phase_rad = s_AvgPhase[i] * FFT_PI / 180.0f;
            sin_sum += sinf(phase_rad);
            cos_sum += cosf(phase_rad);
        }

        out->phase_deg = FFT_NormalizeDeg(atan2f(sin_sum, cos_sum) * 180.0f / FFT_PI);
    }

    /* Av 由校准后的平均 Ui/Uo 重算，避免与 OLED 示数脱节 */
    if (out->ui_rms_v > 1.0e-9f)
    {
        float mag = out->uo_rms_v / out->ui_rms_v;

        if (fabsf(out->phase_deg) > 90.0f)
        {
            out->av_gain = -mag;
        }
        else
        {
            out->av_gain = mag;
        }
    }
    else
    {
        out->av_gain = 0.0f;
    }
}

void FFT_Analyze_ResetAvg(void)
{
    s_AvgCount = 0U;
}

uint8_t FFT_Analyze_Process(uint16_t *raw_ui,
                            uint16_t *raw_uo,
                            uint16_t target_freq_hz,
                            FFTMeasure_t *out,
                            uint8_t *fail_code)
{
    float ui_rms;
    float uo_rms;
    float av_gain;
    float meas_freq_hz;
    float phase_deg;
    uint16_t target_bin;
    uint16_t peak_bin;
    float sample_rate_hz;
    float ui_peak_mag;
    float ui_mean;
    float uo_mean;
    float ui_re;
    float ui_im;
    float uo_re;
    float uo_im;

    if (out == 0)
    {
        return 1U;
    }

    if (fail_code != 0)
    {
        *fail_code = 0U;
    }

    FFT_GetMinMax(raw_ui, &out->adc_ui_min, &out->adc_ui_max);
    FFT_GetMinMax(raw_uo, &out->adc_uo_min, &out->adc_uo_max);

    if (FFT_IsAbnormalFrame(out->adc_ui_min, out->adc_ui_max))
    {
        if (fail_code != 0)
        {
            *fail_code = 1U;
        }
        return 1U;
    }

    if (FFT_IsAbnormalFrame(out->adc_uo_min, out->adc_uo_max))
    {
        if (fail_code != 0)
        {
            *fail_code = 2U;
        }
        return 1U;
    }

    ui_rms = FFT_CalcAcRmsV(raw_ui) * UI_RMS_CAL_SCALE;
    uo_rms = FFT_CalcAcRmsV(raw_uo) * UO_RMS_CAL_SCALE;
    sample_rate_hz = ADC_Dual_GetSampleRateHz();

    ui_mean = 0.0f;
    uo_mean = 0.0f;
    {
        uint32_t i;

        for (i = 0U; i < FFT_SIZE; i++)
        {
            ui_mean += (float)raw_ui[i];
            uo_mean += (float)raw_uo[i];
        }
        ui_mean /= (float)FFT_SIZE;
        uo_mean /= (float)FFT_SIZE;
    }

    FFT_PackAcAutoGain(raw_ui, ui_mean, s_FftBufUi);
    FFT_RunCfft(s_FftBufUi);

    target_bin = (uint16_t)(((float)target_freq_hz * (float)FFT_SIZE / sample_rate_hz) + 0.5f);
    if (target_bin < 1U)
    {
        target_bin = 1U;
    }
    if (target_bin >= (FFT_SIZE / 2U))
    {
        target_bin = (uint16_t)(FFT_SIZE / 2U - 1U);
    }

    peak_bin = FFT_FindPeakBin(s_FftBufUi, target_bin, &ui_peak_mag);
    g_FFT_DbgUiPeakMag = ui_peak_mag;
    g_FFT_DbgPeakBin = peak_bin;
    g_FFT_DbgTargetBin = target_bin;

    if (ui_peak_mag < FFT_BIN_MIN_MAG)
    {
        if (fail_code != 0)
        {
            *fail_code = 3U;
        }
        return 1U;
    }

    FFT_PackAcAutoGain(raw_uo, uo_mean, s_FftBufUo);
    FFT_RunCfft(s_FftBufUo);

    if (FFT_GetBinMagnitude(s_FftBufUo, peak_bin) < FFT_BIN_MIN_MAG)
    {
        if (fail_code != 0)
        {
            *fail_code = 4U;
        }
        return 1U;
    }

    meas_freq_hz = FFT_RefineFrequencyHz(s_FftBufUi, peak_bin, sample_rate_hz);
    FFT_GetBinComplex(s_FftBufUi, peak_bin, &ui_re, &ui_im);
    FFT_GetBinComplex(s_FftBufUo, peak_bin, &uo_re, &uo_im);

    phase_deg = FFT_CalcCrossPhaseDeg(peak_bin,
                                      ui_re,
                                      ui_im,
                                      uo_re,
                                      uo_im,
                                      meas_freq_hz);

    if (ui_rms > 1.0e-9f)
    {
        av_gain = uo_rms / ui_rms;
    }
    else
    {
        av_gain = 0.0f;
    }

    if (fabsf(phase_deg) > 90.0f)
    {
        av_gain = -fabsf(av_gain);
    }
    else
    {
        av_gain = fabsf(av_gain);
    }

    FFT_ApplyAverage(ui_rms, uo_rms, av_gain, meas_freq_hz, phase_deg, out);

    if (target_freq_hz > 0U)
    {
        out->freq_ok = (fabsf(out->meas_freq_hz - (float)target_freq_hz) /
                        (float)target_freq_hz <= (FREQ_ERROR_PERMIL_MAX / 1000.0f)) ? 1U : 0U;
    }
    else
    {
        out->freq_ok = 0U;
    }

    return 0U;
}
