#ifndef PID_CTRL_H
#define PID_CTRL_H

#include <stdint.h>

void PID_Ctrl_Reset(void);
uint8_t PID_Ctrl_Update(float uo_rms_v, uint16_t *gain_scale_q15);

#endif /* PID_CTRL_H */
