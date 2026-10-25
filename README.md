# WiFi 电子时钟（STM32F103C8T6）

基于 STM32F103C8T6 + ESP32（AT 固件）的桌面电子时钟：OLED 显示大字号时间、日期、北京天气与室内温湿度，旋转编码器操作六项图标菜单，断网时靠 DS1302 继续走时。

- 版本：v1.2（大时间首页、六项图标菜单、每日软件闹钟、专注倒计时）
- 主控：STM32F103C8T6（Cortex-M3，64KB Flash / 20KB RAM，HSE 8MHz × 9 = 72MHz）
- 开发环境：STM32CubeMX + Keil MDK（ARMCC V5.06，-O3）+ STM32F1 HAL 库
- 资源占用：Flash 27.52KB（43%）、RAM 4.41KB（22%）
- 编译状态：0 Error / 3 Warning（1 处 GBK 多字节告警 + 2 处文件末尾缺换行）

---

## 1. 功能

界面分三级：**首页 → 图标菜单 → 功能详情页**。

| 菜单项 | 详情页内容 |
|---|---|
| CLOCK/WX | 天气状况文字、日期星期、`HH:MM:SS`、气温/湿度/风速 |
| INDOOR | SHT30 实测温度与相对湿度，2 秒采样一次 |
| WIFI | ESP32 模块、WiFi、校时、天气四项各自是否正常 |
| ALARM | 每日闹钟的时、分、开关设置；触发后全屏提醒 |
| FOCUS | 1~99 分钟专注倒计时，支持开始/暂停/继续/重置 |
| PROJECT | MCU、OLED 型号和固件版本 |

**导航**
- 首页：短按进入图标菜单
- 菜单：旋转循环切换六项，短按进入详情，长按回首页
- 详情：长按返回菜单；闹钟在编辑状态下长按只退出编辑，不返回菜单

**闹钟**
- 默认 07:00、关闭；短按进入编辑，旋转调值，短按依次切换「时 → 分 → 开关 → 完成」，当前字段反白
- 编辑状态下长按只退出编辑，不穿透到返回菜单
- 触发后全屏提醒 60 秒，短按停止，长按停止并回菜单
- 同一天同一设定时刻只触发一次；若 ESP-AT 忙等跨过了设定分钟，5 分钟内补响一次

**专注**
- 未启动时旋转调时长（1~99 分钟，默认 25），短按开始
- 运行中短按暂停，暂停中短按继续，完成后短按复位
- 计时基于**绝对截止时间**，切换页面、进入菜单都不中断；完成时自动跳到专注页

**时间与联网**
- 上电先读 DS1302 立即显示，再联网校时（无网也能看时间）
- SNTP 校时后**回写 DS1302**，之后每 1 小时重新校准
- 天气每 30 分钟刷新一次（Open-Meteo，北京 39.91N / 116.40E）
- 联网失败后每 60 秒后台重连，连续 3 次更新失败判定为掉线

---

## 2. 硬件与接线

| 模块 | 型号 | 接口 | 引脚 |
|---|---|---|---|
| OLED | 1.3" SH1106 128×64 | 4 线 SPI（硬件 SPI1） | PA5 SCK、PA7 MOSI、PA4 CS、PB0 RST、PB1 DC |
| RTC | DS1302 | 3 线 | PA11 CE(RST)、PA8 SCLK、PB4 I/O |
| 温湿度 | SHT30 | I2C1 @100kHz，地址 0x44 | PB6 SCL、PB7 SDA |
| WiFi | ESP32（AT 固件） | USART2 @115200 | PA2 TX→ESP RX、PA3 RX←ESP TX |
| 编码器 | 带按键旋转编码器 | EXTI（上升沿） | PB12 A、PB14 B、PB13 SW |
| 调试 | USB-TTL | USART1 @115200 | PA9 TX、PA10 RX |

**接线要点**
- ESP32 需烧录 AT 固件（支持 `AT+HTTPCGET`，即 ESP-AT 2.x 以上），供电能力要够（峰值 500mA，建议独立 3.3V LDO 并共地）
- 编码器 A/B/SW 三个引脚在代码中配置为**无上下拉**，依赖模块自带上拉电阻；若用裸编码器需外接 10kΩ 上拉到 3.3V，否则读数乱跳
- DS1302 的 I/O 是准双向口，驱动内部会在读之前切换输入/输出方向，无需外部上拉
- PA6（SPI1_MISO）已配置但本设计不用，OLED 是单向写
- PB4 复位后默认是 JTAG 的 NJTRST，驱动里已开 AFIO 时钟做重映射，仿真器需设为 SWD 模式
- **没有蜂鸣器**：闹钟和专注完成都只有屏幕提示，无声音输出

---

## 3. 软件架构

```
┌──────────────────────────────────────────────┐
│  app/        应用层（业务逻辑，不碰寄存器）      │
│    ui_app.c     三态导航、闹钟/专注状态机、绘图   │
│    ui_assets.c  图标与 16x32 数字字模（Flash）   │
│    net_app.c    联网调度、时间/天气、首页文本区   │
└───────────────┬──────────────────────────────┘
                │  只调用驱动层接口
┌───────────────┴──────────────────────────────┐
│  interface/  驱动层（硬件相关，各自封装）        │
│    oled.c     SH1106 显存+绘图（SPI1）          │
│    ds1302.c   RTC 读写（3 线软件时序）          │
│    SHT3x.c    I2C 温湿度 + CRC 校验             │
│    esp32.c    AT 指令、SNTP 校时、HTTP 天气解析  │
│    encoder.c  EXTI 正交解码 + 按键状态机        │
└───────────────┬──────────────────────────────┘
                │
┌───────────────┴──────────────────────────────┐
│  Core/        CubeMX 生成：时钟树、GPIO、USART、│
│               SPI、I2C 初始化，中断入口          │
│  Drivers/     STM32F1 HAL + CMSIS（不改动）     │
└──────────────────────────────────────────────┘
```

**调用链**：`main()` → `UI_Init()` →（`Encoder_Init` → `NetApp_Init` → `Sht_Update` → `EnterHome`）→ 死循环 `UI_Process()`。

`UI_Process()` 是唯一调度中心，每轮固定顺序：

```
读编码器 → HandleInput() → NetApp_Process() → Alarm_Process()
        → Focus_Process() → 采温湿度 → 按 dirty/周期重绘当前页
        → Refresh_StatusIcons() → OLED_Refresh_Gram()
```

没有 RTOS，全部靠 `HAL_GetTick()` 软定时。**一帧只在末尾提交一次显存**。

---

## 4. 关键设计（改代码前必须知道）

### 4.1 DS1302 是唯一时间源

网络时间**从不直接用于显示**。流程是：SNTP 取到时间 → 写入 DS1302 → 再从 DS1302 读回来显示。这样显示值永远等于 RTC 值，不会出现「网络时间跳一下又跳回去」。

`Rtc_Read()` 做合法性校验：年份 2024~2099、月日时分秒范围、星期 1~7。掉电后 DS1302 回到 2000 年或总线异常读到全 0/全 FF，都会判为无效，显示 `----/--/--`。

闹钟和专注都依赖 `NetApp_GetTime()`，时间无效时闹钟不触发。

### 4.2 显示归属权（`s_display` 标志）

首页的固定文本区（日期、大时间、天气行）由 `net_app.c` 绘制，右侧状态图标和底部菜单图标由 `ui_app.c` 绘制；图标菜单和全部详情页只由 `ui_app.c` 绘制。通过 `NetApp_SetDisplay(0/1)` 移交：

- 离开首页 → `SetDisplay(0)`，net_app 停止绘制但保留脏标记
- 回到首页 → `SetDisplay(1)`，重绘文本区，再由 `Draw_HomeStatus()` 补图标

两个模块写的是同一块 GRAM，谁都不负责刷新，统一由 `UI_Process()` 末尾提交。新增页面必须在 `EnterHome()/EnterMenu()/EnterDetail()` 中正确移交，否则会互相覆盖。

### 4.3 脏标记 + 缓存比对

三层去重，目的都是减少 SPI 传输：

| 层 | 机制 |
|---|---|
| 数据层 | `s_dateDirty` / `s_weatherDirty` / `s_shtDirty` / `s_alarmDirty` / `s_focusDirty` |
| 页面层 | 状态图标缓存（`s_homeWifiState` 等），值不变就不重画 |
| 驱动层 | `s_gramDirty`，无变化时 `OLED_Refresh_Gram()` 直接返回 |

首页大时间和时钟详情每秒更新；其余页面只在数据或输入变化时重绘。

### 4.4 三级降级

| 情况 | 行为 |
|---|---|
| 全部正常 | 显示网络时间 + 天气，首页 WiFi 图标为连接状态 |
| ESP32 或 WiFi 失败 | 用 DS1302 时间继续走时，WiFi 图标变断开，后台 60s 重连 |
| DS1302 也读不到 | 显示 `----/--/--` 和 `--:--:--`，界面不崩，闹钟静默 |

启动过程在 OLED 上逐行打印 `ESP32 / WiFi / Time / Weather` 的 `...`→`OK/FAIL`，不开串口也能定位问题。

### 4.5 非阻塞调度写法

统一用这个模式，不要用 `HAL_Delay()` 做周期：

```c
static int Due(uint32_t deadline) {          /* 溢出安全的时间比较 */
    return (int32_t)(HAL_GetTick() - deadline) >= 0;
}

if (Due(s_nextSht)) { Sht_Update(); s_nextSht = HAL_GetTick() + SHT_PERIOD_MS; }
```

### 4.6 表驱动的页面结构

`ui_app.c` 顶部有一张只读表 `s_pages[UI_PAGE_COUNT]`，每个页面对应一个 `UIPageDesc`：

```c
{ page, previous, next, name, menuIcon, drawDetail, handleDetailInput }
```

- `previous` / `next` 决定菜单旋转顺序（首尾成环）
- `drawDetail` 负责整页绘制，**必须先 `OLED_Clear()`**
- `handleDetailInput` 可选，返回 1 表示已消费输入，返回 0 表示交给默认的「长按返回菜单」

**新增一个页面只需三步**：加 `UIPage` 枚举值 → 在表里补一行（含前后链接）→ 写 `Draw_Xxx()`。不用改导航逻辑。

### 4.7 闹钟与专注的时间处理

**闹钟**（`Alarm_Process()`）比较的是「当天分钟数」，不是秒：
- 命中条件：`nowMinute == scheduledMinute`
- 去重：`s_alarmLastDay` + `s_alarmLastMinute`，同日同时刻不再触发
- 补响：记录上次轮询的日期与分钟，若本次轮询发现跨过了设定分钟且跨度 ≤ 5 分钟，补响一次。这是为了兜住 ESP-AT 忙等导致的漏检
- 反向保护：RTC 向后跳变或跨度超过 5 分钟不补响，避免上电后误报旧闹钟

**专注**（`Focus_Process()`）存的是绝对截止时间 `s_focusDeadline = now + 剩余秒数*1000`，不是每秒自减。所以：
- 页面切换、联网阻塞都不会丢时间
- 暂停时把「剩余秒数」冻结到 `s_focusRemaining`，继续时重新算截止时间

---

## 5. OLED 显示布局与图标

可见区域 **128×64**；SH1106 的 132 列是控制器内部 GDDRAM。当前列偏移为 2，逻辑 `x=0/127` 映射到内部列 `2/129`。

**首页（HOME）**

| 区域 | 坐标 | 内容 |
|---|---|---|
| 顶行 | (0,0) 起，14 字符 | `2026-10-25 Sun`，8×16 字体 |
| 状态图标 | (112,0)、(120,0) | WiFi 8×8、闹钟 8×8 |
| 大时间 | (4,16) 起，5 个 16×32 字模 | `HH:MM`，含 1 个冒号字模 |
| 秒 | (96, page4) | `SS`，8×16 字体 |
| 天气行 | (0, page6)，14 字符 | `21.5C H48 W12` |
| 菜单入口 | (112,48) | 16×16 图标 |

**图标菜单（MENU）**：中央 (44,4) 40×40 图标，左右箭头 (4,18)/(112,18) 各 12×12，底部居中页面名称，主图标右侧 (88,4) 可挂 8×8 状态角标。

**详情页（DETAIL）**：标题行占 14 字符，右上角 (120,0) 显示 8×8 状态图标，其余三行各 16 字符。

> 排版约定：文本行统一用 `ShowRow()` / `ShowRowN()` 写满固定宽度，右补空格覆盖旧内容，避免残影。

**位图格式**：列优先、垂直字节、bit0 在上、1 为点亮、高度不足 8 位时高位补 0。`OLED_DrawBitmap()` 支持任意像素 Y 和边界裁剪，源图中的 0 会清除目标像素；`OLED_InvertRect()` 用于编辑字段反白。

> 若换成 0.96 寸 SSD1306，把 `interface/oled.h` 里的 `OLED_USE_SH1106` 改成 `0`，否则画面会整体错位 2 列。

---

## 6. 主要配置项

| 位置 | 宏 | 默认 | 作用 |
|---|---|---|---|
| `app/net_app.h` | `NET_WIFI_SSID` / `NET_WIFI_PWD` | — | WiFi 账号密码（**见第 9 节**） |
| `app/net_app.h` | `NET_TIME_SYNC_MS` | 3600000 | 校时间隔（1 小时） |
| `app/net_app.h` | `NET_WEATHER_MS` | 1800000 | 天气刷新间隔（30 分钟） |
| `app/net_app.h` | `NET_RETRY_MS` | 60000 | 失败重试间隔（60 秒） |
| `app/net_app.h` | `NET_WIFI_RETRY` | 3 | 单次连接最多尝试次数 |
| `app/ui_app.c` | `SHT_PERIOD_MS` | 2000 | 室内温湿度采样周期 |
| `app/ui_app.c` | `STATUS_PERIOD_MS` | 1000 | 网络状态页刷新周期 |
| `app/ui_app.c` | `CLOCK_PAGE_MS` | 1000 | 时钟详情页/闹钟提醒页刷新周期 |
| `app/ui_app.c` | `ALARM_RING_MS` | 60000 | 闹钟全屏提醒时长 |
| `app/ui_app.c` | `ALARM_CATCHUP_MIN` | 5 | 漏检补响的最大跨度（分钟） |
| `app/ui_app.c` | `FOCUS_DEFAULT_MIN` | 25 | 专注默认时长 |
| `app/ui_app.c` | `FOCUS_MIN/MAX_MINUTES` | 1 / 99 | 专注时长上下限 |
| `interface/encoder.c` | `ENCODER_DEBOUNCE_MS` | 20 | 按键消抖时间 |
| `interface/encoder.c` | `ENCODER_LONG_MS` | 700 | 长按判定时间 |
| `interface/oled.h` | `OLED_USE_SH1106` | 1 | 1=SH1106(1.3")，0=SSD1306(0.96") |
| `interface/esp32.c` | `ESP32_GetWeather()` 内 URL | 北京 | 天气经纬度 |

改经纬度时注意：**URL 里的逗号必须写成 `\\,`**，因为 AT 指令用逗号分隔参数，不转义会被 ESP32 截断。

---

## 7. 编译与烧录

1. Keil MDK 打开 `MDK-ARM/ESP32.uvprojx`
2. `F7` 编译（ARMCC V5.06，-O3），产物在 `MDK-ARM/ESP32/ESP32.hex`
3. 仿真器设 **SWD** 模式（PB4 被 DS1302 占用，JTAG 不可用），`F8` 下载
4. 串口助手接 PA9/PA10，**115200 8N1**，可看到启动流程和 AT 交互日志（`printf` 已重定向到 USART1）

当前编译结果：`Code=23000  RO-data=4564  RW-data=620  ZI-data=3900`，即 ROM 27.52KB / RAM 4.41KB。3 个警告均为无害项：1 个来自 GBK 源文件的多字节字符告警，2 个是文件末尾缺换行。

改外设配置请用 CubeMX 打开 `ESP32.ioc` 重新生成，**只改 `USER CODE BEGIN/END` 之间**的内容，否则重新生成会被覆盖。

---

## 8. 目录结构

```
ESP32/
├── ESP32.ioc                CubeMX 工程（引脚/时钟/外设配置源）
├── .mxproject               CubeMX 生成缓存
├── Core/
│   ├── Inc/                 gpio/i2c/spi/usart/main 头文件、HAL 配置
│   └── Src/                 初始化代码、中断入口、printf 重定向
├── Drivers/                 STM32F1 HAL + CMSIS（原样引入，不修改）
├── interface/               硬件驱动层
│   ├── oled.c/h/oledfont.h  SH1106 驱动 + 8x16 ASCII 字库
│   ├── ds1302.c/h           DS1302 三线时序
│   ├── SHT3x.c/h            SHT30 I2C 驱动（含 CRC8）
│   ├── esp32.c/h            AT 指令封装、SNTP、HTTP 天气
│   └── encoder.c/h          正交编码器 + 按键状态机
├── app/                     应用层
│   ├── net_app.c/h          联网调度、时间天气数据、首页文本区
│   ├── ui_app.c/h           三态导航、闹钟与专注状态机、各页绘制
│   └── ui_assets.c/h        6 个 40x40 图标、8x8 状态图标、16x32 数字字模
└── MDK-ARM/                 Keil 工程与编译产物（产物已 gitignore）
```

---

## 9. 已知问题与改进方向

按优先级排列。

**① `interface/oled.c` 的中文注释已损坏（882 处乱码）**
该文件在 v1.2 编辑时被错误地做过一次 GBK→UTF-8 转换，中文注释全部变成 `U+FFFD` 替换字符，在 VS Code 和 Keil 里都显示为 `���ɵ�` 之类的乱码。**代码本身是 ASCII，功能不受影响**，编译也正常。
好消息：`b1f2077` 提交里的原始 GBK 版本是完好的，可按行比对恢复。这是当前仓库里最该修的一件事。

**② 联网操作阻塞主循环，界面卡住**
`ESP32_SendCmd()` 是忙等超时（`WaitFor`），期间 `UI_Process()` 不执行。最坏情况：
- 重连时 `AT+CWJAP` 超时 20s × 3 次 = **最长 60 秒界面无响应**
- 每次 SNTP 校时最长 15 秒（15 次 1 秒轮询）
- 天气请求 15 秒

现象是「隔一段时间按键突然失灵」。v1.2 已用绝对截止时间把专注计时的损失兜住、用 5 分钟补响兜住闹钟漏检，但这只是补偿，不是根治。根治方案是把 AT 交互改成状态机（发送即返回，主循环里查结果）。

**③ WiFi 密码硬编码在 `app/net_app.h`，已进入 git 历史**
公开仓库前必须处理：把 SSID/密码抽到 `app/net_config.h`（加入 `.gitignore`），仓库里只留 `net_config.example.h` 模板，再用 `git filter-repo` 清理历史。当前仓库只适合本地或私有远端。

**④ 部分源文件仍是 GBK 编码**
`interface/encoder.c`、`interface/encoder.h`、`interface/oledfont.h` 是 GBK，其余均为 UTF-8。VS Code 打开会乱码，且编译时产生 1 个 `invalid multibyte character sequence` 警告。建议统一转 UTF-8，并在 Keil 里设 `Edit → Configuration → Encoding = UTF-8`。

**⑤ 显存仍整帧提交**
一帧只传一次 1KB GRAM，无变化不传输，已经够用。若后续要压帧率或降功耗，可再做脏区域局部刷新。

**⑥ 其他**
- 闹钟只有屏幕提示，**没有声音**——原理图未分配蜂鸣器引脚。这让闹钟实用性有限，是硬件层面的待办
- 闹钟设置、专注时长都只存在 RAM，掉电丢失；页面位置也不保存
- 无 DS1302 备用电池电压检测，电池耗尽后只能靠联网校时
- 天气备用接口（`current_weather=true`）不返回湿度，降级时湿度显示 `--`
- `MDK-ARM/ESP32.uvoptx` 被跟踪，Keil 每次增删文件或改窗口都会产生改动，建议 `git rm --cached` 后加入 `.gitignore`
- `Core/Src/main.c` 里的 `uint8_t buff[1000]` 未被使用，可删

---

## 10. 版本记录

| 版本 | 提交 | 内容 |
|---|---|---|
| v1.0 | `299b339` | 首个可用版本：三页界面、DS1302 走时、SNTP 校时回写、天气、SHT30、编码器交互、断网降级 |
| v1.1 | 未单独提交 | 扩展六页 GUI，新增每日软件闹钟、全屏提醒和可后台运行的专注倒计时 |
| v1.2 | `4efd612` | 大时间首页、六项表驱动图标菜单、动态状态图标、任意坐标位图与统一显存提交（v1.1 改动一并提交） |
| 文档 | 本次提交 | README 同步至 v1.2 实际实现，补充布局坐标、状态机规则与资源占用 |
