#include "encoder.h"

/* A 相 PB14，B 相 PB12，按键 PB13。 */
#define ENCODER_A_PORT        GPIOB
#define ENCODER_A_PIN         GPIO_PIN_14
#define ENCODER_B_PORT        GPIOB
#define ENCODER_B_PIN         GPIO_PIN_12
#define ENCODER_SW_PORT       GPIOB
#define ENCODER_SW_PIN        GPIO_PIN_13

#define ENCODER_DEBOUNCE_MS   30U     /* 按键必须连续 30ms 稳定才算有效         */
#define ENCODER_LONG_MS       700U
#define ENCODER_ROTATE_GUARD_MS 150U  /* 旋转后 150ms 内忽略按键（抑制旋转串扰） */
#define ENCODER_STEPS_PER_DETENT 4

/* 正交解码器以四分之一档位累计，满 4 个合法边沿输出一档。 */
static volatile int16_t Encoder_SubSteps = 0;
static volatile uint8_t Encoder_LastAB = 0;

/* 按键状态机：由 1ms 节拍驱动（见 Encoder_Tick1ms），与主循环快慢无关 */
static uint8_t  button_pressed = 0;       /* 消抖后的稳定状态：1=按下 */
static uint8_t  button_count = 0;         /* 与稳定状态不同的连续毫秒数 */
static uint32_t button_press_tick;
static uint8_t  long_press_sent;
static uint8_t  press_aborted;            /* 按下期间发生旋转：本次按键作废 */
static volatile uint8_t tick_hooked = 0;  /* 1ms 节拍已接入 SysTick */
static volatile uint8_t enc_ready = 0;    /* Encoder_Init 完成后才允许采样（SysTick 在此之前就已运行） */

/* 最近一次 A/B 边沿的时刻：旋转触点抖动可能串扰到按键脚 */
static volatile uint32_t last_rotate_tick = 0;

static volatile EncoderButtonEvent button_event = ENCODER_BUTTON_NONE;

/* 完整正交状态表：索引 = 前一状态 << 2 | 当前状态。
 * 状态编码 A<<1 | B；符号保持原驱动的旋转方向。 */
static const int8_t Encoder_Transition[16] =
{
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static uint8_t Encoder_ReadAB(void)
{
    uint8_t a = (HAL_GPIO_ReadPin(ENCODER_A_PORT, ENCODER_A_PIN) ==
                 GPIO_PIN_SET) ? 1U : 0U;
    uint8_t b = (HAL_GPIO_ReadPin(ENCODER_B_PORT, ENCODER_B_PIN) ==
                 GPIO_PIN_SET) ? 1U : 0U;
    return (uint8_t)((a << 1) | b);
}

void Encoder_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /* A/B 双边沿中断。机械抖动产生的往返边沿会在状态表中相互抵消。 */
    GPIO_InitStruct.Pin = ENCODER_A_PIN | ENCODER_B_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* 按键使用轮询和 30ms 稳定窗口，不使用 EXTI。 */
    GPIO_InitStruct.Pin = ENCODER_SW_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ENCODER_SW_PORT, &GPIO_InitStruct);

    /* CubeMX 曾把 PB13 也配成 EXTI，这里显式关闭该线，避免无意义中断。 */
    EXTI->IMR &= ~((uint32_t)ENCODER_SW_PIN);
    EXTI->EMR &= ~((uint32_t)ENCODER_SW_PIN);
    EXTI->RTSR &= ~((uint32_t)ENCODER_SW_PIN);
    EXTI->FTSR &= ~((uint32_t)ENCODER_SW_PIN);
    __HAL_GPIO_EXTI_CLEAR_IT(ENCODER_SW_PIN);

    Encoder_SubSteps = 0;
    Encoder_LastAB = Encoder_ReadAB();

    button_pressed = 0;
    button_count = 0;
    button_press_tick = HAL_GetTick();
    long_press_sent = 0;
    press_aborted = 0;
    last_rotate_tick = HAL_GetTick() - ENCODER_ROTATE_GUARD_MS;   /* 上电时封锁未生效 */
    button_event = ENCODER_BUTTON_NONE;

    __HAL_GPIO_EXTI_CLEAR_IT(ENCODER_A_PIN);
    __HAL_GPIO_EXTI_CLEAR_IT(ENCODER_B_PIN);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    enc_ready = 1;
}

int16_t Encoder_Get(void)
{
    int16_t raw;
    int16_t detents;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    raw = Encoder_SubSteps;
    Encoder_SubSteps = 0;
    if (primask == 0U) __enable_irq();

    detents = (int16_t)(raw / ENCODER_STEPS_PER_DETENT);

    /* 保留不足一档的余量，避免慢速旋转时丢步。 */
    primask = __get_PRIMASK();
    __disable_irq();
    Encoder_SubSteps += (int16_t)(raw % ENCODER_STEPS_PER_DETENT);
    if (primask == 0U) __enable_irq();

    return detents;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint8_t current;
    uint8_t index;

    if (GPIO_Pin != ENCODER_A_PIN && GPIO_Pin != ENCODER_B_PIN)
    {
        return;
    }

    last_rotate_tick = HAL_GetTick();       /* 任何 A/B 边沿（含抖动）都视为正在旋转 */

    current = Encoder_ReadAB();
    index = (uint8_t)((Encoder_LastAB << 2) | current);
    Encoder_LastAB = current;
    Encoder_SubSteps += Encoder_Transition[index];
}

/* 按键采样，每 1ms 调用一次。
 * 消抖：与当前稳定状态不同的电平必须连续保持 30ms，中间只要回到原电平就重新计时。
 * 抗旋转串扰：旋转后 150ms 内按键一律视为松开；若按下过程中发生旋转，
 * 本次按键作废，松开时不产生短按事件，也不会触发长按。 */
static void Encoder_ButtonSample(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t raw_pressed;

    if (!enc_ready) return;
    raw_pressed =
        (HAL_GPIO_ReadPin(ENCODER_SW_PORT, ENCODER_SW_PIN) == GPIO_PIN_RESET) ? 1U : 0U;

    if ((now - last_rotate_tick) < ENCODER_ROTATE_GUARD_MS)
    {
        if (button_pressed) press_aborted = 1;
        raw_pressed = 0;
    }

    if (raw_pressed != button_pressed)
    {
        if (++button_count >= ENCODER_DEBOUNCE_MS)
        {
            button_count = 0;
            button_pressed = raw_pressed;

            if (button_pressed)
            {
                button_press_tick = now;
                long_press_sent = 0;
                press_aborted = 0;
            }
            else if (!long_press_sent && !press_aborted)
            {
                button_event = ENCODER_BUTTON_CONFIRM;      /* 松开时产生短按 */
            }
        }
    }
    else
    {
        button_count = 0;
    }

    if (button_pressed && !long_press_sent && !press_aborted &&
        (now - button_press_tick) >= ENCODER_LONG_MS)
    {
        button_event = ENCODER_BUTTON_BACK;
        long_press_sent = 1;
    }
}

/* 在 SysTick_Handler 中每 1ms 调用一次（见 stm32f1xx_it.c）。 */
void Encoder_Tick1ms(void)
{
    tick_hooked = 1;
    Encoder_ButtonSample();
}

/* 兼容旧接口：已接入 1ms 节拍时不做任何事；
 * 未接入时退化为主循环轮询（主循环慢时消抖效果会变差，建议接入 1ms 节拍）。 */
void Encoder_ButtonTask(void)
{
    static uint32_t last_poll = 0;
    uint32_t now = HAL_GetTick();
    uint32_t n;

    if (tick_hooked) return;

    n = now - last_poll;                 /* 按经过的毫秒数补采样，保证消抖时间准确 */
    last_poll = now;
    if (n > 100U) n = 100U;
    while (n-- > 0U) Encoder_ButtonSample();
}

EncoderButtonEvent Encoder_GetButtonEvent(void)
{
    EncoderButtonEvent temp;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    temp = button_event;
    button_event = ENCODER_BUTTON_NONE;
    if (primask == 0U) __enable_irq();

    return temp;
}