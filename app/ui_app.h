#ifndef __UI_APP_H
#define __UI_APP_H

#include <stdint.h>

/* 六项图标菜单与功能详情页共用的页面 ID。 */
typedef enum
{
    UI_PAGE_CLOCK = 0,
    UI_PAGE_INDOOR,
    UI_PAGE_NETWORK,
    UI_PAGE_ALARM,
    UI_PAGE_FOCUS,
    UI_PAGE_ABOUT,
    UI_PAGE_COUNT
} UIPage;

/* 初始化编码器、SHT30 和网络模块，然后进入大时间首页。 */
void UI_Init(void);

/* 主循环周期调用：处理“首页 -> 图标菜单 -> 详情页”导航，
 * 并后台驱动联网、SHT30、每日闹钟和专注倒计时。 */
void UI_Process(void);

#endif
