#ifndef __BEEP_H
#define __BEEP_H

#include <stdint.h>

/* 预置提示音 */
typedef enum
{
    BEEP_CLICK = 0,      /* 操作确认（极短）               */
    BEEP_OK,             /* 保存 / 进入休息                */
    BEEP_START,          /* 专注开始 / 继续（上扬）        */
    BEEP_PAUSE,          /* 专注暂停（下行）               */
    BEEP_SNOOZE,         /* 闹钟贪睡（两短声）             */
    BEEP_ALARM_1,        /* 闹钟第 1 阶段：每秒一声（循环）*/
    BEEP_ALARM_2,        /* 闹钟第 2 阶段：双声（循环）    */
    BEEP_ALARM_3,        /* 闹钟第 3 阶段：急促（循环）    */
    BEEP_FOCUS_DONE,     /* 专注结束：上行旋律（循环）     */
    BEEP_BREAK_DONE,     /* 休息结束：下行旋律（循环）     */
    BEEP_PATTERN_COUNT
} BeepPattern_t;

/* 初始化：启动 TIM2 PWM 输出，默认静音。调用前需已执行 MX_TIM2_Init()。 */
void Beep_Init(void);

/* 直接发声 / 静音（无源蜂鸣器靠 PWM 频率发声） */
void Beep_Tone(uint16_t freq_hz);
void Beep_Off(void);

/* 非阻塞播放预置提示音；循环型提示音需调用 Beep_Stop() 停止 */
void Beep_Play(BeepPattern_t p);
void Beep_Stop(void);
uint8_t Beep_IsPlaying(void);

/* 放在主循环中频繁调用，负责切换音符 */
void Beep_Process(void);

#endif
