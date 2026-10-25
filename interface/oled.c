
#include "oled.h"
#include "oledfont.h"
#include <string.h>

/* CubeMX ���ɵ� SPI1 ��� */
extern SPI_HandleTypeDef hspi1;

/* ============================================================
 * OLED ���Ŷ���
 *
 * PA5 = DO
 * PA7 = DI
 * PA4 = CS
 * PB0 = RST
 * PB1 = DC
 *
 * ע�⣺PA5��PA7 �� SPI1 �������������ʹ�� GPIO ģ�� SPI��
 * ============================================================ */

#define OLED_CS_CLR()    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define OLED_CS_SET()    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)

#define OLED_RST_CLR()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET)
#define OLED_RST_SET()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET)

#define OLED_DC_CLR()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET)
#define OLED_DC_SET()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET)

/* SPI ���ͳ�ʱʱ�䣬��λ ms */
#define OLED_SPI_TIMEOUT 100U

/* SH1106 �Ŀɼ�����ͨ����Ҫ 2 ��ƫ�� */
#if OLED_USE_SH1106
#define OLED_COL_OFFSET  2
#else
#define OLED_COL_OFFSET  0
#endif

/* OLED �Դ棺8 ҳ��ÿҳ 128 �ֽ� */
static uint8_t OLED_GRAM[OLED_PAGES][OLED_WIDTH];
static uint8_t s_gramDirty = 1;


/* ============================================================
 * OLED �������ų�ʼ��
 *
 * PA4��CS
 * PB0��RST
 * PB1��DC
 *
 * PA5��PA7 ���������ʼ������ SPI1 ��ʼ���������á�
 * ============================================================ */
static void OLED_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PA4��Ƭѡ CS */
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB0��RST��PB1��DC */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* ��ʼ�����״̬ */
    OLED_CS_SET();
    OLED_RST_SET();
    OLED_DC_SET();
}

/* ============================================================
 * ͨ��Ӳ�� SPI1 ����һ���ֽ�
 *
 * cmd = OLED_CMD  ������
 * cmd = OLED_DATA ���Դ�����
 *
 * ����ԭ���ĺ����ӿڣ�������ݾ�������
 * ============================================================ */
HAL_StatusTypeDef OLED_WR_Byte(uint8_t dat, uint8_t cmd)
{
    /* ���� DC��0 Ϊ���1 Ϊ���� */
    if (cmd == OLED_DATA)
    {
        OLED_DC_SET();
    }
    else
    {
        OLED_DC_CLR();
    }

    /* ѡ�� OLED */
    OLED_CS_CLR();

    /* ͨ��Ӳ�� SPI1 ���� 1 �ֽ� */
    if (HAL_SPI_Transmit(&hspi1,
                         &dat,
                         1,
                         OLED_SPI_TIMEOUT) != HAL_OK)
    {
        /* ����ʧ��ҲҪ�ͷ�Ƭѡ */
        OLED_CS_SET();
        OLED_DC_SET();
        return HAL_ERROR;
    }

    /* �ͷ� OLED */
    OLED_CS_SET();
    OLED_DC_SET();
    return HAL_OK;
}


/* ============================================================
 * ���� OLED д��λ��
 *
 * x�������� 0~127
 * y��ҳ���� 0~7��ÿҳ 8 ���ظ�
 * ============================================================ */
HAL_StatusTypeDef OLED_Set_Pos(uint8_t x, uint8_t y)
{
    uint16_t col;

    if (x >= OLED_WIDTH || y >= OLED_PAGES)
    {
        return HAL_ERROR;
    }

    col = (uint16_t)x + OLED_COL_OFFSET;

    if (OLED_WR_Byte((uint8_t)(0xB0 + y), OLED_CMD) != HAL_OK ||
        OLED_WR_Byte((uint8_t)(((col >> 4) & 0x0F) | 0x10), OLED_CMD) != HAL_OK ||
        OLED_WR_Byte((uint8_t)(col & 0x0F), OLED_CMD) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return HAL_OK;
}


/* ============================================================
 * ˢ�������Դ浽 OLED
 *
 * ÿҳ�����õ�ַ������������ 128 �ֽڡ�
 * ���ÿ���ֽڵ������� OLED_WR_Byte()��Ч�ʸ��ߡ�
 * ============================================================ */
void OLED_Refresh_Gram(void)
{
    uint8_t page;

    if (s_gramDirty == 0U)
    {
        return;
    }

    for (page = 0; page < OLED_PAGES; page++)
    {
        /* 地址命令失败时保留 dirty，下一轮重试整帧。 */
        if (OLED_Set_Pos(0, page) != HAL_OK)
        {
            OLED_DC_SET();
            return;
        }

        /* ���������͵�����ʾ���� */
        OLED_DC_SET();

        /* ѡ�� OLED */
        OLED_CS_CLR();

        /* �������͵�ǰҳ�� 128 �ֽ� */
        if (HAL_SPI_Transmit(&hspi1,
                             &OLED_GRAM[page][0],
                             OLED_WIDTH,
                             OLED_SPI_TIMEOUT) != HAL_OK)
        {
            OLED_CS_SET();
            OLED_DC_SET();
            return;
        }

        /* �ͷ�Ƭѡ */
        OLED_CS_SET();
    }

    OLED_DC_SET();
    s_gramDirty = 0;
}


/* ============================================================
 * ���� / �ر���ʾ
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
 * �������������������ˢ�� OLED
 * ============================================================ */
void OLED_Clear(void)
{
    memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
    s_gramDirty = 1;
}


/* ============================================================
 * ��ʼ�� OLED
 *
 * ��ȷ������ OLED_Init() ǰ��ִ�� HAL_Init()��
 * �����ϵͳʱ�����ã�ʹ HAL_Delay() ����������
 * ============================================================ */
void OLED_Init(void)
{
    OLED_GPIO_Init();

    /* Ӳ����λ */
    OLED_RST_SET();
    HAL_Delay(100);

    OLED_RST_CLR();
    HAL_Delay(20);

    OLED_RST_SET();
    HAL_Delay(100);

    OLED_WR_Byte(0xAE, OLED_CMD); /* �ر���ʾ */

    OLED_WR_Byte(0xD5, OLED_CMD);
    OLED_WR_Byte(0x80, OLED_CMD); /* ��ʾʱ�� */

    OLED_WR_Byte(0xA8, OLED_CMD);
    OLED_WR_Byte(0x3F, OLED_CMD); /* 1/64 ռ�ձ� */

    OLED_WR_Byte(0xD3, OLED_CMD);
    OLED_WR_Byte(0x00, OLED_CMD); /* ��ʾƫ�� */

    OLED_WR_Byte(0x40, OLED_CMD); /* ��ʼ��ʾ�� */
    OLED_WR_Byte(0xA1, OLED_CMD); /* ��ɨ�跽�� */
    OLED_WR_Byte(0xC8, OLED_CMD); /* ��ɨ�跽�� */

    OLED_WR_Byte(0xDA, OLED_CMD);
    OLED_WR_Byte(0x12, OLED_CMD); /* COM �������� */

    OLED_WR_Byte(0x81, OLED_CMD);
    OLED_WR_Byte(0xCF, OLED_CMD); /* �Աȶ� */

    OLED_WR_Byte(0xD9, OLED_CMD);
#if OLED_USE_SH1106
    OLED_WR_Byte(0x22, OLED_CMD); /* SH1106 ����Ԥ������� */
#else
    OLED_WR_Byte(0xF1, OLED_CMD); /* SSD1306 Ԥ������� */
#endif

    OLED_WR_Byte(0xDB, OLED_CMD);
#if OLED_USE_SH1106
    OLED_WR_Byte(0x35, OLED_CMD); /* SH1106 VCOMH */
#else
    OLED_WR_Byte(0x40, OLED_CMD); /* SSD1306 VCOMH */
#endif

    OLED_WR_Byte(0xA4, OLED_CMD); /* ��ʾ RAM ���� */
    OLED_WR_Byte(0xA6, OLED_CMD); /* ������ʾ���Ƿ��� */

#if OLED_USE_SH1106
    /* SH1106 ���ڲ� DC-DC ���� */
    OLED_WR_Byte(0xAD, OLED_CMD);
    OLED_WR_Byte(0x8B, OLED_CMD);
#else
    /* SSD1306 �ĵ�ɱ����� */
    OLED_WR_Byte(0x8D, OLED_CMD);
    OLED_WR_Byte(0x14, OLED_CMD);

    /* SSD1306 ҳѰַģʽ */
    OLED_WR_Byte(0x20, OLED_CMD);
    OLED_WR_Byte(0x02, OLED_CMD);
#endif

    /* ���������ٴ���ʾ */
    OLED_Clear();
    OLED_Refresh_Gram();
    OLED_Display_On();
}


/* ============================================================
 * ���»�ͼ���������Դ棬������ɺ�ˢ����ʾ��
 * ============================================================ */

/* ����
 * x��0~127��y��0~63
 * t���� 0 ������0 ���
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

    s_gramDirty = 1;
}


/* �����Σ����귶Χ��Ϊ�������꣬�����յ�
 * dot != 0����������
 * dot == 0���������
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

    s_gramDirty = 1;
}


/* ============================================================
 * �ַ������ڲ�������ֻ�޸��Դ棬������ˢ�¡�
 * �ⲿ�������һ����ƺ�ͳһˢ�£��Լ����ظ�ˢ�¡�
 * ����ԭ���� oledfont.h��
 *   F8X16��F6x8��Hzk
 * ============================================================ */
static void OLED_DrawCharToGram(uint8_t x, uint8_t y, uint8_t chr)
{
    uint8_t i;
    uint8_t c;

    /* �ֿ�ͨ������ ASCII �ɴ�ӡ�ַ� */
    if (chr < ' ' || chr > '~')
    {
        chr = '?';
    }

    c = (uint8_t)(chr - ' ');

#if SIZE == 16

    /* 16 ������ռ����ҳ */
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

    /* 6x8 ����ռ��һҳ */
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
 * ��ʾһ���ַ�
 * x��������
 * y��ҳ���꣬���� 0~63 ������ y ����
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
    s_gramDirty = 1;
}


/* ���� m �� n �η� */
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
 * ��ʾʮ��������
 * len����ʾλ��
 * size�������ɽӿڲ�����ͨ��ʹ�� 16
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

    s_gramDirty = 1;
}


/* ============================================================
 * ��ʾ�ַ���
 * y ʹ��ҳ���ꡣ
 * 16 ������ÿ��һ�� y ���� 2��
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

    s_gramDirty = 1;
}


/* ============================================================
 * ��ʾһ�� 16x16 ����
 * ��ԭ�ֿ��ʽһ�£�
 * Hzk[2*no]   Ϊ�ϰ벿��
 * Hzk[2*no+1] Ϊ�°벿��
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

    s_gramDirty = 1;
}


/* ============================================================
 * ��ʾ BMP ����
 * ������ʽ����ԭ���̣�
 *   x0��x1����ʼ�С������У������в�������
 *   y0��y1����ʼҳ������ҳ������ҳ��������
 * ÿҳ 8 ���ظߣ�ͼƬ����Ϊ��ҳ��ŵ��ֽڡ�
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

    s_gramDirty = 1;
}

/* 任意像素位置绘制单色位图。
 * 数据格式：列优先、垂直字节、bit0 在上，1 表示点亮。
 * 源图中的 0 也会清除目标像素，因此不会残留上一帧图标。 */
void OLED_DrawBitmap(int16_t x, int16_t y,
                     uint8_t width, uint8_t height,
                     const uint8_t *bitmap)
{
    uint8_t sx;
    uint8_t sy;
    uint8_t bytesPerColumn;
    uint8_t changed = 0;

    if (bitmap == NULL || width == 0U || height == 0U)
    {
        return;
    }

    bytesPerColumn = (uint8_t)((height + 7U) / 8U);
    for (sx = 0; sx < width; sx++)
    {
        int16_t dx = (int16_t)(x + sx);
        if (dx < 0 || dx >= OLED_WIDTH)
        {
            continue;
        }

        for (sy = 0; sy < height; sy++)
        {
            int16_t dy = (int16_t)(y + sy);
            uint16_t sourceIndex;
            uint8_t sourceBit;
            uint8_t mask;

            if (dy < 0 || dy >= OLED_HEIGHT)
            {
                continue;
            }

            sourceIndex = (uint16_t)sx * bytesPerColumn + (uint16_t)(sy / 8U);
            sourceBit = (uint8_t)(1U << (sy % 8U));
            mask = (uint8_t)(1U << (dy % 8));
            if ((bitmap[sourceIndex] & sourceBit) != 0U)
            {
                OLED_GRAM[dy / 8][dx] |= mask;
            }
            else
            {
                OLED_GRAM[dy / 8][dx] &= (uint8_t)(~mask);
            }
            changed = 1;
        }
    }

    if (changed != 0U)
    {
        s_gramDirty = 1;
    }
}

/* 将指定像素区域反白；重复调用两次可恢复原图。 */
void OLED_InvertRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    uint16_t xEnd = (uint16_t)x + width;
    uint16_t yEnd = (uint16_t)y + height;
    uint16_t px;
    uint16_t py;

    if (width == 0U || height == 0U || x >= OLED_WIDTH || y >= OLED_HEIGHT)
    {
        return;
    }
    if (xEnd > OLED_WIDTH) xEnd = OLED_WIDTH;
    if (yEnd > OLED_HEIGHT) yEnd = OLED_HEIGHT;

    for (py = y; py < yEnd; py++)
    {
        for (px = x; px < xEnd; px++)
        {
            OLED_GRAM[py / 8U][px] ^= (uint8_t)(1U << (py % 8U));
        }
    }
    s_gramDirty = 1;
}
