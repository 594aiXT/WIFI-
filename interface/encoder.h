#ifndef __ENCODER_H
#define __ENCODER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef enum
{
    ENCODER_BUTTON_NONE = 0,
    ENCODER_BUTTON_CONFIRM,  /* 消抖后的短按 */
    ENCODER_BUTTON_BACK      /* 按住达到长按时间 */
} EncoderButtonEvent;

void Encoder_Init(void);

/* 读取并清除累计的旋转档位数。
 * 返回 +N 表示顺时针 N 档，-N 表示逆时针 N 档。 */
int16_t Encoder_Get(void);

/* 按键消抖和长按状态机，每 1ms 调用一次。
 * 请在 stm32f1xx_it.c 的 SysTick_Handler 中调用，不要放在主循环里。 */
void Encoder_Tick1ms(void);

/* 兼容旧接口：已接入 Encoder_Tick1ms() 时不做任何事。 */
void Encoder_ButtonTask(void);

EncoderButtonEvent Encoder_GetButtonEvent(void);

#endif