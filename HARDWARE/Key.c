#include "stm32f4xx.h"
#include "BoardConfig.h"
#include "Key.h"
#include "App_Main.h"

static uint8_t s_KeyPrevDown[3];
static volatile uint8_t s_KeyPending;

static GPIO_TypeDef * const s_KeyGpio[3] = {
    PIN_KEY1_GPIO,
    PIN_KEY2_GPIO,
    PIN_KEY3_GPIO
};

static const uint16_t s_KeyPin[3] = {
    PIN_KEY1_PIN,
    PIN_KEY2_PIN,
    PIN_KEY3_PIN
};

static uint8_t Key_IsPressed(GPIO_TypeDef *gpio, uint16_t pin)
{
    return (GPIO_ReadInputDataBit(gpio, pin) == Bit_RESET) ? 1U : 0U;
}

static void Key_SyncState(void)
{
    uint8_t i;

    for (i = 0U; i < 3U; i++)
    {
        s_KeyPrevDown[i] = Key_IsPressed(s_KeyGpio[i], s_KeyPin[i]);
    }
}

static void Key_StartTim7(void)
{
    TIM_TimeBaseInitTypeDef tim;
    NVIC_InitTypeDef nvic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);

    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Prescaler = 8399U;
    tim.TIM_Period = 199U;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM7, &tim);

    TIM_ClearITPendingBit(TIM7, TIM_IT_Update);
    TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel = TIM7_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = IRQ_PRIO_TIM7;
    nvic.NVIC_IRQChannelSubPriority = 0U;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    TIM_Cmd(TIM7, ENABLE);
}

void Key_Init(void)
{
    GPIO_InitTypeDef gpio;

    s_KeyPending = 0U;
    Key_SyncState();

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = PIN_KEY1_PIN | PIN_KEY2_PIN | PIN_KEY3_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_KEY1_GPIO, &gpio);

    Key_SyncState();
}

void Key_StartScan(void)
{
    Key_SyncState();
    Key_StartTim7();
}

void Key_Tick20ms(void)
{
    uint8_t i;
    uint8_t down;

    for (i = 0U; i < 3U; i++)
    {
        down = Key_IsPressed(s_KeyGpio[i], s_KeyPin[i]);

        /* B.7：20 ms 周期，低电平有效，检测释放→按下 */
        if ((down != 0U) && (s_KeyPrevDown[i] == 0U))
        {
            s_KeyPending = (uint8_t)(i + 1U);
        }

        s_KeyPrevDown[i] = down;
    }
}

void Key_Poll(void)
{
    uint8_t key_id;

    key_id = s_KeyPending;
    if (key_id == 0U)
    {
        return;
    }

    s_KeyPending = 0U;

    if (key_id > 3U)
    {
        return;
    }

    App_OnKeyEvent(key_id);
}
