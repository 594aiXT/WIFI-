# WiFi 电子时钟（STM32F103C8T6）

基于 STM32F103C8T6 + ESP32（AT 固件）的桌面电子时钟：OLED 显示时间、日期、北京天气与室内温湿度，旋转编码器切换页面，断网时靠 DS1302 继续走时。

- 版本：v1.0（首个可用版本，提交 `299b339`）
- 主控：STM32F103C8T6（Cortex-M3，64KB Flash / 20KB RAM，HSE 8MHz × 9 = 72MHz）
- 开发环境：STM32CubeMX + Keil MDK（ARMCC V5.06，-O3）+ STM32F1 HAL 库
- 资源占用：Flash 21.31KB（33%）、RAM 3.93KB（20%）

---

## 1. 功能

| 页面 | 内容 |
|---|---|
| 0 时间天气（默认） | 日期+星期+联网标记、`HH:MM:SS`、北京天气状况、气温/湿度/风速 |
| 1 室内温湿度 | SHT30 实测温度与相对湿度，2 秒采样一次 |
| 2 网络状态 | ESP32 模块、WiFi、校时、天气四项各自是否正常 |

**交互**：旋转编码器或短按 → 切到下一页（循环）；长按 700ms → 回到首页。

**时间与联网**：
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

---

## 3. 软件架构

```
┌──────────────────────────────────────────────┐
│  app/        应用层（业务逻辑，不碰寄存器）      │
│    ui_app.c    页面管理、编码器输入、SHT30 采样  │
│    net_app.c   联网调度、时间/天气数据、时间页绘制│
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

**调用链**：`main()` → `UI_Init()` → （`Encoder_Init` → `NetApp_Init` → `Sht_Update` → `EnterPage(0)`）→ 死循环 `UI_Process()`。

`UI_Process()` 是唯一的调度中心，每轮依次做 4 件事：读输入 → 跑联网模块 → 定时采温湿度 → 重绘当前页。没有 RTOS，全部靠 `HAL_GetTick()` 软定时。

---

## 4. 关键设计（改代码前必须知道）

### 4.1 DS1302 是唯一时间源

网络时间**从不直接用于显示**。流程是：SNTP 取到时间 → 写入 DS1302 → 再从 DS1302 读回来显示。这样显示值永远等于 RTC 值，不会出现「网络时间跳一下又跳回去」。

`Rtc_Read()` 里做了合法性校验：年份 2024~2099、月日时分秒范围、星期 1~7。掉电后 DS1302 回到 2000 年或总线异常读到全 0/全 FF，都会被判为无效，显示 `----/--/--`。

### 4.2 显示归属权（`s_display` 标志）

时间天气页由 `net_app.c` 绘制，其余两页由 `ui_app.c` 绘制。两个模块都能写 OLED，靠 `NetApp_SetDisplay(0/1)` 移交控制权：

- 离开时间页 → `SetDisplay(0)`，net_app 停止所有绘制但**保留脏标记**
- 回到时间页 → `SetDisplay(1)`，立即整页重绘一次

这是本项目最容易踩坑的地方：新增页面时必须在 `EnterPage()` 里正确调用，否则会出现两个模块互相覆盖、屏幕闪烁。

### 4.3 脏标记重绘

`net_app.c` 用 `s_dateDirty` / `s_weatherDirty` 控制重绘，`ui_app.c` 用 `s_shtDirty`。目的只有一个：**不要每秒整屏刷新**，否则 OLED 会明显闪。

时间行是例外，每秒必刷一次（秒数在变）。

### 4.4 三级降级

| 情况 | 行为 |
|---|---|
| 全部正常 | 显示网络时间 + 天气，日期行末尾标记 `W` |
| ESP32 或 WiFi 失败 | 用 DS1302 时间继续走时，标记变 `x`，后台 60s 重连 |
| DS1302 也读不到 | 显示 `----/--/--` 和 `--:--:--`，界面不崩 |

启动过程在 OLED 上逐行打印 `ESP32 / WiFi / Time / Weather` 的 `...`→`OK/FAIL`，方便不开串口也能定位问题。

### 4.5 非阻塞调度写法

统一用这个模式，不要用 `HAL_Delay()` 做周期：

```c
static int Due(uint32_t deadline) {          /* 溢出安全的时间比较 */
    return (int32_t)(HAL_GetTick() - deadline) >= 0;
}

if (Due(s_nextSht)) { Sht_Update(); s_nextSht = HAL_GetTick() + SHT_PERIOD_MS; }
```

---

## 5. OLED 显示布局

128×64 屏，8×16 英文字体，**一行 16 字符 × 4 行**（行 0~3，对应页地址 y=0/2/4/6）。

所有字符串统一走 `ShowRow(row, str)`，内部 `%-16s` 右补空格，保证新内容能盖住旧内容。

**时间天气页**
```
2026-10-25 Sun W     ← 日期 星期 联网标记(W/x)
01:03:29             ← 时间
Beijing Clear        ← 城市 + 天气状况
 21.5C H48 W12       ← 温度 湿度 风速(km/h)
```

**室内页**
```
-- INDOOR --
Temp: 21.5 C
Humi: 48.2 %
Page 2/3
```

**网络页**
```
-- NETWORK --
ESP:OK WiFi:ON
Wthr:OK Time:OK
Page 3/3
```

> 屏幕默认按 **SH1106**（1.3 寸）驱动，列偏移 2。若换成 0.96 寸 SSD1306，把 `interface/oled.h` 里的 `OLED_USE_SH1106` 改成 `0`，否则画面会整体错位 2 列。

---

## 6. 主要配置项

改参数基本只动这几个文件：

| 位置 | 宏 | 作用 |
|---|---|---|
| `app/net_app.h` | `NET_WIFI_SSID` / `NET_WIFI_PWD` | WiFi 账号密码（**见第 9 节**） |
| `app/net_app.h` | `NET_TIME_SYNC_MS` | 校时间隔，默认 1 小时 |
| `app/net_app.h` | `NET_WEATHER_MS` | 天气刷新间隔，默认 30 分钟 |
| `app/net_app.h` | `NET_RETRY_MS` | 失败重试间隔，默认 60 秒 |
| `app/net_app.h` | `NET_WIFI_RETRY` | 单次连接最多尝试次数，默认 3 |
| `app/ui_app.c` | `SHT_PERIOD_MS` | 室内温湿度采样周期，默认 2000ms |
| `app/ui_app.c` | `STATUS_PERIOD_MS` | 网络状态页刷新周期，默认 1000ms |
| `interface/encoder.c` | `ENCODER_DEBOUNCE_MS` | 按键消抖时间，默认 20ms |
| `interface/encoder.c` | `ENCODER_LONG_MS` | 长按判定时间，默认 700ms |
| `interface/oled.h` | `OLED_USE_SH1106` | 1=SH1106(1.3")，0=SSD1306(0.96") |
| `interface/esp32.c` | `ESP32_GetWeather()` 内 URL | 天气经纬度（默认北京） |

改经纬度时注意：**URL 里的逗号必须写成 `\\,`**，因为 AT 指令用逗号分隔参数，不转义会被 ESP32 截断。

---

## 7. 编译与烧录

1. Keil MDK 打开 `MDK-ARM/ESP32.uvprojx`
2. `F7` 编译（ARMCC V5.06，-O3），产物在 `MDK-ARM/ESP32/ESP32.hex`
3. 仿真器设 **SWD** 模式（PB4 被 DS1302 占用，JTAG 不可用），`F8` 下载
4. 串口助手接 PA9/PA10，**115200 8N1**，可看到启动流程和 AT 交互日志（`printf` 已重定向到 USART1）

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
│   ├── net_app.c/h          联网调度、时间天气数据、时间页绘制
│   └── ui_app.c/h           页面管理、编码器交互、室内页绘制
└── MDK-ARM/                 Keil 工程与编译产物（产物已 gitignore）
```

---

## 9. 已知问题与改进方向

按优先级排列，前两条建议优先处理。

**① WiFi 密码硬编码在 `app/net_app.h`，已进入 git 历史**
公开仓库前必须处理：把 SSID/密码抽到 `app/net_config.h`（加入 `.gitignore`），仓库里只留 `net_config.example.h` 模板，再用 `git filter-repo` 清理历史。当前仓库只适合本地或私有远端。

**② 联网操作会阻塞主循环，界面卡住**
`ESP32_SendCmd()` 是忙等超时（`WaitFor`），期间 `UI_Process()` 不执行。实测最坏情况：
- 重连时 `AT+CWJAP` 超时 20s × 3 次 = **最长 60 秒界面无响应**（编码器转不动、按键无反应）
- 每次 SNTP 校时最长 15 秒（15 次 1 秒轮询）
- 天气请求 15 秒

现象是「隔一段时间按键突然失灵」。根治方案是把 AT 交互改成状态机（发送后立即返回，主循环里查结果），这是 v1.1 最值得做的一件事。

**③ 注释编码不统一，VS Code 里部分中文乱码**
`encoder.h`、`oled.h`、`oled.c`、`SHT3x.c` 等文件是 GBK 编码（Keil 默认），`app/` 下是 UTF-8。VS Code 打开会看到 `�̰���ȷ��` 这类乱码。不影响编译，但影响阅读。统一转 UTF-8 并在 Keil 里设 `Edit → Configuration → Encoding = UTF-8` 即可。

**④ 每次写一行都整屏刷新**
`OLED_ShowString()` 内部直接调 `OLED_Refresh_Gram()`，一页 4 行 = 4 次全屏 SPI 传输（每次 1KB）。SPI1 分频 16 → 72/16 = 4.5MHz，刷一次全屏约 1.8ms，暂时看不出闪，但 CPU 时间是白花的。改成「写显存 → 循环末尾统一刷一次」更合理。

**⑤ 其他**
- 无 DS1302 备用电池电压检测，电池耗尽后只能靠联网校时
- 天气备用接口（`current_weather=true`）不返回湿度，降级时湿度显示 `--`
- `main.c` 里的 `uint8_t buff[1000]` 未被使用，可删
- 未做断电时间保持以外的掉电保护（WiFi 密码、页面位置均不持久化）

---

## 10. 版本记录

| 版本 | 提交 | 内容 |
|---|---|---|
| v1.0 | `299b339` | 首个可用版本：三页界面、DS1302 走时、SNTP 校时回写、天气、SHT30、编码器交互、断网降级 |
