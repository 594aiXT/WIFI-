#ifndef __UI_APP_H
#define __UI_APP_H

#include <stdint.h>

/* 页面：0=时间+天气  1=室内温湿度  2=网络状态 */
#define UI_PAGE_COUNT   3

/* 初始化编码器和 SHT30，并启动联网模块（NetApp_Init）。
 * 调用前需已完成 OLED_Init()、MX_I2C1_Init()。                         */
void UI_Init(void);

/* 放在 main 的 while(1) 中调用：
 * - 处理编码器（旋转/短按切页，长按回首页）
 * - 驱动联网模块 NetApp_Process()
 * - 读取 SHT30、刷新当前页面                                          */
void UI_Process(void);

#endif
