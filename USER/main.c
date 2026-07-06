#include "stm32f4xx.h"
#include "BoardConfig.h"
#include "PeriphInit.h"
#include "App_Main.h"
#include "DAC_Wave.h"
#include "ADC_Dual.h"
#include "Display.h"
#include "Key.h"

int main(void)
{
    PeriphInit_All();
    App_Init();
    DAC_Wave_Init();

#if (FEATURE_DISPLAY_ENABLE != 0U)
    Display_Init();
#endif

    ADC_Dual_Init();
    Key_Init();
    Key_StartScan();

    while (1)
    {
    }
}
