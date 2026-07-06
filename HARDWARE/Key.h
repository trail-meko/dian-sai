#ifndef KEY_H
#define KEY_H

#include <stdint.h>

void Key_Init(void);
void Key_StartScan(void);
void Key_Tick20ms(void);
void Key_Poll(void);

#endif /* KEY_H */
