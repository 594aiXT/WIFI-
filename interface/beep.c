#include "beep.h"
#include "tim.h"                    /* htim2（CubeMX 生成） */

/* ============================ 硬件配置 ============================
 * 默认：蜂鸣器信号脚接 TIM2_CH1（PA0）。
 * 如果你接在别的定时器/通道，只需改下面几个宏。
 * ================================================================== */
#define BEEP_TIM          htim2
#define BEEP_CHANNEL      TIM_CHANNEL_1
#define BEEP_TIM_PSC      71U       /* 定时器时钟 72MHz，分频后计数 1MHz */
#define BEEP_IDLE_HIGH    1         /* 0：静音时输出低电平（高电平触发模块）
                                       1：静音时输出高电平（低电平触发模块） */

typedef struct
{
    uint16_t freq;                  /* Hz，0 表示休止 */
    uint16_t ms;
} Note_t;

typedef struct
{
    const Note_t *notes;
    uint8_t       len;
    uint8_t       loop;
} Pattern_t;

static const Note_t kClick[]     = {{2000, 25}};
static const Note_t kOk[]        = {{1500, 80}, {0, 40}, {2000, 120}};
static const Note_t kStart[]     = {{1500, 70}, {0, 30}, {2200, 110}};
static const Note_t kPause[]     = {{2000, 60}, {0, 30}, {1400, 90}};
static const Note_t kSnooze[]    = {{1800, 100}, {0, 80}, {1800, 100}};

/* 闹钟三个阶段：越往后越急促，频率也更高 */
static const Note_t kAlarm1[]    = {{1800, 150}, {0, 850}};
static const Note_t kAlarm2[]    = {{2000, 120}, {0, 80}, {2000, 120}, {0, 680}};
static const Note_t kAlarm3[]    = {{2400, 100}, {0, 60}, {2400, 100}, {0, 60},
                                    {2400, 100}, {0, 60}, {3000, 250}, {0, 350}};

static const Note_t kFocusDone[] = {{1319, 160}, {1568, 160}, {2093, 320}, {0, 900}};
static const Note_t kBreakDone[] = {{2093, 160}, {1568, 160}, {1319, 320}, {0, 900}};

#define ARRAY_LEN(a)  ((uint8_t)(sizeof(a) / sizeof((a)[0])))

static const Pattern_t kPatterns[BEEP_PATTERN_COUNT] =
{
    {kClick,     ARRAY_LEN(kClick),     0},   /* BEEP_CLICK      */
    {kOk,        ARRAY_LEN(kOk),        0},   /* BEEP_OK         */
    {kStart,     ARRAY_LEN(kStart),     0},   /* BEEP_START      */
    {kPause,     ARRAY_LEN(kPause),     0},   /* BEEP_PAUSE      */
    {kSnooze,    ARRAY_LEN(kSnooze),    0},   /* BEEP_SNOOZE     */
    {kAlarm1,    ARRAY_LEN(kAlarm1),    1},   /* BEEP_ALARM_1    */
    {kAlarm2,    ARRAY_LEN(kAlarm2),    1},   /* BEEP_ALARM_2    */
    {kAlarm3,    ARRAY_LEN(kAlarm3),    1},   /* BEEP_ALARM_3    */
    {kFocusDone, ARRAY_LEN(kFocusDone), 1},   /* BEEP_FOCUS_DONE */
    {kBreakDone, ARRAY_LEN(kBreakDone), 1},   /* BEEP_BREAK_DONE */
};

static const Pattern_t *s_pat     = 0;
static uint8_t          s_idx     = 0;
static uint8_t          s_playing = 0;
static uint32_t         s_noteEnd = 0;

/* ------------------------------ 底层 ------------------------------ */
void Beep_Tone(uint16_t freq_hz)
{
    if (freq_hz < 100U)   freq_hz = 100U;
    if (freq_hz > 20000U) freq_hz = 20000U;

    uint32_t arr = 1000000UL / freq_hz - 1U;
    __HAL_TIM_SET_AUTORELOAD(&BEEP_TIM, arr);
    __HAL_TIM_SET_COMPARE(&BEEP_TIM, BEEP_CHANNEL, (arr + 1U) / 2U);   /* 50% 占空比 */
    __HAL_TIM_SET_COUNTER(&BEEP_TIM, 0U);                              /* 避免计数值大于新 ARR */
}

void Beep_Off(void)
{
    __HAL_TIM_SET_AUTORELOAD(&BEEP_TIM, 999U);
#if BEEP_IDLE_HIGH
    __HAL_TIM_SET_COMPARE(&BEEP_TIM, BEEP_CHANNEL, 1000U);             /* CCR>ARR：恒高 */
#else
    __HAL_TIM_SET_COMPARE(&BEEP_TIM, BEEP_CHANNEL, 0U);                /* CCR=0：恒低   */
#endif
    __HAL_TIM_SET_COUNTER(&BEEP_TIM, 0U);
}

void Beep_Init(void)
{
    __HAL_TIM_SET_PRESCALER(&BEEP_TIM, BEEP_TIM_PSC);
    Beep_Off();
    HAL_TIM_PWM_Start(&BEEP_TIM, BEEP_CHANNEL);
    BEEP_TIM.Instance->EGR = TIM_EGR_UG;        /* 立即装载预分频和比较值 */
}

/* ---------------------------- 非阻塞播放 ---------------------------- */
static void StartNote(void)
{
    const Note_t *n = &s_pat->notes[s_idx];

    if (n->freq != 0U) Beep_Tone(n->freq);
    else               Beep_Off();

    s_noteEnd = HAL_GetTick() + n->ms;
}

void Beep_Play(BeepPattern_t p)
{
    if (p >= BEEP_PATTERN_COUNT) return;

    s_pat     = &kPatterns[p];
    s_idx     = 0;
    s_playing = 1;
    StartNote();
}

void Beep_Stop(void)
{
    s_playing = 0;
    Beep_Off();
}

uint8_t Beep_IsPlaying(void)
{
    return s_playing;
}

void Beep_Process(void)
{
    if (!s_playing) return;
    if ((int32_t)(HAL_GetTick() - s_noteEnd) < 0) return;

    if (++s_idx >= s_pat->len)
    {
        if (s_pat->loop)
        {
            s_idx = 0;
        }
        else
        {
            Beep_Stop();
            return;
        }
    }
    StartNote();
}
