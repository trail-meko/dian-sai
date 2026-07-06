#ifndef FFT_ANALYZE_H
#define FFT_ANALYZE_H

#include <stdint.h>

typedef struct
{
    float ui_rms_v;
    float uo_rms_v;
    float av_gain;
    float phase_deg;
    float meas_freq_hz;
    float h1_v;         /* 1st harmonic RMS voltage (input fundamental) */
    float h3_v;         /* 3rd harmonic RMS voltage (input) */
    float h5_v;         /* 5th harmonic RMS voltage (input) */
    uint8_t freq_ok;
    uint16_t adc_ui_min;
    uint16_t adc_ui_max;
    uint16_t adc_uo_min;
    uint16_t adc_uo_max;
} FFTMeasure_t;

extern volatile float g_FFT_DbgUiPeakMag;
extern volatile uint16_t g_FFT_DbgPeakBin;
extern volatile uint16_t g_FFT_DbgTargetBin;

void FFT_Analyze_ResetAvg(void);
uint8_t FFT_Analyze_Process(uint16_t *raw_ui,
                            uint16_t *raw_uo,
                            uint16_t target_freq_hz,
                            FFTMeasure_t *out,
                            uint8_t *fail_code);

#endif /* FFT_ANALYZE_H */
