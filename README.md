# WiFi 电子时钟（STM32F103C8T6）

基于 STM32F103C8T6 + ESP32（AT 固件）的桌面电子时钟：OLED 显示大字号时间、日期、北京天气与室内温湿度，旋转编码器操作六项图标菜单，断网时靠 DS1302 继续走时。

- 版本：v1.5（凭据外置；v1.4 起含蜂鸣器提醒、闹钟四模式与贪睡、专注休息周期、设置掉电保存）
- 主控：STM32F103C8T6（Cortex-M3，64KB Flash / 20KB RAM，HSE 8MHz × 9 = 72MHz）
- 开发环境：STM32CubeMX + Keil MDK（ARMCC V5.06，-O3）+ STM32F1 HAL 库
- 资源占用：Flash 31.16KB（49%）、RAM 4.52KB（23%）
- 编译状态：0 Error / 0 Warning

---

## 1. 功能

界面分三级：**首页 → 图标菜单 → 功能详情页**。

| 菜单项 | 详情页内容 |
|---|---|
| CLOCK/WX | 天气状况文字、日期星期、`HH:MM:SS`、气温/湿度/风速 |
| INDOOR | SHT30 实测温度与相对湿度，2 秒采样一次 |
| WIFI | ESP32 模块、WiFi、校时、天气四项各自是否正常 |
| ALARM | 闹钟时分 + 四种模式（关/一次/每天/工作日）；响铃三阶段升级、支持贪睡 |
| FOCUS | 专注倒计时 + 休息周期，1~99 分钟可调，结束有提示音 |
| PROJECT | MCU、OLED 型号和固件版本 |

**导航**
- 首页：短按进入图标菜单
- 菜单：旋转循环切换六项，短按进入详情，长按回首页
- 详情：长按返回菜单；闹钟在编辑状态下长按只退出编辑，不返回菜单

**闹钟**
- 默认 07:00、`OFF`；短按进入编辑，旋转调值，短按依次切换「时 → 分 → 模式 → 完成」，当前字段反白
- 模式：`OFF` / `ONCE`（响一次后自动关闭并保存）/ `DAILY` / `WKDAY`（周一至周五）
- 响铃全屏提醒，**铃声随响铃时长升级**：前 15 秒单声、15~40 秒双声、40 秒后急促三连
- 响铃时短按 **贪睡 5 分钟**，长按关闭；60 秒无人响应自动进入贪睡，最多再响 2 次
- 同一天同一设定时刻只触发一次；ESP-AT 忙等跨过设定分钟时，5 分钟内补响一次

**专注**
- 未启动时旋转调时长（1~99 分钟，默认 25；快速旋转按 5 分钟一档加速），短按开始
- 运行中短按暂停；**暂停时旋转仍可调时长**，长按放弃本轮回到待机
- 专注结束短按进入休息：默认 5 分钟，每完成 4 轮给一次 15 分钟长休息；休息中短按可跳过，休息结束短按回到待机
- 专注/休息结束各有提示音，最长响 30 秒，之后保持 `DONE`/`END` 状态等确认
- 计时基于**绝对截止时间**，切换页面、进入菜单都不中断

**提醒音**
- 无源蜂鸣器由 TIM2_CH1 的 PWM 驱动，音符表 + 非阻塞状态机，不占用延时
- 操作反馈音：短按确认、专注开始（上扬）、暂停（下行）、贪睡（两短声）、保存成功
- 闹钟优先级高于专注提示；响铃期间暂停联网请求，避免 AT 忙等把铃声卡在某个音上

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
| 编码器 | 带按键旋转编码器 | A/B 双边沿 EXTI + 按键轮询 | PB14 A、PB12 B、PB13 SW |
| 蜂鸣器 | 无源蜂鸣器 / 蜂鸣器模块 | TIM2_CH1 PWM | PA0 |
| 调试 | USB-TTL | USART1 @115200 | PA9 TX、PA10 RX |

**接线要点**
- ESP32 需烧录 AT 固件（支持 `AT+HTTPCGET`，即 ESP-AT 2.x 以上），供电能力要够（峰值 500mA，建议独立 3.3V LDO 并共地）
- 编码器 A/B/SW 三个引脚使用 STM32 **内部上拉**，按下或接地时读为低电平；若模块已有外部上拉可并联使用。A/B 使用双边沿正交解码，按键使用 30ms 稳定窗口
- DS1302 的 I/O 是准双向口，驱动内部会在读之前切换输入/输出方向，无需外部上拉
- PA6（SPI1_MISO）已配置但本设计不用，OLED 是单向写
- PB4 复位后默认是 JTAG 的 NJTRST，驱动里已开 AFIO 时钟做重映射，仿真器需设为 SWD 模式
- 蜂鸣器必须是**无源**（靠 PWM 频率发声）；有源蜂鸣器只会一直响同一个音。直连请串 100Ω~1kΩ 限流并确认驱动电流
- 蜂鸣器模块若为**低电平触发**，保持 `interface/beep.c` 里 `BEEP_IDLE_HIGH = 1`（静音输出高）；高电平触发改为 `0`。接的不是 PA0/TIM2_CH1 时，改 `BEEP_TIM` / `BEEP_CHANNEL` 两个宏即可

---

## 3. 软件架构

```
┌──────────────────────────────────────────────┐
│  app/        应用层（业务逻辑，不碰寄存器）      │
│    ui_app.c     三态导航、闹钟/专注状态机、提醒音调度│
│    ui_assets.c  图标与 16x32 数字字模（Flash）   │
│    net_app.c    联网调度、时间/天气、首页文本区   │
│    reminder_store.c  闹钟/专注设置掉电保存       │
└───────────────┬──────────────────────────────┘
                │  只调用驱动层接口
┌───────────────┴──────────────────────────────┐
│  interface/  驱动层（硬件相关，各自封装）        │
│    oled.c     SH1106 显存+绘图（SPI1）          │
│    ds1302.c   RTC 读写 + 后备 RAM（3 线时序）   │
│    SHT3x.c    I2C 温湿度 + CRC 校验             │
│    esp32.c    AT 指令、SNTP 校时、HTTP 天气解析  │
│    encoder.c  EXTI 正交解码 + 按键状态机        │
│    beep.c     TIM2 PWM + 音符表非阻塞播放       │
└───────────────┬──────────────────────────────┘
                │
┌───────────────┴──────────────────────────────┐
│  Core/        CubeMX 生成：时钟树、GPIO、USART、│
│               SPI、I2C 初始化，中断入口          │
│  Drivers/     STM32F1 HAL + CMSIS（不改动）     │
└──────────────────────────────────────────────┘
```

**调用链**：`main()` →（`MX_TIM2_Init`）→ `UI_Init()` →（`Beep_Init` → `Encoder_Init` → `NetApp_Init` → `ReminderStore_Load` → `Sht_Update` → `EnterHome`）→ 死循环 `UI_Process()`。

`UI_Process()` 是唯一调度中心，每轮固定顺序：

```
读编码器 → HandleInput() → Alarm_Process() → Focus_Process()
        → Reminder_UpdateBeep() → Beep_Process() → NetApp_PauseNetwork()
        → NetApp_Process() → 采温湿度 → 按 dirty/周期重绘当前页
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

时长调整（`Focus_Adjust()`）：运行中不接受旋转；未开始或已完成时旋转会把状态重置为 IDLE 并套用新时长；暂停时只更新剩余时间、保持暂停。上下限 1~99 分钟，到边界后钳位不再回绕。

### 4.8 编码器输入（v1.3 重做）

机械编码器的两个核心问题是**旋转抖动被当成按键**、**主循环慢导致消抖失效**。v1.3 从三处解决：

**① 旋转：完整正交解码**
A/B 改为**双边沿** EXTI，用 16 项状态表（`Encoder_Transition[前一状态<<2 | 当前状态]`）判断方向。每 4 个合法边沿算 1 档（`ENCODER_STEPS_PER_DETENT`），`Encoder_Get()` 只取整档，把不足一档的余量加回 `SubSteps`，慢速旋转不丢步。抖动产生的往返边沿在状态表里互相抵消，不产生净计数。

**② 按键：30ms 稳定窗口 + 1ms 节拍**
按键不再用 EXTI，改为在 `SysTick_Handler` 里每 1ms 采样一次（`Encoder_Tick1ms()`）。电平必须与当前稳定状态不同且连续保持 30ms 才认账，中途回到原电平就重新计时。松手才产生 `CONFIRM`，按住 700ms 产生 `BACK`。
放在 SysTick 而不是主循环，是因为主循环会被 ESP-AT 忙等阻塞最长 60 秒——那时轮询消抖完全失效。

**③ 旋转/按键互锁（本次关键修复）**
- A/B 任意边沿（含抖动）都刷新 `last_rotate_tick`
- 其后 150ms 内（`ENCODER_ROTATE_GUARD_MS`）按键一律视为松开
- 若按下过程中发生旋转，本次按键标记 `press_aborted`：松手不产生短按，也不会触发长按

这解决的现象是：转动编码器时偶尔会误进菜单、或刚进专注页就被"短按"开始计时，导致时长调不动。

> 兼容：`Encoder_ButtonTask()` 保留。若未接入 1ms 节拍，它会按经过的毫秒数补采样（主循环慢时效果变差），功能不会丢。
> **注意**：`Encoder_Tick1ms()` 的调用必须写在 `stm32f1xx_it.c` 的 `USER CODE BEGIN/END SysTick_IRQn 1` 区间内，否则 CubeMX 重新生成代码时会把它删掉。

### 4.9 提醒音系统（v1.4）

蜂鸣器是无源的，靠 PWM 频率发声：`Beep_Tone()` 按 `1MHz / freq` 算 ARR、CCR 取一半得到 50% 占空比。所有提示音都是 `{频率, 时长}` 的音符表：

```c
static const Note_t kAlarm2[] = {{2000,120},{0,80},{2000,120},{0,680}};   /* 双声，0=休止 */
```

`Beep_Process()` 在主循环里比对 `HAL_GetTick()` 切换音符，**全程不阻塞**。循环型提示音（闹钟三阶段、专注结束、休息结束）会一直重复，直到 `Beep_Stop()`。

调度上分两层：
- `Reminder_UpdateBeep()` 决定「当前应该响哪一种」，只在类型变化时切换，避免每圈重启音符
- `UiBeep()` 负责操作反馈音，且**在提醒响铃期间自动让路**（`s_ringKind != 0` 时不播放）

优先级：闹钟 > 专注提示 > 操作反馈。闹钟响铃阶段按经过时间自动升级（15s / 40s 两个门槛）。

响铃期间 `NetApp_PauseNetwork(1)` 会掐掉重连、校时、天气请求——否则 AT 忙等 15~60 秒会让音符卡死在当前音上。DS1302 读时和屏幕刷新不受影响。

### 4.10 设置持久化（v1.4）

闹钟时分、模式、专注时长存在 **DS1302 的 31 字节后备 RAM** 里，靠纽扣电池保持，不需要额外 EEPROM。

- 地址：`0xC0/0xC1 + 2n`（n=0~30），用 n=0~5
- 结构：魔数 `0xA6` + 4 个数据字节 + 校验字节（4 字节异或 0x5A）
- 写入顺序：先写数据、**最后写魔数**，保证掉电中断时不会出现「魔数有效但数据半新半旧」
- 读取：魔数、校验、范围（时 0~23 / 分 0~59 / 模式 0~3 / 时长 1~99）全部通过才采用，否则回落到默认值
- 保存时机：闹钟编辑完成、`ONCE` 模式响完自动关闭、专注开始时
- 必须在 `ds1032_init()` 之后才能读写，所以 `UI_Init()` 里放在 `NetApp_Init()` 后面

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

**闹钟页的状态**：`Time: 07:00` + `State:OFF/ONCE/DAILY/WKDAY` + 操作提示；编辑时当前字段反白。响铃态换成 `!!! ALARM !!!` + 当前时间 + `Click:Snooze 5m` / `Hold :Stop`；贪睡态第二行显示 `Snooze mm:ss` 倒计时。

**专注页的状态**：标题在 `FOCUS` / `BREAK` 之间切换，第二行是 `mm:ss 状态词`（`READY`/`RUN`/`PAUSE`/`DONE`/`REST`/`END`），第三行随状态显示 `Set:Nm Turn`、`Round N Focus`、`Relax N min`、`Break over!` 等。

> 排版约定：文本行统一用 `ShowRow()` / `ShowRowN()` 写满固定宽度，右补空格覆盖旧内容，避免残影。

**位图格式**：列优先、垂直字节、bit0 在上、1 为点亮、高度不足 8 位时高位补 0。`OLED_DrawBitmap()` 支持任意像素 Y 和边界裁剪，源图中的 0 会清除目标像素；`OLED_InvertRect()` 用于编辑字段反白。

> 若换成 0.96 寸 SSD1306，把 `interface/oled.h` 里的 `OLED_USE_SH1106` 改成 `0`，否则画面会整体错位 2 列。

---

## 6. 主要配置项

| 位置 | 宏 | 默认 | 作用 |
|---|---|---|---|
| `app/net_config.h` | `NET_WIFI_SSID` / `NET_WIFI_PWD` | — | WiFi 账号密码，本机私有文件（**见第 7、9 节**） |
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
| `app/ui_app.c` | `ALARM_SNOOZE_MS` | 300000 | 贪睡时长（5 分钟） |
| `app/ui_app.c` | `ALARM_AUTO_REPEATS` | 2 | 无人响应时自动贪睡的最大次数 |
| `app/ui_app.c` | `ALARM_STAGE2/3_MS` | 15000 / 40000 | 铃声升级为双声 / 急促的门槛 |
| `app/ui_app.c` | `FOCUS_RING_MS` | 30000 | 专注/休息结束提示音最长时长 |
| `app/ui_app.c` | `FOCUS_BREAK_MIN` / `FOCUS_LONG_BREAK_MIN` | 5 / 15 | 短休息 / 长休息分钟数 |
| `app/ui_app.c` | `FOCUS_LONG_BREAK_EVERY` | 4 | 每几轮专注给一次长休息 |
| `interface/beep.c` | `BEEP_TIM` / `BEEP_CHANNEL` | htim2 / CH1 | 蜂鸣器所用定时器与通道 |
| `interface/beep.c` | `BEEP_IDLE_HIGH` | 1 | 1=静音输出高（低电平触发模块），0=反之 |
| `interface/encoder.c` | `ENCODER_DEBOUNCE_MS` | 30 | 按键稳定窗口 |
| `interface/encoder.c` | `ENCODER_STEPS_PER_DETENT` | 4 | 每个机械档位的正交边沿数 |
| `interface/encoder.c` | `ENCODER_ROTATE_GUARD_MS` | 150 | 旋转后忽略按键的互锁时间 |
| `interface/encoder.c` | `ENCODER_LONG_MS` | 700 | 长按判定时间 |
| `interface/oled.h` | `OLED_USE_SH1106` | 1 | 1=SH1106(1.3")，0=SSD1306(0.96") |
| `interface/esp32.c` | `ESP32_GetWeather()` 内 URL | 北京 | 天气经纬度 |

改经纬度时注意：**URL 里的逗号必须写成 `\\,`**，因为 AT 指令用逗号分隔参数，不转义会被 ESP32 截断。

---

## 7. 编译与烧录

1. **先配置 WiFi 凭据**（仓库里只有模板，缺这一步会编译报错）：
   ```bash
   cp app/net_config.example.h app/net_config.h
   # 然后编辑 app/net_config.h，填入真实 SSID 和密码
   ```
   `app/net_config.h` 已被 `.gitignore` 忽略，不会被提交。
2. Keil MDK 打开 `MDK-ARM/ESP32.uvprojx`
3. `F7` 编译（ARMCC V5.06，-O3），产物在 `MDK-ARM/ESP32/ESP32.hex`
4. 仿真器设 **SWD** 模式（PB4 被 DS1302 占用，JTAG 不可用），`F8` 下载
5. 串口助手接 PA9/PA10，**115200 8N1**，可看到启动流程和 AT 交互日志（`printf` 已重定向到 USART1）

当前编译结果：`Code=26394  RO-data=4850  RW-data=660  ZI-data=3972`，即 ROM 31.16KB / RAM 4.52KB，0 Error / 0 Warning。增量编译时若某些文件未被重新编译，`last line of file ends without a newline` 警告会时有时无，属正常。

改外设配置请用 CubeMX 打开 `ESP32.ioc` 重新生成，**只改 `USER CODE BEGIN/END` 之间**的内容，否则重新生成会被覆盖。

---

## 8. 目录结构

```
ESP32/
├── ESP32.ioc                CubeMX 工程（引脚/时钟/外设配置源）
├── .mxproject               CubeMX 生成缓存
├── Core/
│   ├── Inc/                 gpio/i2c/spi/tim/usart/main 头文件、HAL 配置
│   └── Src/                 初始化代码、中断入口、printf 重定向
├── Drivers/                 STM32F1 HAL + CMSIS（原样引入，不修改）
├── interface/               硬件驱动层
│   ├── oled.c/h/oledfont.h  SH1106 驱动 + 8x16 ASCII 字库
│   ├── ds1302.c/h           DS1302 三线时序
│   ├── SHT3x.c/h            SHT30 I2C 驱动（含 CRC8）
│   ├── esp32.c/h            AT 指令封装、SNTP、HTTP 天气
│   ├── encoder.c/h          正交编码器 + 按键状态机
│   └── beep.c/h             蜂鸣器 PWM 驱动 + 音符表播放
├── app/                     应用层
│   ├── net_app.c/h          联网调度、时间天气数据、首页文本区
│   ├── ui_app.c/h           三态导航、闹钟与专注状态机、各页绘制
│   ├── reminder_store.c/h   闹钟/专注设置存 DS1302 后备 RAM
│   ├── net_config.example.h WiFi 凭据模板（net_config.h 被 gitignore）
│   └── ui_assets.c/h        6 个 40x40 图标、8x8 状态图标、16x32 数字字模
└── MDK-ARM/                 Keil 工程与编译产物（产物已 gitignore）
```

---

## 9. 已知问题与改进方向

按优先级排列。

**① `interface/oled.c` 的中文注释已损坏（882 处乱码）**
该文件在 v1.2 编辑时被错误地做过一次 GBK→UTF-8 转换，中文注释全部变成 `U+FFFD` 替换字符，在 VS Code 和 Keil 里都显示为 `���ɵ�` 之类的乱码。**代码本身是 ASCII，功能不受影响**，编译也正常。
好消息：`5439882` 提交里的原始 GBK 版本是完好的，可按行比对恢复。这是当前仓库里最该修的一件事。

**② 联网操作阻塞主循环，界面卡住**
`ESP32_SendCmd()` 是忙等超时（`WaitFor`），期间 `UI_Process()` 不执行。最坏情况：
- 重连时 `AT+CWJAP` 超时 20s × 3 次 = **最长 60 秒界面无响应**
- 每次 SNTP 校时最长 15 秒（15 次 1 秒轮询）
- 天气请求 15 秒

现象是「隔一段时间按键突然失灵」。已有三层缓解：绝对截止时间兜住专注计时、5 分钟补响兜住闹钟漏检、响铃期间暂停联网请求保证铃声不断。但这都是补偿，不是根治——根治方案是把 AT 交互改成状态机（发送即返回，主循环里查结果）。

**③ WiFi 凭据管理（v1.5 已解决）**
v1.0~v1.4 曾把 SSID/密码硬编码在 `app/net_app.h` 并写入 git 历史。v1.5 已抽出到 `app/net_config.h`（`.gitignore` 忽略），并用 `git filter-repo --replace-text` 把全部历史中的明文替换为 `REDACTED_SSID` / `REDACTED_PWD`。
**注意**：所有历史提交号因此被重写。如果你的其他设备上还有旧克隆，那些副本仍带明文密码，需要重新克隆。

**④ 部分源文件仍是 GBK 编码**
`interface/oledfont.h` 仍是 GBK，其余主要源码已统一为 UTF-8。建议将剩余字库转为 UTF-8，并在 Keil 里设 `Edit → Configuration → Encoding = UTF-8`。

**⑤ CubeMX 重新生成会清空生成区**
v1.4 已把 `Encoder_Tick1ms()` 的调用移进 `USER CODE BEGIN/END SysTick_IRQn 1`，不会再被删掉。但每次改 `ESP32.ioc` 重新生成后，仍应确认这行还在——它是按键消抖的唯一节拍源。

**⑥ 显存仍整帧提交**
一帧只传一次 1KB GRAM，无变化不传输，已经够用。若后续要压帧率或降功耗，可再做脏区域局部刷新。

**⑦ 其他**
- `Draw_PageAbout()` 里的固件版本字符串仍是 `v1.3`，与当前 v1.4 不一致，需要手动同步
- 专注的「已完成轮数」`s_focusSessions` 只在 RAM，掉电清零（闹钟与专注时长本身已存 DS1302 后备 RAM）
- 无 DS1302 备用电池电压检测；电池耗尽时后备 RAM 一起失效，设置回落到默认值
- 天气备用接口（`current_weather=true`）不返回湿度，降级时湿度显示 `--`
- `MDK-ARM/ESP32.uvoptx` 被跟踪，Keil 每次增删文件或改窗口都会产生改动，建议 `git rm --cached` 后加入 `.gitignore`
- `Core/Src/main.c` 里的 `uint8_t buff[1000]` 未被使用，可删

---

## 10. 版本记录

| 版本 | 提交 | 内容 |
|---|---|---|
| v1.0 | `e7166d5` | 首个可用版本：三页界面、DS1302 走时、SNTP 校时回写、天气、SHT30、编码器交互、断网降级 |
| v1.1 | 未单独提交 | 扩展六页 GUI，新增每日软件闹钟、全屏提醒和可后台运行的专注倒计时 |
| v1.2 | `0d202dc` | 大时间首页、六项表驱动图标菜单、动态状态图标、任意坐标位图与统一显存提交（v1.1 改动一并提交） |
| v1.3 | `bcf8fd8` | 编码器改为完整正交解码 + 1ms 节拍消抖 + 旋转互锁；专注暂停时可旋转调时 |
| v1.4 | `b7faa38` | 蜂鸣器提醒（TIM2 PWM 音符表）、闹钟四模式与贪睡/自动重响/三阶段铃声、专注休息周期、设置存 DS1302 后备 RAM |
| v1.5 | `824b046` | WiFi 凭据移出源码，改由 gitignore 的 net_config.h 提供；历史中的明文凭据已清除 |

> 注：v1.5 之前的提交号在清除凭据时被整体重写，与旧文档中记录的不同。
