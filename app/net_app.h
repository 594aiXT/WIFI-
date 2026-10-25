#ifndef __NET_APP_H
#define __NET_APP_H

#include "esp32.h"
#include <stdint.h>

/* ---------------- 用户配置 ---------------- */
#define NET_WIFI_SSID          "REDACTED_SSID"
#define NET_WIFI_PWD           "REDACTED_PWD"

#define NET_WIFI_RETRY         3            /* 连接 WiFi 最多尝试次数            */
#define NET_TIME_SYNC_MS       3600000UL    /* 每 1 小时重新校时                 */
#define NET_WEATHER_MS         1800000UL    /* 每 30 分钟刷新天气                */
#define NET_RETRY_MS           60000UL      /* 更新失败后 60 秒重试              */

/* 初始化：DS1302 -> ESP32 -> 连接 WiFi -> 网络校时(写入 DS1302) -> 获取天气，
 * 过程中在 OLED 上显示进度。返回 0 表示全部成功。
 * 联网失败时仍用 DS1302 的时间继续显示，并在后台定时重连。
 * 调用前需已完成 OLED_Init()。                                        */
int  NetApp_Init(void);

/* 放在 main 的 while(1) 中周期调用：
 * - 每秒读取 DS1302 并刷新 OLED 时间（断网也走时）
 * - 定时重新校时、刷新天气                                            */
void NetApp_Process(void);

/* 读取当前数据（供其他模块，如写入 DS1302 使用） */
int  NetApp_GetTime(BJTime_t *t);        /* 0=有效 */
int  NetApp_GetWeather(Weather_t *w);    /* 0=有效 */
const char *NetApp_GetWeatherText(void); /* 天气状况文字，无数据时为 "--" */

/* 联网状态（供状态页显示） */
typedef struct
{
    uint8_t espOK;          /* ESP32 模块正常        */
    uint8_t wifiOK;         /* WiFi 已连接           */
    uint8_t timeValid;      /* DS1302 时间有效       */
    uint8_t weatherValid;   /* 已获取过天气          */
} NetStatus_t;

void NetApp_GetStatus(NetStatus_t *st);

/* 页面控制：on=0 暂停本模块对 OLED 的绘制；on=1 恢复并整页重绘（调用前先 OLED_Clear） */
void NetApp_SetDisplay(uint8_t on);

/* 响铃/提示期间暂停联网请求（重连、校时、天气），避免阻塞导致铃声卡顿；
 * DS1302 读时和 OLED 刷新不受影响。on=0 恢复。 */
void NetApp_PauseNetwork(uint8_t on);

#endif
