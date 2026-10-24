#ifndef __ESP32_H
#define __ESP32_H

#include "usart.h"
#include <string.h>
#include <stdint.h>

// 北京时间
typedef struct
{
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
    uint8_t  wday;      // 0=周日 ... 6=周六
} BJTime_t;

// 天气（温度/风速为实际值的10倍，便于不用浮点打印）
typedef struct
{
    int16_t  temp10;    // 温度 *10，单位 ℃
    int16_t  humi10;    // 相对湿度 *10，单位 %
    int16_t  wind10;    // 风速 *10，单位 km/h
    uint8_t  code;      // WMO 天气代码
} Weather_t;

// 初始化：启动串口接收、复位模块、关闭回显、设置 Station 模式
int  ESP32_Init(void);

// 发送 AT 指令，等待 OK；返回 0 成功，-1 失败(ERROR/FAIL)，-2 超时
int  ESP32_SendCmd(const char *cmd, uint32_t timeout);

// 连接 WiFi
int  ESP32_WiFiConnect(const char *ssid, const char *pwd);

// 通过 SNTP 获取北京时间
int  ESP32_GetTime(BJTime_t *t);

// 通过 HTTPS 获取北京天气 (Open-Meteo)
int  ESP32_GetWeather(Weather_t *w);

// 天气代码转英文描述
const char *Weather_CodeToStr(uint8_t code);

#endif
