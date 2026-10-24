#include "ds1302.h"

/* ==================== 引脚定义 ==================== */

#define DS1302_CE_PORT       GPIOA
#define DS1302_CE_PIN        GPIO_PIN_11

#define DS1302_SCLK_PORT     GPIOA
#define DS1302_SCLK_PIN      GPIO_PIN_8

#define DS1302_IO_PORT       GPIOB
#define DS1302_IO_PIN        GPIO_PIN_4

#define CE_L() \
    HAL_GPIO_WritePin(DS1302_CE_PORT, DS1302_CE_PIN, GPIO_PIN_RESET)

#define CE_H() \
    HAL_GPIO_WritePin(DS1302_CE_PORT, DS1302_CE_PIN, GPIO_PIN_SET)

#define SCLK_L() \
    HAL_GPIO_WritePin(DS1302_SCLK_PORT, DS1302_SCLK_PIN, GPIO_PIN_RESET)

#define SCLK_H() \
    HAL_GPIO_WritePin(DS1302_SCLK_PORT, DS1302_SCLK_PIN, GPIO_PIN_SET)

#define DATA_L() \
    HAL_GPIO_WritePin(DS1302_IO_PORT, DS1302_IO_PIN, GPIO_PIN_RESET)

#define DATA_H() \
    HAL_GPIO_WritePin(DS1302_IO_PORT, DS1302_IO_PIN, GPIO_PIN_SET)

#define DATA_Read() \
    HAL_GPIO_ReadPin(DS1302_IO_PORT, DS1302_IO_PIN)


/* ==================== 全局变量 ==================== */

struct TIMEData TimeData;
u8 read_time[7];


/* ==================== 微秒延时 ==================== */

/*
 * STM32F103 使用 Cortex-M3。
 * DWT->CYCCNT 是向上计数的 CPU 周期计数器。
 *
 * 注意：DWT 必须在支持该功能的内核及调试配置下可用。
 */
static void ds1302_dwt_init(void)
{
    static uint8_t initialized = 0U;

    if (initialized == 0U)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        initialized = 1U;
    }
}


void delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t ticks;
    uint32_t cycles_per_us;

    if (us == 0U)
    {
        return;
    }

    ds1302_dwt_init();

    cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
    ticks = cycles_per_us * us;

    start = DWT->CYCCNT;

    while ((uint32_t)(DWT->CYCCNT - start) < ticks)
    {
        /* 等待指定的 CPU 周期数 */
    }
}


/* ==================== GPIO 初始化 ==================== */

void ds1302_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /*
     * 关闭 JTAG，保留 SWD。
     * PB4 是 JTAG 相关引脚，需要释放后用作普通 GPIO。
     */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    /* PA11 = CE，PA8 = SCLK */
    GPIO_InitStructure.Pin = DS1302_CE_PIN | DS1302_SCLK_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStructure.Pull = GPIO_NOPULL;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    CE_L();
    SCLK_L();

    /* PB4 = 双向数据线，初始配置为输出 */
    ds1032_DATAOUT_init();

    DATA_L();

    delay_us(2U);
}


/* DATA 配置为推挽输出 */
void ds1032_DATAOUT_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /*
     * 先将输出寄存器置低，再切换模式，
     * 避免从输入切换为输出时输出意外的高电平。
     */
    HAL_GPIO_WritePin(
        DS1302_IO_PORT,
        DS1302_IO_PIN,
        GPIO_PIN_RESET
    );

    GPIO_InitStructure.Pin = DS1302_IO_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStructure.Pull = GPIO_NOPULL;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(DS1302_IO_PORT, &GPIO_InitStructure);
}


/* DATA 配置为输入，供 DS1302 驱动数据线 */
void ds1032_DATAINPUT_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.Pin = DS1302_IO_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT;
    GPIO_InitStructure.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(DS1302_IO_PORT, &GPIO_InitStructure);
}


/* ==================== 底层串行通信 ==================== */

/*
 * 发送一字节，低位先传输。
 *
 * 调用前需要：
 * 1. CE 已经拉高；
 * 2. DATA 已配置为输出；
 * 3. SCLK 处于低电平或上一位已结束。
 *
 * 数据在 SCLK 上升沿被 DS1302 采样。
 */
static void ds1302_shift_out(u8 data)
{
    u8 count;

    for (count = 0U; count < 8U; count++)
    {
        SCLK_L();

        if ((data & 0x01U) != 0U)
        {
            DATA_H();
        }
        else
        {
            DATA_L();
        }

        delay_us(1U);

        SCLK_H();
        delay_us(1U);

        data >>= 1;
    }
}


/*
 * 接收一字节，低位先接收。
 *
 * DS1302 在 SCLK 下降沿输出数据。
 * 因此在下降沿之后、下一个上升沿之前读取 IO。
 */
static u8 ds1302_shift_in(void)
{
    u8 count;
    u8 data = 0U;

    for (count = 0U; count < 8U; count++)
    {
        SCLK_L();
        delay_us(1U);

        if (DATA_Read() == GPIO_PIN_SET)
        {
            data |= (u8)(1U << count);
        }

        delay_us(1U);

        SCLK_H();
        delay_us(1U);
    }

    return data;
}


/* 兼容原来的底层发送接口 */
void ds1302_write_onebyte(u8 data)
{
    ds1032_DATAOUT_init();
    ds1302_shift_out(data);
}


/* ==================== 寄存器读写 ==================== */

/*
 * 向指定寄存器写入一个字节。
 *
 * address 使用 DS1302 原生命令地址：
 * 秒写地址 0x80，分写地址 0x82，等等。
 */
void ds1302_wirte_rig(u8 address, u8 data)
{
    CE_L();
    SCLK_L();

    ds1032_DATAOUT_init();
    DATA_L();

    delay_us(2U);

    CE_H();
    delay_us(2U);

    ds1302_shift_out(address);
    ds1302_shift_out(data);

    SCLK_L();
    delay_us(2U);

    /*
     * DS1302 在 CE 下降沿结束传输并提交写入。
     */
    CE_L();

    delay_us(2U);
}


/*
 * 从指定寄存器读取一个字节。
 *
 * address 使用 DS1302 原生读命令地址：
 * 秒读地址 0x81，分读地址 0x83，等等。
 */
u8 ds1302_read_rig(u8 address)
{
    u8 data;

    CE_L();
    SCLK_L();

    ds1032_DATAOUT_init();
    DATA_L();

    delay_us(2U);

    CE_H();
    delay_us(2U);

    /* 发送读命令 */
    ds1302_shift_out(address);

    /*
     * 命令结束后释放 DATA 引脚，
     * 随后由 DS1302 输出数据。
     */
    ds1032_DATAINPUT_init();

    data = ds1302_shift_in();

    SCLK_L();
    delay_us(1U);

    CE_L();

    /*
     * 预先设置输出寄存器为低电平。
     * 目前仍是输入模式，不会驱动总线。
     */
    HAL_GPIO_WritePin(
        DS1302_IO_PORT,
        DS1302_IO_PIN,
        GPIO_PIN_RESET
    );

    return data;
}


/* ==================== BCD 数据转换 ==================== */

static u8 ds1302_bcd_to_bin(u8 bcd)
{
    return (u8)(((bcd >> 4) & 0x0FU) * 10U +
                 (bcd & 0x0FU));
}


static u8 ds1302_bin_to_bcd(u8 bin)
{
    return (u8)(((bin / 10U) << 4) |
                 (bin % 10U));
}


/* ==================== 初始化 ==================== */

/*
 * 初始化 RTC 的基本状态。
 *
 * 不再写入固定日期，避免每次 MCU 复位都覆盖 RTC 时间。
 * 如果秒寄存器的 CH 位为 1，则清零以启动时钟。
 */
void ds1032_init(void)
{
    u8 seconds;

    /* 关闭写保护 */
    ds1302_wirte_rig(0x8E, 0x00);

    seconds = ds1302_read_rig(0x81);

    /* 秒寄存器 bit7 为 CH，置 1 表示停止振荡 */
    if ((seconds & 0x80U) != 0U)
    {
        ds1302_wirte_rig(0x80, seconds & 0x7FU);
    }

    /* 重新开启写保护 */
    ds1302_wirte_rig(0x8E, 0x80);
}


/* ==================== 设置时间 ==================== */

/*
 * 参数中的 year 使用完整年份，例如 2026。
 * hour 使用 24 小时制，week 范围为 1~7。
 *
 * 返回值：
 * 1 = 输入合法，写入完成
 * 0 = 输入参数不合法
 */
u8 ds1302_set_time(const struct TIMEData *time)
{
    if (time == NULL)
    {
        return 0U;
    }

    if ((time->year < 2000U) || (time->year > 2099U) ||
        (time->month < 1U) || (time->month > 12U) ||
        (time->day < 1U) || (time->day > 31U) ||
        (time->hour > 23U) ||
        (time->minute > 59U) ||
        (time->second > 59U) ||
        (time->week < 1U) || (time->week > 7U))
    {
        return 0U;
    }

    ds1302_wirte_rig(0x8E, 0x00);  /* 关闭写保护 */

    /*
     * 秒寄存器 bit7 = CH。
     * 这里写入 0，确保时钟运行。
     */
    ds1302_wirte_rig(
        0x80,
        ds1302_bin_to_bcd(time->second) & 0x7FU
    );

    ds1302_wirte_rig(0x82, ds1302_bin_to_bcd(time->minute));
    ds1302_wirte_rig(0x84, ds1302_bin_to_bcd(time->hour));
    ds1302_wirte_rig(0x86, ds1302_bin_to_bcd(time->day));
    ds1302_wirte_rig(0x88, ds1302_bin_to_bcd(time->month));
    ds1302_wirte_rig(0x8A, time->week);
    ds1302_wirte_rig(0x8C, ds1302_bin_to_bcd((u8)(time->year % 100U)));

    ds1302_wirte_rig(0x8E, 0x80);  /* 开启写保护 */

    return 1U;
}


/* ==================== 读取时间 ==================== */

/*
 * DS1302 时钟突发读取命令为 0xBF。
 * 返回顺序依次为：
 * 秒、分、时、日、月、星期、年、写保护。
 *
 * 采用一次突发读取，可以避免分别读取寄存器时
 * 秒值恰好翻转导致各字段来自不同时间点。
 */
void ds1032_read_time(void)
{
    u8 i;
    u8 burst_data[8];

    CE_L();
    SCLK_L();

    ds1032_DATAOUT_init();
    DATA_L();

    delay_us(2U);

    CE_H();
    delay_us(2U);

    /* 时钟与日历突发读取命令 */
    ds1302_shift_out(0xBF);

    ds1032_DATAINPUT_init();

    for (i = 0U; i < 8U; i++)
    {
        burst_data[i] = ds1302_shift_in();
    }

    SCLK_L();
    delay_us(1U);

    CE_L();

    HAL_GPIO_WritePin(
        DS1302_IO_PORT,
        DS1302_IO_PIN,
        GPIO_PIN_RESET
    );

    for (i = 0U; i < 7U; i++)
    {
        read_time[i] = burst_data[i];
    }
}


/* 将读取到的 BCD 数据转换为十进制时间 */
void ds1032_read_realTime(void)
{
    u8 raw_hour;

    ds1032_read_time();

    TimeData.second = ds1302_bcd_to_bin(
        read_time[0] & 0x7FU
    );

    TimeData.minute = ds1302_bcd_to_bin(
        read_time[1] & 0x7FU
    );

    raw_hour = read_time[2];

    /*
     * bit7=1：12 小时制；
     * bit5：PM 标志。
     * bit7=0：24 小时制。
     */
    if ((raw_hour & 0x80U) != 0U)
    {
        u8 hour12;
        u8 is_pm;

        hour12 = ds1302_bcd_to_bin(raw_hour & 0x1FU);
        is_pm = (raw_hour & 0x20U) != 0U;

        if (hour12 == 12U)
        {
            hour12 = 0U;
        }

        TimeData.hour = (u8)(hour12 + (is_pm ? 12U : 0U));
    }
    else
    {
        TimeData.hour = ds1302_bcd_to_bin(raw_hour & 0x3FU);
    }

    TimeData.day = ds1302_bcd_to_bin(
        read_time[3] & 0x3FU
    );

    TimeData.month = ds1302_bcd_to_bin(
        read_time[4] & 0x1FU
    );

    TimeData.week = read_time[5] & 0x07U;

    TimeData.year = (u16)(
        2000U + ds1302_bcd_to_bin(read_time[6])
    );
}