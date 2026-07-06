/* Includes ------------------------------------------------------------------*/
#include "BoardConfig.h"
#include "stm32f4xx_it.h"
#include "ADC_Dual.h"
#include "App_Main.h"
#include "Key.h"
#include "Display.h"

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/******************************************************************************/

void TIM4_IRQHandler(void)
{
    Display_OnTim4Irq();
}

void TIM7_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM7, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
        Key_Tick20ms();
#if (FEATURE_KEY_ENABLE != 0U)
        Key_Poll();
#endif
        App_CheckFault();

        if (g_ADC_FrameReady != 0U)
        {
            App_BackgroundProcess();
        }

#if (FEATURE_DISPLAY_ENABLE != 0U)
        Display_ServicePending();
#endif
    }
}
