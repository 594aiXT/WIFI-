#include "net_app.h"
#include "oled.h"
#include "ds1302.h"
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------ 状态变量 ------------------------------ */
static BJTime_t   s_time;                 /* 最近一次从 DS1302 读到的时间      */
static Weather_t  s_weather;
static uint8_t    s_timeValid    = 0;     /* DS1302 时间是否有效               */
static uint8_t    s_weatherValid = 0;

static uint8_t    s_espOK        = 0;
static uint8_t    s_wifiOK       = 0;
static uint8_t    s_failCnt      = 0;

static uint8_t    s_dateDirty    = 1;     /* 日期行需要重绘                   */
static uint8_t    s_weatherDirty = 1;     /* 天气行需要重绘                   */
static uint8_t    s_display      = 1;     /* 1=当前页面归本模块绘制，0=暂停绘制 */

static uint32_t   s_lastTick     = 0;     /* 上次读取 DS1302 的时刻           */
static uint32_t   s_nextTime     = 0;     /* 下次联网校时时刻                 */
static uint32_t   s_nextWeather  = 0;     /* 下次刷新天气时刻                 */
static uint32_t   s_nextConn     = 0;     /* 下次重连时刻                     */

static const char *const s_wdayName[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

/* ------------------------------ 工具函数 ------------------------------ */
static int Due(uint32_t deadline)
{
    return (int32_t)(HAL_GetTick() - deadline) >= 0;
}

/* OLED 一行 16 个字符（8x16 字体，行号 0~3），不足补空格以覆盖旧内容 */
static void ShowRow(uint8_t row, const char *s)
{
    char line[17];
    snprintf(line, sizeof(line), "%-16s", s);
    OLED_ShowString(0, (uint8_t)(row * 2), line);
}

static void SetWifiState(uint8_t ok)
{
    if (s_wifiOK != ok)
    {
        s_wifiOK = ok;
        s_dateDirty = 1;                /* 日期行末尾的联网标记需要刷新 */
    }
}

/* ------------------------------ DS1302 ------------------------------ */
/* 星期约定：DS1302 week 1~7 = 周一~周日；BJTime_t.wday 0~6 = 周日~周六 */

/* 读取 DS1302，数据合法返回 0 */
static int Rtc_Read(BJTime_t *t)
{
    ds1032_read_realTime();

    /* 掉电后寄存器会回到 2000 年，或总线异常读到全 0/全 FF，统一视为无效 */
    if (TimeData.year  < 2024 || TimeData.year > 2099 ||
        TimeData.month < 1    || TimeData.month > 12  ||
        TimeData.day   < 1    || TimeData.day   > 31  ||
        TimeData.hour  > 23   || TimeData.minute > 59 || TimeData.second > 59 ||
        TimeData.week  < 1    || TimeData.week   > 7)
    {
        return -1;
    }

    t->year  = TimeData.year;
    t->month = TimeData.month;
    t->day   = TimeData.day;
    t->hour  = TimeData.hour;
    t->min   = TimeData.minute;
    t->sec   = TimeData.second;
    t->wday  = (uint8_t)(TimeData.week % 7);
    return 0;
}

/* 把网络时间写入 DS1302，成功返回 0 */
static int Rtc_Write(const BJTime_t *t)
{
    struct TIMEData d;
    d.year   = t->year;
    d.month  = t->month;
    d.day    = t->day;
    d.hour   = t->hour;
    d.minute = t->min;
    d.second = t->sec;
    d.week   = (t->wday == 0) ? 7 : t->wday;
    return ds1302_set_time(&d) ? 0 : -1;
}

/* 从 DS1302 刷新显示用的时间 */
static void Rtc_Refresh(void)
{
    BJTime_t t;

    if (Rtc_Read(&t) != 0)
    {
        if (s_timeValid)
        {
            s_timeValid = 0;
            s_dateDirty = 1;
        }
        return;
    }

    if (!s_timeValid || t.day != s_time.day || t.month != s_time.month)
        s_dateDirty = 1;

    s_time = t;
    s_timeValid = 1;
}

/* ------------------------------ 显示 ------------------------------ */
static void Draw_Date(void)
{
    char buf[24];
    if (!s_display) return;                     /* 不在本页：保留脏标记，回到本页时统一重绘 */
    char link = s_wifiOK ? 'W' : 'x';           /* W=已联网  x=离线（仅用 DS1302 走时） */

    if (s_timeValid)
        snprintf(buf, sizeof(buf), "%04u-%02u-%02u %s %c",
                 s_time.year, s_time.month, s_time.day,
                 s_wdayName[s_time.wday % 7], link);
    else
        snprintf(buf, sizeof(buf), "----/--/--      %c", link);
    ShowRow(0, buf);
    s_dateDirty = 0;
}

static void Draw_Time(void)
{
    char buf[24];
    if (!s_display) return;
    if (s_timeValid)
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", s_time.hour, s_time.min, s_time.sec);
    else
        snprintf(buf, sizeof(buf), "--:--:--");
    ShowRow(1, buf);
}

static void Draw_Weather(void)
{
    char buf[24];
    if (!s_display) return;

    if (!s_weatherValid)
    {
        ShowRow(2, "Beijing: --");
        ShowRow(3, "--.-C H--% W--");
        s_weatherDirty = 0;
        return;
    }

    snprintf(buf, sizeof(buf), "Beijing %s", Weather_CodeToStr(s_weather.code));
    ShowRow(2, buf);

    int t10 = s_weather.temp10;
    char sign = (t10 < 0) ? '-' : ' ';
    if (t10 < 0) t10 = -t10;

    char humi[8];
    if (s_weather.humi10 >= 0)
        snprintf(humi, sizeof(humi), "%d%%", s_weather.humi10 / 10);
    else
        snprintf(humi, sizeof(humi), "--");

    snprintf(buf, sizeof(buf), "%c%d.%dC H%s W%d",
             sign, t10 / 10, t10 % 10, humi, s_weather.wind10 / 10);
    ShowRow(3, buf);
    s_weatherDirty = 0;
}

static void Draw_All(void)
{
    Draw_Date();
    Draw_Time();
    Draw_Weather();
}

/* ------------------------------ 联网动作 ------------------------------ */
static int ConnectWiFi(void)
{
    for (int i = 0; i < NET_WIFI_RETRY; i++)
    {
        if (ESP32_WiFiConnect(NET_WIFI_SSID, NET_WIFI_PWD) == 0)
            return 0;
        ESP32_SendCmd("AT+CWQAP\r\n", 2000);    /* 停止后台反复重连 */
        HAL_Delay(1000);
    }
    return -1;
}

/* 网络校时：成功后写入 DS1302，之后断网也能继续走时 */
static int DoSyncTime(void)
{
    BJTime_t t;
    if (ESP32_GetTime(&t) != 0)
        return -1;

    if (Rtc_Write(&t) != 0)
        return -2;

    Rtc_Refresh();                      /* 从 DS1302 读回，保证显示与 RTC 一致 */
    s_dateDirty = 1;
    return 0;
}

static int DoWeather(void)
{
    Weather_t w;
    if (ESP32_GetWeather(&w) != 0)
        return -1;

    s_weather      = w;
    s_weatherValid = 1;
    s_weatherDirty = 1;
    return 0;
}

/* 在线更新结果统一处理：连续失败 3 次认为 WiFi 已断，转入重连 */
static void OnUpdateResult(int ret)
{
    if (ret == 0)
    {
        s_failCnt = 0;
    }
    else if (++s_failCnt >= 3)
    {
        SetWifiState(0);
        s_failCnt = 0;
        s_nextConn = HAL_GetTick();             /* 立即尝试重连 */
    }
}

/* ------------------------------ 对外接口 ------------------------------ */
int NetApp_Init(void)
{
    /* 0. DS1302：先读出已保存的时间，没网也能显示 */
    ds1302_gpio_init();
    ds1032_init();
    Rtc_Refresh();
    s_lastTick = HAL_GetTick();

    OLED_Clear();

    /* 先显示一次 DS1302 的时间（离线可用），再开始联网 */
    Draw_Date();
    Draw_Time();
    HAL_Delay(800);
    OLED_Clear();

    /* 1. ESP32 */
    ShowRow(0, "ESP32  : ...");
    if (ESP32_Init() != 0)
    {
        ShowRow(0, "ESP32  : FAIL");
        s_espOK = 0;
        s_nextConn = HAL_GetTick() + NET_RETRY_MS;
        goto offline;
    }
    s_espOK = 1;
    ShowRow(0, "ESP32  : OK");

    /* 2. WiFi */
    ShowRow(1, "WiFi   : ...");
    if (ConnectWiFi() != 0)
    {
        ShowRow(1, "WiFi   : FAIL");
        SetWifiState(0);
        s_nextConn = HAL_GetTick() + NET_RETRY_MS;
        goto offline;
    }
    SetWifiState(1);
    ShowRow(1, "WiFi   : OK");

    /* 3. 时间 */
    ShowRow(2, "Time   : ...");
    if (DoSyncTime() == 0)
    {
        ShowRow(2, "Time   : OK");
        s_nextTime = HAL_GetTick() + NET_TIME_SYNC_MS;
    }
    else
    {
        ShowRow(2, "Time   : FAIL");
        s_nextTime = HAL_GetTick() + NET_RETRY_MS;
    }

    /* 4. 天气 */
    ShowRow(3, "Weather: ...");
    if (DoWeather() == 0)
    {
        ShowRow(3, "Weather: OK");
        s_nextWeather = HAL_GetTick() + NET_WEATHER_MS;
    }
    else
    {
        ShowRow(3, "Weather: FAIL");
        s_nextWeather = HAL_GetTick() + NET_RETRY_MS;
    }

    HAL_Delay(1000);                    /* 让用户看清结果 */
    OLED_Clear();
    Rtc_Refresh();
    Draw_All();
    return (s_timeValid && s_weatherValid) ? 0 : -3;

offline:
    /* 联网失败：直接用 DS1302 的时间继续显示，后台定时重连 */
    HAL_Delay(1500);
    OLED_Clear();
    Rtc_Refresh();
    Draw_All();
    return -1;
}

void NetApp_Process(void)
{
    uint32_t now = HAL_GetTick();

    /* 1. 每秒读一次 DS1302 并刷新时间行（与 WiFi 状态无关） */
    if (now - s_lastTick >= 1000U)
    {
        s_lastTick = now;
        Rtc_Refresh();
        if (s_dateDirty) Draw_Date();
        Draw_Time();
    }

    if (s_dateDirty)    Draw_Date();
    if (s_weatherDirty) Draw_Weather();

    /* 2. 掉线或模块未就绪：定时重连 */
    if ((!s_espOK || !s_wifiOK) && Due(s_nextConn))
    {
        if (!s_espOK && ESP32_Init() == 0) s_espOK = 1;

        if (s_espOK && ConnectWiFi() == 0)
        {
            SetWifiState(1);
            s_failCnt     = 0;
            s_nextTime    = HAL_GetTick();      /* 连上后立即校时、更新天气 */
            s_nextWeather = HAL_GetTick();
        }
        else
        {
            s_nextConn = HAL_GetTick() + NET_RETRY_MS;
        }
        return;
    }

    if (!s_wifiOK) return;

    /* 3. 定时校时 */
    if (Due(s_nextTime))
    {
        int r = DoSyncTime();
        s_nextTime = HAL_GetTick() + ((r == 0) ? NET_TIME_SYNC_MS : NET_RETRY_MS);
        OnUpdateResult(r);
        if (r == 0) { Draw_Date(); Draw_Time(); }
        return;
    }

    /* 4. 定时刷新天气 */
    if (Due(s_nextWeather))
    {
        int r = DoWeather();
        s_nextWeather = HAL_GetTick() + ((r == 0) ? NET_WEATHER_MS : NET_RETRY_MS);
        OnUpdateResult(r);
        if (r == 0) Draw_Weather();
    }
}

int NetApp_GetTime(BJTime_t *t)
{
    if (!s_timeValid || t == NULL) return -1;
    *t = s_time;
    return 0;
}

int NetApp_GetWeather(Weather_t *w)
{
    if (!s_weatherValid || w == NULL) return -1;
    *w = s_weather;
    return 0;
}

/* 页面切换时由 UI 层调用：on=1 表示回到时间天气页并整页重绘（调用前请先清屏） */
void NetApp_SetDisplay(uint8_t on)
{
    s_display = on ? 1 : 0;
    if (s_display)
    {
        s_dateDirty = 1;
        s_weatherDirty = 1;
        Rtc_Refresh();
        Draw_All();
    }
}

void NetApp_GetStatus(NetStatus_t *st)
{
    if (st == NULL) return;
    st->espOK        = s_espOK;
    st->wifiOK       = s_wifiOK;
    st->timeValid    = s_timeValid;
    st->weatherValid = s_weatherValid;
}
