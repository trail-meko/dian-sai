#include "stm32f4xx.h"
#include "PeriphInit.h"
#include "lit.h"

void PeriphInit_Nvic(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
}

void PeriphInit_All(void)
{
    PeriphInit_Nvic();
    Init_sig();
}
