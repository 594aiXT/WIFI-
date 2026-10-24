
#ifndef __OLED_H
#define __OLED_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* 默认按常见的 1.3 寸 SH1106 OLED 配置。
   如果你的屏幕确认是 SSD1306，将其改为 0。 */
#ifndef OLED_USE_SH1106
#define OLED_USE_SH1106    1
#endif

#define OLED_WIDTH         128
#define OLED_HEIGHT        64
#define OLED_PAGES         8

/* 16x16 点阵英文字体；保留原工程的字体接口 */
#define SIZE               16

#define OLED_CMD           0
#define OLED_DATA          1

/* 低层接口 */
void OLED_WR_Byte(uint8_t dat, uint8_t cmd);
void OLED_Set_Pos(uint8_t x, uint8_t y);
void OLED_Refresh_Gram(void);

/* OLED 控制接口 */
void OLED_Init(void);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Clear(void);

/* 绘图接口
   DrawPoint 和 Fill 使用像素坐标：x=0~127，y=0~63。
   ShowChar、ShowString、ShowNum、ShowCHinese 保留旧接口的页坐标：
   y=0 表示第 0 页，每页高 8 像素；16 高字体占用 y 和 y+1 两页。 */
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t);
void OLED_Fill(uint8_t x1, uint8_t y1,
               uint8_t x2, uint8_t y2, uint8_t dot);

void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr);
void OLED_ShowNum(uint8_t x, uint8_t y,
                  uint32_t num, uint8_t len, uint8_t size);
void OLED_ShowString(uint8_t x, uint8_t y, const char *p);
void OLED_ShowCHinese(uint8_t x, uint8_t y, uint8_t no);

/* 图片数据使用原代码的页格式；
   x1 是结束列，y1 是结束页，均不包含在绘制范围内。 */
void OLED_DrawBMP(uint8_t x0, uint8_t y0,
                  uint8_t x1, uint8_t y1, const uint8_t BMP[]);

#endif
