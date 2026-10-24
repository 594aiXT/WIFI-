
#include "oled.h"
#include "oledfont.h"
#include <string.h>

/* CubeMX 生成的 SPI1 句柄 */
extern SPI_HandleTypeDef hspi1;

/* ============================================================
 * OLED 引脚定义
 *
 * PA5 = DO
 * PA7 = DI
 * PA4 = CS
 * PB0 = RST
 * PB1 = DC
 *
 * 注意：PA5、PA7 由 SPI1 外设管理，不再使用 GPIO 模拟 SPI。
 * ============================================================ */

#define OLED_CS_CLR()    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define OLED_CS_SET()    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)

#define OLED_RST_CLR()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET)
#define OLED_RST_SET()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET)

#define OLED_DC_CLR()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET)
#define OLED_DC_SET()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET)

/* SPI 发送超时时间，单位 ms */
#define OLED_SPI_TIMEOUT 100U

/* SH1106 的可见区域通常需要 2 列偏移 */
#if OLED_USE_SH1106
#define OLED_COL_OFFSET  2
#else
#define OLED_COL_OFFSET  0
#endif

/* OLED 显存：8 页，每页 128 字节 */
static uint8_t OLED_GRAM[OLED_PAGES][OLED_WIDTH];


/* ============================================================
 * OLED 控制引脚初始化
 *
 * PA4：CS
 * PB0：RST
 * PB1：DC
 *
 * PA5、PA7 不在这里初始化，由 SPI1 初始化函数配置。
 * ============================================================ */
static void OLED_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PA4：片选 CS */
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB0：RST，PB1：DC */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* 初始化输出状态 */
    OLED_CS_SET();
    OLED_RST_SET();
    OLED_DC_SET();
}

/* ============================================================
 * 通过硬件 SPI1 发送一个字节
 *
 * cmd = OLED_CMD  ：命令
 * cmd = OLED_DATA ：显存数据
 *
 * 保留原来的函数接口，方便兼容旧驱动。
 * ============================================================ */
void OLED_WR_Byte(uint8_t dat, uint8_t cmd)
{
    /* 设置 DC：0 为命令，1 为数据 */
    if (cmd == OLED_DATA)
    {
        OLED_DC_SET();
    }
    else
    {
        OLED_DC_CLR();
    }

    /* 选中 OLED */
    OLED_CS_CLR();

    /* 通过硬件 SPI1 发送 1 字节 */
    if (HAL_SPI_Transmit(&hspi1,
                         &dat,
                         1,
                         OLED_SPI_TIMEOUT) != HAL_OK)
    {
        /* 发送失败也要释放片选 */
        OLED_CS_SET();
        OLED_DC_SET();
        return;
    }

    /* 释放 OLED */
    OLED_CS_SET();
    OLED_DC_SET();
}


/* ============================================================
 * 设置 OLED 写入位置
 *
 * x：列坐标 0~127
 * y：页坐标 0~7，每页 8 像素高
 * ============================================================ */
void OLED_Set_Pos(uint8_t x, uint8_t y)
{
    uint16_t col;

    if (x >= OLED_WIDTH || y >= OLED_PAGES)
    {
        return;
    }

    col = (uint16_t)x + OLED_COL_OFFSET;

    OLED_WR_Byte((uint8_t)(0xB0 + y), OLED_CMD);
    OLED_WR_Byte((uint8_t)(((col >> 4) & 0x0F) | 0x10), OLED_CMD);
    OLED_WR_Byte((uint8_t)(col & 0x0F), OLED_CMD);
}


/* ============================================================
 * 刷新整个显存到 OLED
 *
 * 每页先设置地址，再连续发送 128 字节。
 * 相比每个字节单独调用 OLED_WR_Byte()，效率更高。
 * ============================================================ */
void OLED_Refresh_Gram(void)
{
    uint8_t page;

    for (page = 0; page < OLED_PAGES; page++)
    {
        /* 设置当前页起始列和页地址 */
        OLED_Set_Pos(0, page);

        /* 接下来发送的是显示数据 */
        OLED_DC_SET();

        /* 选中 OLED */
        OLED_CS_CLR();

        /* 连续发送当前页的 128 字节 */
        if (HAL_SPI_Transmit(&hspi1,
                             &OLED_GRAM[page][0],
                             OLED_WIDTH,
                             OLED_SPI_TIMEOUT) != HAL_OK)
        {
            OLED_CS_SET();
            OLED_DC_SET();
            return;
        }

        /* 释放片选 */
        OLED_CS_SET();
    }

    OLED_DC_SET();
}


/* ============================================================
 * 开启 / 关闭显示
 * ============================================================ */
void OLED_Display_On(void)
{
    OLED_WR_Byte(0xAF, OLED_CMD);
}

void OLED_Display_Off(void)
{
    OLED_WR_Byte(0xAE, OLED_CMD);
}


/* ============================================================
 * 清屏：清除缓冲区，再刷新 OLED
 * ============================================================ */
void OLED_Clear(void)
{
    memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
    OLED_Refresh_Gram();
}


/* ============================================================
 * 初始化 OLED
 *
 * 请确保调用 OLED_Init() 前已执行 HAL_Init()，
 * 并完成系统时钟配置，使 HAL_Delay() 正常工作。
 * ============================================================ */
void OLED_Init(void)
{
    OLED_GPIO_Init();

    /* 硬件复位 */
    OLED_RST_SET();
    HAL_Delay(100);

    OLED_RST_CLR();
    HAL_Delay(20);

    OLED_RST_SET();
    HAL_Delay(100);

    OLED_WR_Byte(0xAE, OLED_CMD); /* 关闭显示 */

    OLED_WR_Byte(0xD5, OLED_CMD);
    OLED_WR_Byte(0x80, OLED_CMD); /* 显示时钟 */

    OLED_WR_Byte(0xA8, OLED_CMD);
    OLED_WR_Byte(0x3F, OLED_CMD); /* 1/64 占空比 */

    OLED_WR_Byte(0xD3, OLED_CMD);
    OLED_WR_Byte(0x00, OLED_CMD); /* 显示偏移 */

    OLED_WR_Byte(0x40, OLED_CMD); /* 起始显示行 */
    OLED_WR_Byte(0xA1, OLED_CMD); /* 列扫描方向 */
    OLED_WR_Byte(0xC8, OLED_CMD); /* 行扫描方向 */

    OLED_WR_Byte(0xDA, OLED_CMD);
    OLED_WR_Byte(0x12, OLED_CMD); /* COM 引脚配置 */

    OLED_WR_Byte(0x81, OLED_CMD);
    OLED_WR_Byte(0xCF, OLED_CMD); /* 对比度 */

    OLED_WR_Byte(0xD9, OLED_CMD);
#if OLED_USE_SH1106
    OLED_WR_Byte(0x22, OLED_CMD); /* SH1106 常用预充电配置 */
#else
    OLED_WR_Byte(0xF1, OLED_CMD); /* SSD1306 预充电配置 */
#endif

    OLED_WR_Byte(0xDB, OLED_CMD);
#if OLED_USE_SH1106
    OLED_WR_Byte(0x35, OLED_CMD); /* SH1106 VCOMH */
#else
    OLED_WR_Byte(0x40, OLED_CMD); /* SSD1306 VCOMH */
#endif

    OLED_WR_Byte(0xA4, OLED_CMD); /* 显示 RAM 内容 */
    OLED_WR_Byte(0xA6, OLED_CMD); /* 正常显示，非反显 */

#if OLED_USE_SH1106
    /* SH1106 的内部 DC-DC 控制 */
    OLED_WR_Byte(0xAD, OLED_CMD);
    OLED_WR_Byte(0x8B, OLED_CMD);
#else
    /* SSD1306 的电荷泵设置 */
    OLED_WR_Byte(0x8D, OLED_CMD);
    OLED_WR_Byte(0x14, OLED_CMD);

    /* SSD1306 页寻址模式 */
    OLED_WR_Byte(0x20, OLED_CMD);
    OLED_WR_Byte(0x02, OLED_CMD);
#endif

    /* 先清屏，再打开显示 */
    OLED_Clear();
    OLED_Display_On();
}


/* ============================================================
 * 以下绘图函数操作显存，绘制完成后刷新显示。
 * ============================================================ */

/* 画点
 * x：0~127，y：0~63
 * t：非 0 点亮，0 清除
 */
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t)
{
    uint8_t page;
    uint8_t bit_mask;

    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
    {
        return;
    }

    page = y / 8;
    bit_mask = (uint8_t)(1U << (y % 8));

    if (t != 0)
    {
        OLED_GRAM[page][x] |= bit_mask;
    }
    else
    {
        OLED_GRAM[page][x] &= (uint8_t)(~bit_mask);
    }

    OLED_Refresh_Gram();
}


/* 填充矩形，坐标范围均为像素坐标，包含终点
 * dot != 0：点亮矩形
 * dot == 0：清除矩形
 */
void OLED_Fill(uint8_t x1, uint8_t y1,
               uint8_t x2, uint8_t y2, uint8_t dot)
{
    uint16_t x;
    uint16_t y;
    uint8_t page;
    uint8_t bit_mask;

    if (x1 > x2 || y1 > y2)
    {
        return;
    }

    if (x1 >= OLED_WIDTH || y1 >= OLED_HEIGHT)
    {
        return;
    }

    if (x2 >= OLED_WIDTH)
    {
        x2 = OLED_WIDTH - 1;
    }

    if (y2 >= OLED_HEIGHT)
    {
        y2 = OLED_HEIGHT - 1;
    }

    for (y = y1; y <= y2; y++)
    {
        page = (uint8_t)(y / 8);
        bit_mask = (uint8_t)(1U << (y % 8));

        for (x = x1; x <= x2; x++)
        {
            if (dot != 0)
            {
                OLED_GRAM[page][x] |= bit_mask;
            }
            else
            {
                OLED_GRAM[page][x] &= (uint8_t)(~bit_mask);
            }
        }
    }

    OLED_Refresh_Gram();
}


/* ============================================================
 * 字符绘制内部函数：只修改显存，不立即刷新。
 * 外部函数完成一组绘制后统一刷新，以减少重复刷新。
 * 依赖原来的 oledfont.h：
 *   F8X16、F6x8、Hzk
 * ============================================================ */
static void OLED_DrawCharToGram(uint8_t x, uint8_t y, uint8_t chr)
{
    uint8_t i;
    uint8_t c;

    /* 字库通常包含 ASCII 可打印字符 */
    if (chr < ' ' || chr > '~')
    {
        chr = '?';
    }

    c = (uint8_t)(chr - ' ');

#if SIZE == 16

    /* 16 高字体占用两页 */
    if (y >= OLED_PAGES - 1)
    {
        return;
    }

    if ((uint16_t)x + 8 > OLED_WIDTH)
    {
        return;
    }

    for (i = 0; i < 8; i++)
    {
        OLED_GRAM[y][x + i]     = F8X16[c * 16 + i];
        OLED_GRAM[y + 1][x + i] = F8X16[c * 16 + i + 8];
    }

#else

    /* 6x8 字体占用一页 */
    if (y >= OLED_PAGES)
    {
        return;
    }

    if ((uint16_t)x + 6 > OLED_WIDTH)
    {
        return;
    }

    for (i = 0; i < 6; i++)
    {
        OLED_GRAM[y][x + i] = F6x8[c][i];
    }

#endif
}


/* ============================================================
 * 显示一个字符
 * x：列坐标
 * y：页坐标，不是 0~63 的像素 y 坐标
 * ============================================================ */
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr)
{
#if SIZE == 16
    if ((uint16_t)x + 8 > OLED_WIDTH)
    {
        x = 0;
        y += 2;
    }
#else
    if ((uint16_t)x + 6 > OLED_WIDTH)
    {
        x = 0;
        y += 1;
    }
#endif

    OLED_DrawCharToGram(x, y, chr);
    OLED_Refresh_Gram();
}


/* 计算 m 的 n 次方 */
static uint32_t OLED_Pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while (n-- != 0)
    {
        result *= m;
    }

    return result;
}


/* ============================================================
 * 显示十进制整数
 * len：显示位数
 * size：保留旧接口参数，通常使用 16
 * ============================================================ */
void OLED_ShowNum(uint8_t x, uint8_t y,
                  uint32_t num, uint8_t len, uint8_t size)
{
    uint8_t t;
    uint8_t temp;
    uint8_t started = 0;
    uint16_t char_x;
    uint32_t divisor;

    if (len == 0 || len > 10)
    {
        return;
    }

    for (t = 0; t < len; t++)
    {
        divisor = OLED_Pow(10, (uint8_t)(len - t - 1));
        temp = (uint8_t)((num / divisor) % 10);

        if (started == 0 && t < len - 1 && temp == 0)
        {
            char_x = (uint16_t)x + ((uint16_t)(size / 2) * t);

            if (char_x < OLED_WIDTH)
            {
                OLED_DrawCharToGram((uint8_t)char_x, y, ' ');
            }

            continue;
        }

        started = 1;
        char_x = (uint16_t)x + ((uint16_t)(size / 2) * t);

        if (char_x < OLED_WIDTH)
        {
            OLED_DrawCharToGram((uint8_t)char_x, y,
                                (uint8_t)('0' + temp));
        }
    }

    OLED_Refresh_Gram();
}


/* ============================================================
 * 显示字符串
 * y 使用页坐标。
 * 16 高字体每换一行 y 增加 2。
 * ============================================================ */
void OLED_ShowString(uint8_t x, uint8_t y, const char *p)
{
    uint16_t next_x;

    if (p == NULL)
    {
        return;
    }

    while (*p != '\0')
    {
#if SIZE == 16
        if ((uint16_t)x + 8 > OLED_WIDTH)
        {
            x = 0;
            y += 2;
        }

        if (y >= OLED_PAGES - 1)
        {
            break;
        }

        OLED_DrawCharToGram(x, y, (uint8_t)*p);
        next_x = (uint16_t)x + 8;
#else
        if ((uint16_t)x + 6 > OLED_WIDTH)
        {
            x = 0;
            y += 1;
        }

        if (y >= OLED_PAGES)
        {
            break;
        }

        OLED_DrawCharToGram(x, y, (uint8_t)*p);
        next_x = (uint16_t)x + 6;
#endif

        x = (uint8_t)next_x;
        p++;
    }

    OLED_Refresh_Gram();
}


/* ============================================================
 * 显示一个 16x16 汉字
 * 与原字库格式一致：
 * Hzk[2*no]   为上半部分
 * Hzk[2*no+1] 为下半部分
 * ============================================================ */
void OLED_ShowCHinese(uint8_t x, uint8_t y, uint8_t no)
{
    uint8_t i;

    if ((uint16_t)x + 16 > OLED_WIDTH || y >= OLED_PAGES - 1)
    {
        return;
    }

    for (i = 0; i < 16; i++)
    {
        OLED_GRAM[y][x + i]     = Hzk[2 * no][i];
        OLED_GRAM[y + 1][x + i] = Hzk[2 * no + 1][i];
    }

    OLED_Refresh_Gram();
}


/* ============================================================
 * 显示 BMP 点阵
 * 参数格式沿用原工程：
 *   x0、x1：起始列、结束列（结束列不包含）
 *   y0、y1：起始页、结束页（结束页不包含）
 * 每页 8 像素高，图片数据为逐页存放的字节。
 * ============================================================ */
void OLED_DrawBMP(uint8_t x0, uint8_t y0,
                  uint8_t x1, uint8_t y1, const uint8_t BMP[])
{
    uint8_t page;
    uint8_t col;
    uint16_t j = 0;

    if (BMP == NULL ||
        x0 >= x1 || y0 >= y1 ||
        x0 >= OLED_WIDTH || y0 >= OLED_PAGES)
    {
        return;
    }

    if (x1 > OLED_WIDTH)
    {
        x1 = OLED_WIDTH;
    }

    if (y1 > OLED_PAGES)
    {
        y1 = OLED_PAGES;
    }

    for (page = y0; page < y1; page++)
    {
        for (col = x0; col < x1; col++)
        {
            OLED_GRAM[page][col] = BMP[j++];
        }
    }

    OLED_Refresh_Gram();
}
