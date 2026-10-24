#ifndef __DS1302_H
#define __DS1302_H

#include "stm32f1xx_hal.h"

typedef uint8_t u8;
typedef uint16_t u16;

/* 时间数据结构 */
struct TIMEData
{
    u16 year;
    u8  month;
    u8  day;
    u8  hour;
    u8  minute;
    u8  second;
    u8  week;       /* 1~7，由应用定义星期对应关系 */
};

extern struct TIMEData TimeData;
extern u8 read_time[7];

/* GPIO 和驱动初始化 */
void ds1302_gpio_init(void);
void ds1032_init(void);

/* 底层读写接口 */
void ds1302_write_onebyte(u8 data);
void ds1302_wirte_rig(u8 address, u8 data);
u8 ds1302_read_rig(u8 address);

/* 设置和读取时间 */
u8 ds1302_set_time(const struct TIMEData *time);
void ds1032_read_time(void);
void ds1032_read_realTime(void);

/* DATA 引脚输入输出配置 */
void ds1032_DATAOUT_init(void);
void ds1032_DATAINPUT_init(void);

/* 微秒延时 */
void delay_us(uint32_t us);

#endif