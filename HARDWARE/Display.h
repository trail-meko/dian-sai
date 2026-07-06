#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

void Display_Init(void);
void Display_SetRefreshEnable(uint8_t enable);
void Display_OnTim4Irq(void);
void Display_ServicePending(void);
void Display_Invalidate(void);
void Display_Poll(void);
uint8_t Display_IsBusy(void);

#endif /* DISPLAY_H */
