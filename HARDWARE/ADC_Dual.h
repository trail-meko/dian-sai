#ifndef ADC_DUAL_H
#define ADC_DUAL_H

#include <stdint.h>
#include "BoardConfig.h"

extern uint16_t g_AD_Ui[FFT_SIZE];
extern uint16_t g_AD_Uo[FFT_SIZE];
extern volatile uint8_t g_ADC_FrameReady;

void ADC_Dual_Init(void);
void ADC_Dual_BeginProcess(uint16_t **snap_ui, uint16_t **snap_uo);
void ADC_Dual_EndProcess(void);
float ADC_Dual_GetSampleRateHz(void);

#endif /* ADC_DUAL_H */
