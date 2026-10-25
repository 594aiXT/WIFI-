#ifndef __OLED_H
#define __OLED_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* 1.3 寸 SH1106：内部 GDDRAM 为 132 列，模组可见区域为 128 列。
 * 若实际控制器为 SSD1306，将 OLED_USE_SH1106 改为 0。 */
#ifndef OLED_USE_SH1106
#define OLED_USE_SH1106    1
#endif

#define OLED_WIDTH         128
#define OLED_HEIGHT        64
#define OLED_PAGES         8

#define SIZE               16       /* 兼容旧文字接口：8x16 ASCII */

#define OLED_CMD           0
#define OLED_DATA          1

HAL_StatusTypeDef OLED_WR_Byte(uint8_t dat, uint8_t cmd);
HAL_StatusTypeDef OLED_Set_Pos(uint8_t x, uint8_t y);

/* 所有绘图函数只写 GRAM；由主循环或启动流程调用本函数统一提交。
 * 无显存变化时不进行 SPI 传输。 */
void OLED_Refresh_Gram(void);

void OLED_Init(void);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Clear(void);

/* 点与矩形使用像素坐标；旧文字接口的 y 使用页坐标。 */
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t);
void OLED_Fill(uint8_t x1, uint8_t y1,
               uint8_t x2, uint8_t y2, uint8_t dot);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr);
void OLED_ShowNum(uint8_t x, uint8_t y,
                  uint32_t num, uint8_t len, uint8_t size);
void OLED_ShowString(uint8_t x, uint8_t y, const char *p);
void OLED_ShowCHinese(uint8_t x, uint8_t y, uint8_t no);

/* 兼容旧页对齐 BMP 接口。 */
void OLED_DrawBMP(uint8_t x0, uint8_t y0,
                  uint8_t x1, uint8_t y1, const uint8_t BMP[]);

/* 新图标接口：像素坐标，支持任意高度和边界裁剪。
 * 位图格式为列优先、垂直字节、bit0 在上、1 为点亮。 */
void OLED_DrawBitmap(int16_t x, int16_t y,
                     uint8_t width, uint8_t height,
                     const uint8_t *bitmap);
void OLED_InvertRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height);

#endif
