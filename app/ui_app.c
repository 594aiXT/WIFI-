#include "ui_app.h"
#include "net_app.h"
#include "encoder.h"
#include "SHT3x.h"
#include "oled.h"
#include "i2c.h"            /* hi2c1 */
#include <stdio.h>

#define SHT_PERIOD_MS      2000U    /* 室内温湿度采样周期     */
#define STATUS_PERIOD_MS   1000U    /* 状态页刷新周期         */

static uint8_t  s_page = 0;

/* 室内温湿度（均为实际值 *10） */
static int16_t  s_temp10 = 0;
static int16_t  s_humi10 = 0;
static uint8_t  s_shtOK  = 0;
static uint8_t  s_shtDirty = 1;

static uint32_t s_nextSht    = 0;
static uint32_t s_nextStatus = 0;

/* ------------------------------ 工具函数 ------------------------------ */
static int Due(uint32_t deadline)
{
    return (int32_t)(HAL_GetTick() - deadline) >= 0;
}

/* 一行 16 个字符（8x16 字体，行号 0~3），不足补空格 */
static void ShowRow(uint8_t row, const char *s)
{
    char line[17];
    snprintf(line, sizeof(line), "%-16s", s);
    OLED_ShowString(0, (uint8_t)(row * 2), line);
}

static void ShowPageNo(void)
{
    char buf[20];
    snprintf(buf, sizeof(buf), "Page %u/%u", s_page + 1U, (unsigned)UI_PAGE_COUNT);
    ShowRow(3, buf);
}

/* ------------------------------ SHT30 ------------------------------ */
static int RoundTenths(float v)
{
    return (int)(v * 10.0f + (v >= 0.0f ? 0.5f : -0.5f));
}

static void Sht_Update(void)
{
    float t, h;

    if (SHT3x_ReadTemperatureHumidity(&hi2c1, &t, &h) == SHT3X_STATUS_OK)
    {
        s_temp10 = (int16_t)RoundTenths(t);
        s_humi10 = (int16_t)RoundTenths(h);
        s_shtOK  = 1;
    }
    else
    {
        s_shtOK = 0;
    }
    s_shtDirty = 1;
}

/* ------------------------------ 页面绘制 ------------------------------ */
static void Draw_PageIndoor(void)
{
    char buf[24];

    ShowRow(0, "-- INDOOR --");

    if (s_shtOK)
    {
        int t = s_temp10;
        char sign = (t < 0) ? '-' : ' ';
        if (t < 0) t = -t;
        snprintf(buf, sizeof(buf), "Temp:%c%d.%d C", sign, t / 10, t % 10);
        ShowRow(1, buf);

        snprintf(buf, sizeof(buf), "Humi: %d.%d %%", s_humi10 / 10, s_humi10 % 10);
        ShowRow(2, buf);
    }
    else
    {
        ShowRow(1, "Temp: --.- C");
        ShowRow(2, "Humi: --.- %");
    }

    ShowPageNo();
    s_shtDirty = 0;
}

static void Draw_PageNetwork(void)
{
    char buf[24];
    NetStatus_t st;

    NetApp_GetStatus(&st);

    ShowRow(0, "-- NETWORK --");

    snprintf(buf, sizeof(buf), "ESP:%s WiFi:%s",
             st.espOK ? "OK" : "NG", st.wifiOK ? "ON" : "OFF");
    ShowRow(1, buf);

    snprintf(buf, sizeof(buf), "Wthr:%s Time:%s",
             st.weatherValid ? "OK" : "--", st.timeValid ? "OK" : "--");
    ShowRow(2, buf);

    ShowPageNo();
}

/* 进入某一页：清屏后整页重绘 */
static void EnterPage(uint8_t page)
{
    s_page = page;
    OLED_Clear();

    switch (s_page)
    {
    case 0:
        NetApp_SetDisplay(1);           /* 时间天气页由 net_app 绘制 */
        break;
    case 1:
        NetApp_SetDisplay(0);
        Draw_PageIndoor();
        break;
    default:
        NetApp_SetDisplay(0);
        Draw_PageNetwork();
        s_nextStatus = HAL_GetTick() + STATUS_PERIOD_MS;
        break;
    }
}

/* ------------------------------ 对外接口 ------------------------------ */
void UI_Init(void)
{
    Encoder_Init();

    NetApp_Init();                      /* DS1302 + ESP32 + WiFi + 校时 + 天气，期间显示进度 */

    Sht_Update();                       /* 先读一次，进入室内页就有数据 */
    s_nextSht = HAL_GetTick() + SHT_PERIOD_MS;

    EnterPage(0);
}

void UI_Process(void)
{
    int16_t delta;
    EncoderButtonEvent ev;
    int8_t  step = 0;
    uint8_t target = s_page;
    uint8_t changed = 0;

    /* 1. 输入：按键消抖必须持续调用 */
    Encoder_ButtonTask();

    delta = Encoder_Get();
    if (delta > 0)       step = 1;      /* 顺时针：下一页 */
    else if (delta < 0)  step = -1;     /* 逆时针：上一页 */

    ev = Encoder_GetButtonEvent();
    if (ev == ENCODER_BUTTON_CONFIRM)   /* 短按：下一页 */
        step = 1;

    if (step > 0)
    {
        target = (uint8_t)((s_page + 1U) % UI_PAGE_COUNT);
        changed = 1;
    }
    else if (step < 0)
    {
        target = (uint8_t)((s_page + UI_PAGE_COUNT - 1U) % UI_PAGE_COUNT);
        changed = 1;
    }

    if (ev == ENCODER_BUTTON_BACK)      /* 长按：回首页 */
    {
        if (s_page != 0)
        {
            target = 0;
            changed = 1;
        }
    }

    if (changed)
        EnterPage(target);

    /* 2. 联网模块：本页为时间天气页时由它负责刷新；其余时间只在后台跑 */
    NetApp_Process();

    /* 3. 室内温湿度采样 */
    if (Due(s_nextSht))
    {
        Sht_Update();
        s_nextSht = HAL_GetTick() + SHT_PERIOD_MS;
    }

    /* 4. 当前页刷新 */
    if (s_page == 1 && s_shtDirty)
    {
        Draw_PageIndoor();
    }
    else if (s_page == 2 && Due(s_nextStatus))
    {
        Draw_PageNetwork();
        s_nextStatus = HAL_GetTick() + STATUS_PERIOD_MS;
    }
}
