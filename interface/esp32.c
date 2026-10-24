#include "esp32.h"
#include <stdio.h>
#include <stdlib.h>

#define RX_BUF_SIZE   1536
#define ESP_DEBUG     1          // 1: 打印每条指令的收发内容

static uint8_t           rxByte;
static char              rxBuf[RX_BUF_SIZE];
static volatile uint16_t rxLen = 0;

/* ------------------------- 串口接收（逐字节中断） ------------------------- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        if (rxLen < RX_BUF_SIZE - 1)
        {
            rxBuf[rxLen++] = (char)rxByte;
            rxBuf[rxLen] = 0;
        }
        HAL_UART_Receive_IT(&huart2, &rxByte, 1);   // 立即重新开启接收，避免丢字节
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        HAL_UART_Receive_IT(huart, &rxByte, 1);
    }
}

static void RxClear(void)
{
    __disable_irq();
    rxLen = 0;
    rxBuf[0] = 0;
    __enable_irq();
}

// 等待接收缓冲区出现关键字；0 成功，-1 出现 ERROR/FAIL，-2 超时
static int WaitFor(const char *keyword, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < timeout)
    {
        if (strstr(rxBuf, keyword))
            return 0;
        if (strstr(rxBuf, "ERROR") || strstr(rxBuf, "FAIL"))
        {
            HAL_Delay(50);          // 等剩余字符收完，方便打印
            return -1;
        }
    }
    return -2;
}

int ESP32_SendCmd(const char *cmd, uint32_t timeout)
{
    RxClear();
    HAL_UART_Transmit(&huart2, (uint8_t *)cmd, strlen(cmd), 1000);
    int ret = WaitFor("OK\r\n", timeout);
    if (ret == 0)
        HAL_Delay(20);              // 等尾部数据收完
#if ESP_DEBUG
    printf("<< %s>> %s[ret=%d]\r\n", cmd, rxBuf, ret);
#endif
    return ret;
}

/* ------------------------------ 初始化 ------------------------------ */
int ESP32_Init(void)
{
    MX_USART2_UART_Init();
    RxClear();
    HAL_UART_Receive_IT(&huart2, &rxByte, 1);

    // 等 ESP 上电启动完成（STM32 比 ESP 启动快）
    HAL_Delay(2000);

    // 复位；ready 只作参考，不作为失败条件
    RxClear();
    HAL_UART_Transmit(&huart2, (uint8_t *)"AT+RST\r\n", 8, 1000);
    int r = WaitFor("ready", 5000);
    printf("reset ret=%d, rx(%d):", r, rxLen);
    for (int i = 0; i < rxLen && i < 64; i++) printf(" %02X", (uint8_t)rxBuf[i]);
    printf("\r\n");
    HAL_Delay(1000);

    // 反复探测 AT，直到模块应答
    int ok = -1;
    for (int i = 0; i < 5; i++)
    {
        if (ESP32_SendCmd("AT\r\n", 1000) == 0) { ok = 0; break; }
        HAL_Delay(500);
    }
    if (ok != 0)
    {
        printf("ESP32 no AT response\r\n");
        return -1;
    }

    if (ESP32_SendCmd("ATE0\r\n", 1000) != 0) return -1;          // 关闭回显
    if (ESP32_SendCmd("AT+CWMODE=1\r\n", 1000) != 0) return -1;   // Station 模式
    return 0;
}

/* ------------------------------ WiFi ------------------------------ */
int ESP32_WiFiConnect(const char *ssid, const char *pwd)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    return ESP32_SendCmd(cmd, 20000);
}

/* ------------------------------ 时间 ------------------------------ */
// 解析 "+CIPSNTPTIME:Fri Oct 23 22:30:00 2026"
static int ParseSntpTime(BJTime_t *t)
{
    static const char *wdays = "SunMonTueWedThuFriSat";
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char w[8], m[8];
    int day, hh, mm, ss, year;

    char *p = strstr(rxBuf, "+CIPSNTPTIME:");
    if (!p) return -1;
    p += strlen("+CIPSNTPTIME:");

    if (sscanf(p, "%3s %3s %d %d:%d:%d %d", w, m, &day, &hh, &mm, &ss, &year) != 7)
        return -1;

    char *pw = strstr(wdays, w);
    char *pm = strstr(months, m);
    if (!pw || !pm) return -1;

    t->wday  = (uint8_t)((pw - wdays) / 3);
    t->month = (uint8_t)((pm - months) / 3 + 1);
    t->day   = (uint8_t)day;
    t->hour  = (uint8_t)hh;
    t->min   = (uint8_t)mm;
    t->sec   = (uint8_t)ss;
    t->year  = (uint16_t)year;
    return 0;
}

int ESP32_GetTime(BJTime_t *t)
{
    // 使能 SNTP，时区 +8（北京时间），两个 NTP 服务器
    if (ESP32_SendCmd("AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"cn.pool.ntp.org\"\r\n", 2000) != 0)
        return -1;

    // 刚配置完需要几秒同步，同步前时间是 1970 年，轮询直到年份合理
    for (int i = 0; i < 15; i++)
    {
        HAL_Delay(1000);
        if (ESP32_SendCmd("AT+CIPSNTPTIME?\r\n", 2000) == 0)
        {
            if (ParseSntpTime(t) == 0 && t->year >= 2024)
                return 0;
        }
    }
    return -2;
}

/* ------------------------------ 天气 ------------------------------ */
// 从 s 中找 key，解析其后的数字，返回值*10；成功 0
static int ParseTenths(const char *s, const char *key, int *out)
{
    const char *p = strstr(s, key);
    if (!p) return -1;
    p += strlen(key);

    int neg = 0, ip = 0, fp = 0;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; }
    if (*p == '.')
    {
        p++;
        if (*p >= '0' && *p <= '9') fp = *p - '0';
    }
    *out = (ip * 10 + fp) * (neg ? -1 : 1);
    return 0;
}

int ESP32_GetWeather(Weather_t *w)
{
    int temp, humi, wind, code;
    char *cur;

    // 方案1：current= 多个变量。AT 指令里逗号是参数分隔符，URL 中的逗号必须写成 \,
    const char *cmd1 =
        "AT+HTTPCGET=\"https://api.open-meteo.com/v1/forecast"
        "?latitude=39.91&longitude=116.40"
        "&current=temperature_2m\\,relative_humidity_2m\\,weather_code\\,wind_speed_10m"
        "&timezone=auto\"\r\n";

    int ret = ESP32_SendCmd(cmd1, 15000);
    if (ret == 0)
    {
        // 跳过 "current_units"，定位到 "current":{ 数据段
        cur = strstr(rxBuf, "\"current\":{");
        if (cur &&
            ParseTenths(cur, "\"temperature_2m\":", &temp) == 0 &&
            ParseTenths(cur, "\"relative_humidity_2m\":", &humi) == 0 &&
            ParseTenths(cur, "\"wind_speed_10m\":", &wind) == 0 &&
            ParseTenths(cur, "\"weather_code\":", &code) == 0)
        {
            w->temp10 = (int16_t)temp;
            w->humi10 = (int16_t)humi;
            w->wind10 = (int16_t)wind;
            w->code   = (uint8_t)(code / 10);
            return 0;
        }
    }

    // 方案2（备用）：current_weather=true，URL 不含逗号，但没有湿度
    const char *cmd2 =
        "AT+HTTPCGET=\"https://api.open-meteo.com/v1/forecast"
        "?latitude=39.91&longitude=116.40&current_weather=true&timezone=auto\"\r\n";

    ret = ESP32_SendCmd(cmd2, 15000);
    if (ret != 0) return ret;

    cur = strstr(rxBuf, "\"current_weather\":{");
    if (!cur) return -3;
    if (ParseTenths(cur, "\"temperature\":", &temp) != 0) return -4;
    if (ParseTenths(cur, "\"windspeed\":", &wind) != 0) return -4;
    if (ParseTenths(cur, "\"weathercode\":", &code) != 0) return -4;

    w->temp10 = (int16_t)temp;
    w->humi10 = -10;                 // 备用接口没有湿度，-10 表示无效（-1%）
    w->wind10 = (int16_t)wind;
    w->code   = (uint8_t)(code / 10);
    return 0;
}

const char *Weather_CodeToStr(uint8_t code)
{
    if (code == 0)                  return "Clear";
    if (code <= 3)                  return "Cloudy";
    if (code == 45 || code == 48)   return "Fog";
    if (code >= 51 && code <= 57)   return "Drizzle";
    if (code >= 61 && code <= 67)   return "Rain";
    if (code >= 71 && code <= 77)   return "Snow";
    if (code >= 80 && code <= 82)   return "Showers";
    if (code == 85 || code == 86)   return "Snow showers";
    if (code >= 95)                 return "Thunderstorm";
    return "Unknown";
}