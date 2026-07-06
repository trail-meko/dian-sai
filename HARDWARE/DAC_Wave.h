#ifndef DAC_WAVE_H
#define DAC_WAVE_H

#include <stdint.h>

#define WAVE_SINE      0U
#define WAVE_TRIANGLE  1U

void DAC_Wave_Init(void);
void DAC_Wave_SetFrequencyIndex(uint8_t index);
void DAC_Wave_SetWaveType(uint8_t wave);
void DAC_Wave_SetGainScaleQ15(uint16_t scale);
void DAC_Wave_ApplyManualGain(void);
uint16_t DAC_Wave_GetGainScaleQ15(void);

#endif /* DAC_WAVE_H */
