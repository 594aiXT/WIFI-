#include "ui_app.h"
#include "net_app.h"
#include "encoder.h"
#include "SHT3x.h"
#include "oled.h"
#include "ui_assets.h"
#include "i2c.h"            /* hi2c1 */
#include <stdio.h>
#include <string.h>

#define SHT_PERIOD_MS       2000U
#define STATUS_PERIOD_MS    1000U
#define CLOCK_PAGE_MS       1000U
#define ALARM_RING_MS       60000UL
#define ALARM_CATCHUP_MIN   5U      /* 阻塞跨过闹钟时刻后，最多补响 5 分钟 */
#define FOCUS_DEFAULT_MIN   25U
#define FOCUS_MIN_MINUTES   1U
#define FOCUS_MAX_MINUTES   99U

typedef enum
{
    UI_MODE_HOME = 0,
    UI_MODE_MENU,
    UI_MODE_DETAIL
} UIMode;

typedef enum
{
    FOCUS_IDLE = 0,
    FOCUS_RUNNING,
    FOCUS_PAUSED,
    FOCUS_FINISHED
} FocusState;

typedef uint8_t (*UIInputHandler)(int8_t step, EncoderButtonEvent ev);
typedef void (*UIDrawHandler)(void);

typedef struct
{
    UIPage page;
    uint8_t previous;
    uint8_t next;
    const char *name;
    const uint8_t *menuIcon;
    UIDrawHandler drawDetail;
    UIInputHandler handleDetailInput;
} UIPageDesc;

static void Draw_ClockDetail(void);
static void Draw_PageIndoor(void);
static void Draw_PageNetwork(void);
static void Draw_PageAlarm(void);
static void Draw_PageFocus(void);
static void Draw_PageAbout(void);
static uint8_t Alarm_DetailInput(int8_t step, EncoderButtonEvent ev);
static uint8_t Focus_DetailInput(int8_t step, EncoderButtonEvent ev);

/* 与参考 gui.c/h 一样用只读表描述页面关系，但不在 RAM 中维护重复索引。 */
static const UIPageDesc s_pages[UI_PAGE_COUNT] =
{
    { UI_PAGE_CLOCK,   UI_PAGE_ABOUT,   UI_PAGE_INDOOR,  "CLOCK/WX",
      ui_icon_clock_40x40,   Draw_ClockDetail,  NULL },
    { UI_PAGE_INDOOR,  UI_PAGE_CLOCK,   UI_PAGE_NETWORK, "INDOOR",
      ui_icon_indoor_40x40,  Draw_PageIndoor,   NULL },
    { UI_PAGE_NETWORK, UI_PAGE_INDOOR,  UI_PAGE_ALARM,   "WIFI",
      ui_icon_wifi_40x40,    Draw_PageNetwork,  NULL },
    { UI_PAGE_ALARM,   UI_PAGE_NETWORK, UI_PAGE_FOCUS,   "ALARM",
      ui_icon_alarm_40x40,   Draw_PageAlarm,    Alarm_DetailInput },
    { UI_PAGE_FOCUS,   UI_PAGE_ALARM,   UI_PAGE_ABOUT,   "FOCUS",
      ui_icon_focus_40x40,   Draw_PageFocus,    Focus_DetailInput },
    { UI_PAGE_ABOUT,   UI_PAGE_FOCUS,   UI_PAGE_CLOCK,   "PROJECT",
      ui_icon_project_40x40, Draw_PageAbout,    NULL }
};

static const char *const s_wdayName[7] =
    { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static UIMode  s_mode = UI_MODE_HOME;
static uint8_t s_page = UI_PAGE_CLOCK;

/* 室内温湿度（均为实际值 *10） */
static int16_t  s_temp10 = 0;
static int16_t  s_humi10 = 0;
static uint8_t  s_shtOK = 0;
static uint8_t  s_shtDirty = 1;

/* 每日软件闹钟。 */
static uint8_t  s_alarmHour = 7;
static uint8_t  s_alarmMinute = 0;
static uint8_t  s_alarmEnabled = 0;
static uint8_t  s_alarmEditing = 0;
static uint8_t  s_alarmField = 0;
static uint8_t  s_alarmRinging = 0;
static uint8_t  s_alarmDirty = 1;
static uint32_t s_alarmRingStart = 0;
static uint32_t s_alarmLastDay = 0;      /* 已触发日期和时间，防止重复响 */
static uint16_t s_alarmLastMinute = 0;
static uint32_t s_alarmCheckDay = 0;     /* 上一次有效轮询日期 */
static uint16_t s_alarmCheckMinute = 0;  /* 上一次有效轮询的当天分钟 */
static uint8_t  s_alarmClockValid = 0;

/* 专注计时使用绝对截止时间，页面切换不会暂停计时。 */
static FocusState s_focusState = FOCUS_IDLE;
static uint8_t  s_focusMinutes = FOCUS_DEFAULT_MIN;
static uint32_t s_focusRemaining = FOCUS_DEFAULT_MIN * 60UL;
static uint32_t s_focusDeadline = 0;
static uint32_t s_focusShownSecond = 0xFFFFFFFFUL;
static uint8_t  s_focusDirty = 1;

static uint32_t s_nextSht = 0;
static uint32_t s_nextStatus = 0;
static uint32_t s_nextClockPage = 0;

/* 首页和菜单状态图标缓存，避免无变化时重复绘制。 */
static uint8_t s_homeWifiState = 0xFFU;
static uint8_t s_homeAlarmState = 0xFFU;
static uint8_t s_menuWifiState = 0xFFU;
static uint8_t s_menuAlarmState = 0xFFU;
static FocusState s_menuFocusState = (FocusState)0xFFU;

/* ------------------------------ 工具函数 ------------------------------ */
static int Due(uint32_t deadline)
{
    return (int32_t)(HAL_GetTick() - deadline) >= 0;
}

static void ShowRowN(uint8_t row, uint8_t width, const char *s)
{
    char line[17];
    if (width > 16U) width = 16U;
    snprintf(line, sizeof(line), "%-*.*s", (int)width, (int)width, s);
    OLED_ShowString(0, (uint8_t)(row * 2), line);
}

static void ShowRow(uint8_t row, const char *s)
{
    ShowRowN(row, 16U, s);
}

static void ShowCentered(uint8_t row, const char *s)
{
    uint8_t length = (uint8_t)strlen(s);
    uint8_t x = 0;
    if (length < 16U) x = (uint8_t)((16U - length) * 4U);
    ShowRow(row, "");
    OLED_ShowString(x, (uint8_t)(row * 2), s);
}

static void ShowTitle(const char *title, const uint8_t *statusIcon)
{
    ShowRowN(0, 14U, title);
    OLED_Fill(112, 0, 127, 7, 0);
    if (statusIcon != NULL) OLED_DrawBitmap(120, 0, 8, 8, statusIcon);
}

static uint8_t CurrentAlarmIconState(void)
{
    if (s_alarmRinging) return 2U;
    return s_alarmEnabled ? 1U : 0U;
}

static const uint8_t *WifiStatusIcon(void)
{
    NetStatus_t st;
    NetApp_GetStatus(&st);
    return st.wifiOK ? ui_status_wifi_on_8x8 : ui_status_wifi_off_8x8;
}

static const uint8_t *AlarmStatusIcon(void)
{
    uint8_t state = CurrentAlarmIconState();
    if (state == 2U) return ui_status_alarm_ring_8x8;
    return state ? ui_status_alarm_on_8x8 : ui_status_alarm_off_8x8;
}

static const uint8_t *FocusStatusIcon(void)
{
    if (s_focusState == FOCUS_RUNNING) return ui_status_focus_run_8x8;
    if (s_focusState == FOCUS_PAUSED) return ui_status_focus_pause_8x8;
    if (s_focusState == FOCUS_FINISHED) return ui_status_focus_done_8x8;
    return NULL;
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
        s_shtOK = 1;
    }
    else
    {
        s_shtOK = 0;
    }
    s_shtDirty = 1;
}

/* ------------------------------ 首页与图标菜单 ------------------------------ */
static void Draw_HomeStatus(void)
{
    NetStatus_t st;
    NetApp_GetStatus(&st);

    OLED_Fill(112, 0, 127, 7, 0);
    OLED_DrawBitmap(112, 0, 8, 8,
                    st.wifiOK ? ui_status_wifi_on_8x8 : ui_status_wifi_off_8x8);
    OLED_DrawBitmap(120, 0, 8, 8, AlarmStatusIcon());
    OLED_DrawBitmap(112, 48, 16, 16, ui_icon_menu_16x16);
    s_homeWifiState = st.wifiOK;
    s_homeAlarmState = CurrentAlarmIconState();
}

static void Draw_MenuStatus(void)
{
    NetStatus_t st;
    const uint8_t *icon = NULL;

    NetApp_GetStatus(&st);
    if (s_page == UI_PAGE_NETWORK) icon = st.wifiOK ? ui_status_wifi_on_8x8 :
                                                     ui_status_wifi_off_8x8;
    else if (s_page == UI_PAGE_ALARM) icon = AlarmStatusIcon();
    else if (s_page == UI_PAGE_FOCUS) icon = FocusStatusIcon();

    OLED_Fill(88, 4, 95, 11, 0);       /* 主图标右外侧，避免挖掉图标 */
    if (icon != NULL) OLED_DrawBitmap(88, 4, 8, 8, icon);
    s_menuWifiState = st.wifiOK;
    s_menuAlarmState = CurrentAlarmIconState();
    s_menuFocusState = s_focusState;
}

static void Draw_Menu(void)
{
    OLED_Clear();
    OLED_DrawBitmap(44, 4, 40, 40, s_pages[s_page].menuIcon);
    OLED_DrawBitmap(4, 18, 12, 12, ui_icon_left_12x12);
    OLED_DrawBitmap(112, 18, 12, 12, ui_icon_right_12x12);
    ShowCentered(3, s_pages[s_page].name);
    Draw_MenuStatus();
}

/* ------------------------------ 功能详情页 ------------------------------ */
static void Draw_ClockDetail(void)
{
    char buf[28];
    BJTime_t t;
    Weather_t w;

    OLED_Clear();
    snprintf(buf, sizeof(buf), "WX:%s", NetApp_GetWeatherText());
    ShowRow(0, buf);
    if (NetApp_GetTime(&t) == 0)
    {
        snprintf(buf, sizeof(buf), "%04u-%02u-%02u %s",
                 (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
                 s_wdayName[t.wday % 7U]);
        ShowRow(1, buf);
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                 (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec);
        ShowRow(2, buf);
    }
    else
    {
        ShowRow(1, "----/--/-- ---");
        ShowRow(2, "--:--:--");
    }

    if (NetApp_GetWeather(&w) == 0)
    {
        int t10 = w.temp10;
        char sign = (t10 < 0) ? '-' : ' ';
        char humi[8];
        if (t10 < 0) t10 = -t10;
        if (w.humi10 >= 0) snprintf(humi, sizeof(humi), "%d", w.humi10 / 10);
        else snprintf(humi, sizeof(humi), "--");
        snprintf(buf, sizeof(buf), "%c%d.%dC H%s W%d",
                 sign, t10 / 10, t10 % 10, humi, w.wind10 / 10);
        ShowRow(3, buf);
    }
    else
    {
        ShowRow(3, "--.-C H-- W--");
    }
}

static void Draw_PageIndoor(void)
{
    char buf[24];
    OLED_Clear();
    ShowTitle("INDOOR / SHT30", NULL);
    if (s_shtOK)
    {
        int t = s_temp10;
        char sign = (t < 0) ? '-' : ' ';
        if (t < 0) t = -t;
        snprintf(buf, sizeof(buf), "Temp:%c%d.%d C", sign, t / 10, t % 10);
        ShowRow(1, buf);
        snprintf(buf, sizeof(buf), "Humi: %d.%d %%", s_humi10 / 10, s_humi10 % 10);
        ShowRow(2, buf);
        ShowRow(3, "Sensor:OK");
    }
    else
    {
        ShowRow(1, "Temp: --.- C");
        ShowRow(2, "Humi: --.- %");
        ShowRow(3, "Sensor:ERROR");
    }
    s_shtDirty = 0;
}

static void Draw_PageNetwork(void)
{
    char buf[24];
    NetStatus_t st;
    NetApp_GetStatus(&st);

    OLED_Clear();
    ShowTitle("WIFI STATUS", WifiStatusIcon());
    snprintf(buf, sizeof(buf), "ESP32:%s", st.espOK ? "OK" : "NG");
    ShowRow(1, buf);
    snprintf(buf, sizeof(buf), "WiFi:%s", st.wifiOK ? "ON" : "OFF");
    ShowRow(2, buf);
    snprintf(buf, sizeof(buf), "Time:%s Wthr:%s",
             st.timeValid ? "OK" : "--", st.weatherValid ? "OK" : "--");
    ShowRow(3, buf);
}

static void Draw_PageAlarm(void)
{
    char buf[24];
    BJTime_t now;

    OLED_Clear();
    if (s_alarmRinging)
    {
        ShowTitle("!!! ALARM !!!", ui_status_alarm_ring_8x8);
        if (NetApp_GetTime(&now) == 0)
        {
            snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                     (unsigned)now.hour, (unsigned)now.min, (unsigned)now.sec);
            ShowRow(1, buf);
        }
        else ShowRow(1, "--:--:--");
        ShowRow(2, "Time to wake up!");
        ShowRow(3, "Click:Stop");
        s_alarmDirty = 0;
        return;
    }

    ShowTitle(s_alarmEditing ? "ALARM EDIT" : "ALARM", AlarmStatusIcon());
    snprintf(buf, sizeof(buf), "Time: %02u:%02u",
             (unsigned)s_alarmHour, (unsigned)s_alarmMinute);
    ShowRow(1, buf);
    snprintf(buf, sizeof(buf), "State:%s", s_alarmEnabled ? "ON DAILY" : "OFF");
    ShowRow(2, buf);
    ShowRow(3, s_alarmEditing ? "Click:Next" : "Click:Set");

    if (s_alarmEditing)
    {
        if (s_alarmField == 0) OLED_InvertRect(48, 16, 16, 16);
        else if (s_alarmField == 1) OLED_InvertRect(72, 16, 16, 16);
        else OLED_InvertRect(48, 32, s_alarmEnabled ? 64U : 24U, 16);
    }
    s_alarmDirty = 0;
}

static uint32_t Focus_RemainingSeconds(void)
{
    if (s_focusState == FOCUS_RUNNING)
    {
        uint32_t now = HAL_GetTick();
        if (Due(s_focusDeadline)) return 0;
        return (uint32_t)((s_focusDeadline - now + 999U) / 1000U);
    }
    return s_focusRemaining;
}

static void Draw_PageFocus(void)
{
    char buf[24];
    uint32_t remaining = Focus_RemainingSeconds();

    OLED_Clear();
    ShowTitle("FOCUS", FocusStatusIcon());
    snprintf(buf, sizeof(buf), "%02lu:%02lu %s",
             (unsigned long)(remaining / 60U),
             (unsigned long)(remaining % 60U),
             s_focusState == FOCUS_RUNNING ? "RUN" :
             s_focusState == FOCUS_PAUSED ? "PAUSE" :
             s_focusState == FOCUS_FINISHED ? "DONE" : "READY");
    ShowRow(1, buf);

    if (s_focusState == FOCUS_IDLE)
    {
        snprintf(buf, sizeof(buf), "Set:%umin Turn", (unsigned)s_focusMinutes);
        ShowRow(2, buf);
        ShowRow(3, "Click:Start");
    }
    else if (s_focusState == FOCUS_FINISHED)
    {
        ShowRow(2, "Focus complete!");
        ShowRow(3, "Click:Reset");
    }
    else
    {
        ShowRow(2, s_focusState == FOCUS_RUNNING ? "Keep focusing" : "Paused");
        ShowRow(3, s_focusState == FOCUS_RUNNING ? "Click:Pause" : "Click:Resume");
    }
    s_focusShownSecond = remaining;
    s_focusDirty = 0;
}

static void Draw_PageAbout(void)
{
    OLED_Clear();
    ShowRow(0, "PROJECT");
    ShowRow(1, "MCU:STM32F103C8");
    ShowRow(2, "OLED:SH1106 12864");
    ShowRow(3, "FW:v1.2 ICON GUI");
}

/* ------------------------------ 视图切换 ------------------------------ */
static void EnterHome(void)
{
    NetApp_SetDisplay(0);
    OLED_Clear();
    s_mode = UI_MODE_HOME;
    s_homeWifiState = 0xFFU;
    s_homeAlarmState = 0xFFU;
    NetApp_SetDisplay(1);       /* 仅默认首页由 net_app 绘制 */
    Draw_HomeStatus();
}

static void EnterMenu(uint8_t page)
{
    NetApp_SetDisplay(0);
    s_mode = UI_MODE_MENU;
    s_page = (page < UI_PAGE_COUNT) ? page : UI_PAGE_CLOCK;
    s_menuWifiState = 0xFFU;
    s_menuAlarmState = 0xFFU;
    s_menuFocusState = (FocusState)0xFFU;
    Draw_Menu();
}

static void EnterDetail(uint8_t page)
{
    NetApp_SetDisplay(0);
    s_mode = UI_MODE_DETAIL;
    s_page = (page < UI_PAGE_COUNT) ? page : UI_PAGE_CLOCK;
    s_alarmDirty = 1;
    s_focusDirty = 1;
    s_pages[s_page].drawDetail();
    s_nextStatus = HAL_GetTick() + STATUS_PERIOD_MS;
    s_nextClockPage = HAL_GetTick() + CLOCK_PAGE_MS;
}

/* ------------------------------ 后台状态机 ------------------------------ */
static void Alarm_Process(void)
{
    BJTime_t now;
    uint32_t day;
    uint16_t nowMinute;
    uint16_t scheduledMinute;
    uint8_t due = 0;

    if (s_alarmRinging && Due(s_alarmRingStart + ALARM_RING_MS))
    {
        s_alarmRinging = 0;
        s_alarmDirty = 1;
    }
    if (NetApp_GetTime(&now) != 0) return;

    day = (uint32_t)now.year * 10000UL + (uint32_t)now.month * 100UL + now.day;
    nowMinute = (uint16_t)(now.hour * 60U + now.min);
    scheduledMinute = (uint16_t)(s_alarmHour * 60U + s_alarmMinute);

    /* 普通轮询按分钟命中；若联网阻塞跨过设定分钟，在 5 分钟内补响一次。
     * RTC 向后跳变或跨过更久的时间则不补响，避免上电后误报旧闹钟。 */
    if (s_alarmClockValid)
    {
        if (day == s_alarmCheckDay && nowMinute >= s_alarmCheckMinute)
        {
            uint16_t elapsed = (uint16_t)(nowMinute - s_alarmCheckMinute);
            if (nowMinute == scheduledMinute ||
                (elapsed <= ALARM_CATCHUP_MIN &&
                 scheduledMinute > s_alarmCheckMinute &&
                 scheduledMinute <= nowMinute))
            {
                due = 1;
            }
        }
        else if (day != s_alarmCheckDay && nowMinute == scheduledMinute)
        {
            due = 1;
        }
    }
    else if (nowMinute == scheduledMinute)
    {
        due = 1;
    }

    s_alarmCheckDay = day;
    s_alarmCheckMinute = nowMinute;
    s_alarmClockValid = 1;

    if (due && s_alarmEnabled && !s_alarmRinging &&
        (day != s_alarmLastDay || scheduledMinute != s_alarmLastMinute))
    {
        s_alarmLastDay = day;
        s_alarmLastMinute = scheduledMinute;
        s_alarmRinging = 1;
        s_alarmRingStart = HAL_GetTick();
        s_alarmEditing = 0;
        EnterDetail(UI_PAGE_ALARM);
    }
}

static void Focus_Process(void)
{
    if (s_focusState == FOCUS_RUNNING && Due(s_focusDeadline))
    {
        s_focusRemaining = 0;
        s_focusState = FOCUS_FINISHED;
        s_focusDirty = 1;
        if (!s_alarmRinging) EnterDetail(UI_PAGE_FOCUS);
    }
}

static void Focus_Reset(void)
{
    s_focusState = FOCUS_IDLE;
    s_focusRemaining = (uint32_t)s_focusMinutes * 60UL;
    s_focusShownSecond = 0xFFFFFFFFUL;
    s_focusDirty = 1;
}

static void Focus_Confirm(void)
{
    uint32_t now = HAL_GetTick();
    if (s_focusState == FOCUS_RUNNING)
    {
        s_focusRemaining = Focus_RemainingSeconds();
        s_focusState = FOCUS_PAUSED;
    }
    else if (s_focusState == FOCUS_PAUSED)
    {
        s_focusDeadline = now + s_focusRemaining * 1000UL;
        s_focusState = FOCUS_RUNNING;
    }
    else if (s_focusState == FOCUS_FINISHED)
    {
        Focus_Reset();
    }
    else
    {
        s_focusDeadline = now + s_focusRemaining * 1000UL;
        s_focusState = FOCUS_RUNNING;
    }
    s_focusDirty = 1;
}

/* ------------------------------ 输入处理 ------------------------------ */
static uint8_t Alarm_DetailInput(int8_t step, EncoderButtonEvent ev)
{
    if (!s_alarmEditing)
    {
        if (ev == ENCODER_BUTTON_CONFIRM)
        {
            s_alarmEditing = 1;
            s_alarmField = 0;
            s_alarmDirty = 1;
            return 1;
        }
        return (ev == ENCODER_BUTTON_BACK) ? 0U : 1U;
    }

    if (ev == ENCODER_BUTTON_BACK)
    {
        s_alarmEditing = 0;     /* 长按只退出编辑，不穿透到页面返回 */
        s_alarmDirty = 1;
        return 1;
    }
    if (step != 0)
    {
        if (s_alarmField == 0)
            s_alarmHour = (uint8_t)((s_alarmHour + (step > 0 ? 1 : 23)) % 24U);
        else if (s_alarmField == 1)
            s_alarmMinute = (uint8_t)((s_alarmMinute + (step > 0 ? 1 : 59)) % 60U);
        else
            s_alarmEnabled = !s_alarmEnabled;
        s_alarmDirty = 1;
    }
    if (ev == ENCODER_BUTTON_CONFIRM)
    {
        if (s_alarmField < 2U) s_alarmField++;
        else s_alarmEditing = 0;
        s_alarmDirty = 1;
    }
    return 1;
}

static uint8_t Focus_DetailInput(int8_t step, EncoderButtonEvent ev)
{
    if (ev == ENCODER_BUTTON_BACK) return 0;
    if (ev == ENCODER_BUTTON_CONFIRM)
    {
        Focus_Confirm();
    }
    else if (s_focusState == FOCUS_IDLE && step != 0)
    {
        if (step > 0)
            s_focusMinutes = (s_focusMinutes >= FOCUS_MAX_MINUTES) ?
                             FOCUS_MIN_MINUTES : (uint8_t)(s_focusMinutes + 1U);
        else
            s_focusMinutes = (s_focusMinutes <= FOCUS_MIN_MINUTES) ?
                             FOCUS_MAX_MINUTES : (uint8_t)(s_focusMinutes - 1U);
        Focus_Reset();
    }
    return 1;
}

static void HandleInput(int8_t step, EncoderButtonEvent ev)
{
    UIInputHandler handler;

    if (s_alarmRinging)
    {
        if (ev == ENCODER_BUTTON_CONFIRM)
        {
            s_alarmRinging = 0;
            s_alarmDirty = 1;
            EnterDetail(s_focusState == FOCUS_FINISHED ?
                        UI_PAGE_FOCUS : UI_PAGE_ALARM);
        }
        else if (ev == ENCODER_BUTTON_BACK)
        {
            s_alarmRinging = 0;
            s_alarmDirty = 1;
            EnterMenu(UI_PAGE_ALARM);
        }
        return;
    }

    if (s_mode == UI_MODE_HOME)
    {
        if (ev == ENCODER_BUTTON_CONFIRM) EnterMenu(s_page);
        return;
    }

    if (s_mode == UI_MODE_MENU)
    {
        if (ev == ENCODER_BUTTON_BACK) EnterHome();
        else if (ev == ENCODER_BUTTON_CONFIRM) EnterDetail(s_page);
        else if (step > 0) EnterMenu(s_pages[s_page].next);
        else if (step < 0) EnterMenu(s_pages[s_page].previous);
        return;
    }

    handler = s_pages[s_page].handleDetailInput;
    if (handler != NULL && handler(step, ev) != 0U) return;
    if (ev == ENCODER_BUTTON_BACK) EnterMenu(s_page);
}

/* ------------------------------ 周期刷新 ------------------------------ */
static void Refresh_StatusIcons(void)
{
    NetStatus_t st;
    uint8_t alarmState = CurrentAlarmIconState();
    NetApp_GetStatus(&st);

    if (s_mode == UI_MODE_HOME)
    {
        if (st.wifiOK != s_homeWifiState || alarmState != s_homeAlarmState)
            Draw_HomeStatus();
    }
    else if (s_mode == UI_MODE_MENU &&
             (st.wifiOK != s_menuWifiState || alarmState != s_menuAlarmState ||
              s_focusState != s_menuFocusState))
    {
        Draw_MenuStatus();
    }
}

/* ------------------------------ 对外接口 ------------------------------ */
void UI_Init(void)
{
    Encoder_Init();
    NetApp_Init();
    Sht_Update();
    s_nextSht = HAL_GetTick() + SHT_PERIOD_MS;
    EnterHome();
    OLED_Refresh_Gram();
}

void UI_Process(void)
{
    int16_t delta;
    EncoderButtonEvent ev;
    int8_t step = 0;

    Encoder_ButtonTask();
    delta = Encoder_Get();
    if (delta > 0) step = 1;
    else if (delta < 0) step = -1;
    ev = Encoder_GetButtonEvent();
    HandleInput(step, ev);

    /* ESP-AT 忙等会暂停 UI，但绝对截止时间不会丢失计时时长。 */
    NetApp_Process();
    Alarm_Process();
    Focus_Process();

    if (Due(s_nextSht))
    {
        Sht_Update();
        s_nextSht = HAL_GetTick() + SHT_PERIOD_MS;
    }

    if (s_mode == UI_MODE_DETAIL)
    {
        if (s_page == UI_PAGE_CLOCK && Due(s_nextClockPage))
        {
            Draw_ClockDetail();
            s_nextClockPage = HAL_GetTick() + CLOCK_PAGE_MS;
        }
        else if (s_page == UI_PAGE_INDOOR && s_shtDirty)
        {
            Draw_PageIndoor();
        }
        else if (s_page == UI_PAGE_NETWORK && Due(s_nextStatus))
        {
            Draw_PageNetwork();
            s_nextStatus = HAL_GetTick() + STATUS_PERIOD_MS;
        }
        else if (s_page == UI_PAGE_ALARM &&
                 (s_alarmDirty || (s_alarmRinging && Due(s_nextClockPage))))
        {
            Draw_PageAlarm();
            s_nextClockPage = HAL_GetTick() + CLOCK_PAGE_MS;
        }
        else if (s_page == UI_PAGE_FOCUS &&
                 (s_focusDirty || Focus_RemainingSeconds() != s_focusShownSecond))
        {
            Draw_PageFocus();
        }
    }

    Refresh_StatusIcons();
    OLED_Refresh_Gram();       /* 一帧只提交一次，无变化时不发送 SPI */
}
