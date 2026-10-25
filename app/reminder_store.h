#ifndef __REMINDER_STORE_H
#define __REMINDER_STORE_H

#include <stdint.h>

/* 需要掉电保存的提醒设置 */
typedef struct
{
    uint8_t alarmHour;      /* 0~23                              */
    uint8_t alarmMinute;    /* 0~59                              */
    uint8_t alarmMode;      /* 0=关 1=仅一次 2=每天 3=工作日      */
    uint8_t focusMinutes;   /* 专注时长，1~99                    */
} ReminderSettings_t;

/* 读取：成功返回 0；从未保存过或数据无效返回 -1（此时 s 不被修改）。
 * 存放在 DS1302 的 31 字节后备 RAM 中，需在 ds1302_gpio_init()/ds1032_init() 之后调用。 */
int  ReminderStore_Load(ReminderSettings_t *s);

/* 保存（会临时关闭并恢复 DS1302 写保护）。 */
void ReminderStore_Save(const ReminderSettings_t *s);

#endif
