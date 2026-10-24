#ifndef __ENCODER_H
#define __ENCODER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef enum
{
    ENCODER_BUTTON_NONE = 0,
    ENCODER_BUTTON_CONFIRM,  // 短按：确定
    ENCODER_BUTTON_BACK     // 长按：返回
} EncoderButtonEvent;

void Encoder_Init(void);

/* 获取自上次调用以来的旋转增量 */
int16_t Encoder_Get(void);

/* 在主循环中周期性调用，处理按键消抖和长按 */
void Encoder_ButtonTask(void);

/* 获取按键事件 */
EncoderButtonEvent Encoder_GetButtonEvent(void);

#endif