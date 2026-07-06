#include "stm32f4xx.h"
#include "BoardConfig.h"

void Init_sig(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin = PIN_FAULT_LED_PIN;
    gpio.GPIO_Mode = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(PIN_FAULT_LED_GPIO, &gpio);

    /* Low active: default on. */
    GPIO_ResetBits(PIN_FAULT_LED_GPIO, PIN_FAULT_LED_PIN);
}
