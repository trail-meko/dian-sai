#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdint.h>
#include "BoardConfig.h"

typedef enum
{
    WORK_MODE_MANUAL = 0,
    WORK_MODE_AUTO
} WorkMode_t;

typedef struct
{
    WorkMode_t mode;
    uint8_t    wave;
    uint8_t    freq_index;
    uint16_t   target_freq_hz;
} AppParams_t;

typedef struct
{
    float    ui_rms_v;
    float    uo_rms_v;
    float    av_gain;
    float    phase_deg;
    float    meas_freq_hz;
    float    h1_v;           /* 1st harmonic RMS voltage (input fundamental) */
    float    h3_v;           /* 3rd harmonic RMS voltage (input) */
    float    h5_v;           /* 5th harmonic RMS voltage (input) */
    float    target_out_v;
    uint8_t  freq_ok;
    uint8_t  fault_flags;
    uint8_t  fft_fail_code;
    uint16_t adc_ui_min;
    uint16_t adc_ui_max;
    uint16_t adc_uo_min;
    uint16_t adc_uo_max;
    uint8_t  cfg_state;
    uint8_t  pid_saturated;
} MeasureResult_t;

extern AppParams_t g_AppParams;
extern MeasureResult_t g_MeasureResult;

void App_Init(void);
void App_ScheduleBackgroundProcess(void);
void App_BackgroundProcess(void);
void App_OnKeyEvent(uint8_t key_id);
void App_CheckFault(void);

#endif /* APP_MAIN_H */
