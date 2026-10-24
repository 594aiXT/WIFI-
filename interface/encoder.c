#include "encoder.h"

/* 编码器引脚：避开 OLED、DS1302、USART2 和 I2C1
 * A 相 = PB14，B 相 = PB12，按键 = PB13
 * PB12、PB14 都属于 EXTI15_10 中断线（共用一个中断入口）*/
#define ENCODER_A_PORT       GPIOB
#define ENCODER_A_PIN        GPIO_PIN_14

#define ENCODER_B_PORT       GPIOB
#define ENCODER_B_PIN        GPIO_PIN_12

#define ENCODER_SW_PORT      GPIOB
#define ENCODER_SW_PIN       GPIO_PIN_13

#define ENCODER_DEBOUNCE_MS  20U
#define ENCODER_LONG_MS      700U

/* 旋转计数：中断中修改，主循环中读取 */
static volatile int16_t Encoder_Count = 0;

/* 按键状态 */
static GPIO_PinState button_raw_last;
static GPIO_PinState button_stable;
static uint32_t button_change_tick;
static uint32_t button_press_tick;
static uint8_t long_press_sent;

static volatile EncoderButtonEvent button_event =
    ENCODER_BUTTON_NONE;


/**
 * @brief 旋转编码器初始化
 */
void Encoder_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /*
     * PB14、PB12：编码器 A/B 相
     * 下降沿触发，中断模式，内部上拉
     */
    GPIO_InitStruct.Pin =
        ENCODER_A_PIN | ENCODER_B_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /*
     * PB13：编码器按键（普通输入，由主循环消抖轮询，不使用中断）
     * 按下接地，因此按下时为 GPIO_PIN_RESET
     */
    GPIO_InitStruct.Pin = ENCODER_SW_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ENCODER_SW_PORT, &GPIO_InitStruct);

    Encoder_Count = 0;

    button_raw_last =
        HAL_GPIO_ReadPin(ENCODER_SW_PORT, ENCODER_SW_PIN);
    button_stable = button_raw_last;
    button_change_tick = HAL_GetTick();
    button_press_tick = HAL_GetTick();
    long_press_sent = 0;
    button_event = ENCODER_BUTTON_NONE;

    /* 清除可能遗留的 EXTI 标志 */
    __HAL_GPIO_EXTI_CLEAR_IT(ENCODER_A_PIN);
    __HAL_GPIO_EXTI_CLEAR_IT(ENCODER_B_PIN);

    /* PB12、PB14 共用 EXTI15_10 中断 */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}


/**
 * @brief 获取旋转增量
 * @return 正数或负数表示旋转方向，0 表示没有新计数
 *
 * 和原标准外设库版本一样，读取后将计数清零。
 */
int16_t Encoder_Get(void)
{
    int16_t temp;
    uint32_t primask;

    /*
     * 读取并清零是一个整体，防止操作期间中断修改计数。
     * 同时恢复调用前的中断使能状态。
     */
    primask = __get_PRIMASK();
    __disable_irq();

    temp = Encoder_Count;
    Encoder_Count = 0;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return temp;
}


/**
 * @brief HAL 外部中断回调
 *
 * PB14：A 相下降沿
 * PB12：B 相下降沿
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* A 相下降沿 */
    if (GPIO_Pin == ENCODER_A_PIN)
    {
        if (HAL_GPIO_ReadPin(ENCODER_A_PORT,
                             ENCODER_A_PIN) == GPIO_PIN_RESET)
        {
            if (HAL_GPIO_ReadPin(ENCODER_B_PORT,
                                 ENCODER_B_PIN) == GPIO_PIN_RESET)
            {
                Encoder_Count--;
            }
        }
    }

    /* B 相下降沿 */
    else if (GPIO_Pin == ENCODER_B_PIN)
    {
        if (HAL_GPIO_ReadPin(ENCODER_B_PORT,
                             ENCODER_B_PIN) == GPIO_PIN_RESET)
        {
            if (HAL_GPIO_ReadPin(ENCODER_A_PORT,
                                 ENCODER_A_PIN) == GPIO_PIN_RESET)
            {
                Encoder_Count++;
            }
        }
    }
}


/**
 * @brief 按键消抖、短按和长按检测
 *
 * 短按：松开后产生 CONFIRM
 * 长按：按下持续 700ms 后产生 BACK
 *
 * 必须在主循环中持续调用，不要放进中断。
 */
void Encoder_ButtonTask(void)
{
    uint32_t now = HAL_GetTick();

    GPIO_PinState raw =
        HAL_GPIO_ReadPin(ENCODER_SW_PORT, ENCODER_SW_PIN);

    /* 记录原始电平变化 */
    if (raw != button_raw_last)
    {
        button_raw_last = raw;
        button_change_tick = now;
    }

    /* 电平稳定 20ms 后，确认按键状态发生变化 */
    if ((now - button_change_tick) >= ENCODER_DEBOUNCE_MS)
    {
        if (raw != button_stable)
        {
            button_stable = raw;

            if (button_stable == GPIO_PIN_RESET)
            {
                /* 按下 */
                button_press_tick = now;
                long_press_sent = 0;
            }
            else
            {
                /* 松开：若之前不是长按，则认定为短按 */
                if (!long_press_sent)
                {
                    button_event = ENCODER_BUTTON_CONFIRM;
                }
            }
        }
    }

    /* 长按事件只产生一次 */
    if ((button_stable == GPIO_PIN_RESET) &&
        (!long_press_sent) &&
        ((now - button_press_tick) >= ENCODER_LONG_MS))
    {
        button_event = ENCODER_BUTTON_BACK;
        long_press_sent = 1;
    }
}


/**
 * @brief 取出一次按键事件
 */
EncoderButtonEvent Encoder_GetButtonEvent(void)
{
    EncoderButtonEvent temp;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    temp = button_event;
    button_event = ENCODER_BUTTON_NONE;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return temp;
}