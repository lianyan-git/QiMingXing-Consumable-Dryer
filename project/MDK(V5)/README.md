# 启明星耗材烘干箱固件（STM32F103C8T6）

基于 STM32F103C8T6 + ESP-01S 的烘干箱固件，含 **Bootloader（OTA 升级）** 与 **App（烘干控制）** 两部分。

## 项目特性

- 🔥 **PTC 烘干控制**：PTC 加热 + 温度控制 + 过温保护 + PID 自整定
- 📦 **OTA 无线升级**：ESP-01S AP 模式 + 网页上传固件，无需数据线
- 🖥️ **1.14 寸 TFT 屏幕**：显示升级状态、AP 信息、进度条
- 📶 **Web 管理页面**：上传固件、查看状态
- 💾 **外部 Flash（W25Q128）**：固件暂存、参数持久化
- 🎛️ **多传感器**：温湿度（AHT20）、称重（CS1237）、NTC 温度检测
- ⚙️ **电机控制**：步进电机（连续/摆动模式）

## 目录结构

```
├── app/          App 应用（烘干控制、UI、WiFi 配置、Web 管理）
├── board/        板级抽象（引脚定义、看门狗、硬件初始化）
├── bootloader/   Bootloader（ESP01S AP 模式 OTA 升级）
├── bsp/          外设驱动（SPI/Flash/USART/TFT/RGB/传感器等）
├── module/       App 业务模块（OTA、WiFi、Web 服务器、系统时间等）
├── shared/       平台契约（Flash 分区地址、共享协议）
├── libraries/    标准外设库 + CMSIS
└── project/MDK(V5)/  EIDE/Keil 工程文件
```

## Flash 分区布局

| 区间 | 地址 | 大小 | 说明 |
|---|---|---|---|
| Bootloader | `0x08000000` | 18 KB | OTA 引导 + AP 升级入口 |
| 升级标志 | `0x08004800` | 1 KB | 升级请求标志 |
| WiFi 配置 | `0x08004C00` | 1 KB | WiFi 参数存储 |
| App | `0x08005000` | 44 KB | 烘干控制固件 |

## 编译

工程使用 **EIDE（Embedded IDE for VSCode）** 管理，编译器为 Keil ARMCC5（AC5）：

1. 用 VSCode 打开 `project/MDK(V5)` 目录（EIDE 扩展会自动识别 `eide.yml`）
2. 构建两个目标：
   - **Bootloader**：`Ctrl+Shift+B` 选择 `Build Bootloader`
   - **App**：`Ctrl+Shift+B` 选择 `Build APP`
3. 产物：
   - Bootloader：`Objects/Bootloader/dryer_bootloader.bin`
   - App：`Objects/APP/dryer_app.bin`

> 说明：Bootloader 目标编译时必须带 `BOOTLOADER_BUILD` 宏（eide.yml 已配置）；App 目标不带。两目标各自只有唯一的 `main` 入口，互不冲突。

## 如何使用本项目代码

### 开发环境搭建

| 工具 | 用途 | 获取方式 |
|---|---|---|
| VSCode | 代码编辑 + 编译 | https://code.visualstudio.com |
| EIDE 扩展 | 嵌入式工程管理/构建 | VSCode 扩展市场搜索 "Embedded IDE" |
| Keil MDK（ARMCC5/AC5） | 编译工具链 | Keil 官网安装 MDK v5（含 ARM Compiler 5） |
| PW Link / ST-Link | 烧录下载 | 硬件调试器 + 对应烧录软件 |

> EIDE 构建时编译器路径在 `project/MDK(V5)/.eide/env.ini` 中配置（如 `D:\keil\ARM\ARMCC\bin`），若你的 Keil 安装在别处，需修改该文件中的编译器路径。

### 导入工程

1. VSCode 打开工程目录：`File → Open Folder` 选择 **`project/MDK(V5)`**
2. 确保已安装 **EIDE** 扩展，它会自动加载 `.eide/eide.yml` 工程配置
3. 左侧 EIDE 面板应显示两个目标：**Bootloader** 和 **APP**（当前活动目标高亮）

### 构建

- **构建当前目标**：`Ctrl+Shift+B`（或右键 EIDE 面板目标 → Build）
- **切换目标**：点击 EIDE 面板顶部的目标下拉框选择 `Bootloader` 或 `APP`
- 构建产物（bin）输出到 `Objects/` 目录：
  - `Objects/Bootloader/dryer_bootloader.bin`
  - `Objects/APP/dryer_app.bin`
- 清除重编：EIDE 面板 → Clean / Rebuild

### 代码组织与常用入口

| 文件 | 作用 |
|---|---|
| `bootloader/bl_main.c` | Bootloader 主流程：开机判定 → 升级模式 / 跳转 App |
| `bootloader/bl_esp01s.c` | ESP-01S 通信：AT 命令、AP 配置、网页上传解析 |
| `bootloader/bl_tft.c` | Bootloader 屏幕驱动（ST7789 精简版） |
| `app/main.c` | App 主循环：初始化、传感器采集、控制逻辑 |
| `module/mod_wifi_manager.c` | App 侧 WiFi 管理 |
| `module/http_server.c` | App 侧 Web 服务器（管理页面） |
| `board/pin_config.h` | **全部引脚定义**（修改硬件设计需同步这里） |
| `shared/platform_contract.h` | Flash 分区地址等平台常量 |

### 自定义配置

**修改 WiFi 热点名称/密码**：编辑 `bootloader/bl_esp01s.c` 中的 `BL_ESP01S_StartAP()`：

```c
ESP_SendCmd("AT+CWSAP=\"QiMingXing\",\"12345678\",1,4", "OK", 800);
//                ^SSID^            ^密码^   ^通道^  ^加密:4=WPA/WPA2^
```

**修改引脚**：编辑 `board/pin_config.h`（例如更换 ESP 串口、屏幕引脚时）。

**修改 Flash 分区**：编辑 `shared/platform_contract.h` 中 `PLATFORM_APP_ADDR` 等常量，同时同步 Bootloader 与 App 两个目标的链接地址（`eide.yml` 的 `ro-base`）。

### 常见问题

**Q: 编译报错 `main` 重复定义？**
A: 确认当前目标定义正确。Bootloader 目标必须带 `BOOTLOADER_BUILD` 宏，App 目标不能带。若 `app/main.c` 的 `main()` 进了 Bootloader 链接，检查 `.eide/eide.yml` 的 excludeList 是否生效。

**Q: 找不到热点？**
A: ① 确认只烧了 Bootloader 或升级标志为 DOWNLOADED（否则 Bootloader 会直接跳 App 不开 AP）；② 确认 ESP-01S 供电正常（PA12 控制 AO3401 为 ESP 供电）。

**Q: 热点连上了但网页 404？**
A: 重新编译确保包含最新的 `BL_ESP01S_Process()` 解析修复（旧固件的 `+IPD` 解析 bug 会导致 404）。

**Q: 烧录后反复重启？**
A: 检查是否有 SPI/延时死循环未喂狗。若使用独立屏幕/Flash 驱动，确认其内部延时函数已 `Watchdog_Kick()`。

## 烧录

使用 PW Link / ST-Link 等烧录器，把两个 bin 分别烧到对应地址：

| 文件 | 地址 |
|---|---|
| `dryer_bootloader.bin` | `0x08000000` |
| `dryer_app.bin` | `0x08005000` |

烧录时务必确认：
- Bootloader 大小 ≤ 18 KB
- 两个文件地址不重叠（Bootloader 到 `0x08004FFF`，标志区 `0x08005000` 前）

## OTA 升级流程

### 进入升级模式

Bootloader 每次上电时：

1. 读取升级标志
2. **没有有效 App** 或 **升级标志为 DOWNLOADED** → 进入升级模式（开 AP）
3. 有有效 App 且无升级请求 → 正常跳转 App

因此有两种方式进入升级模式：

- **首次烧录**（只有 Bootloader、没有 App）→ 自动进入升级模式
- **App 内升级**：在 App 的 Web 管理页面点击"固件升级"→ 写入升级标志并复位 → 进入升级模式

### 连接并上传固件（两阶段）

**阶段 1 — 下载到外部 Flash**
1. 手机/电脑搜索 WiFi 热点 **`QiMingXing`**（密码 **`12345678`**）
2. 连接后浏览器访问 **`http://192.168.4.1`**
3. 选择 `.bin` 固件文件，点击 **UPLOAD & UPDATE**
4. 固件上传到外部 Flash（W25Q128）暂存，屏幕显示下载进度
5. 校验 CRC 后写入升级标志并自动重启

**阶段 2 — 拷贝到单片机**
6. 重启后 Bootloader 检测到升级标志，显示 "WRITE TO MCU" + 拷贝进度
7. 从外部 Flash 刷入 App 分区并校验
8. 完成后清标志并重启，跳转运行新 App

> 注意：Bootloader 进入升级模式后开 AP（`QiMingXing`），热点约数秒后出现。

## 更新日志

### 2026-09-15

#### 新增
- **开机语言字库引导页 + 在线字库上传**：无字库时上电自动进语言 AP 引导页（热点 QIMINGXING + 进度条 + 实时状态），手机上传 `font_lang.bin` 到外部 Flash，完成自动重启加载；AB 双区 + `LANG_FLAG` 有效标记防砖；`module/lang_ota`（`0xAA 0x55` 协议收库）+ `bsp/bsp_font_store`（外部字库渲染，无字库用内置 ASCII 回退自举）
- **设置页「更新字库」**：单击弹上传弹窗（开 AP → 传 → 自动重启）；传输中锁定、空闲可关
- **音乐播放 RGB 音高灯效**：按音符频率点亮灯带（中间起步、音高向两侧扩散，蓝→青→绿→橙）
- **内置 5 首新歌**：群青 / Bad Apple / LOVE_2000 / 恋爱吧少女 / 孤独摇滚（Music 目标生成 `Music.bin` 上传）

#### 变更
- **开机流程重排**：先基础初始化 + 探测外部 Flash → 查字库；**有字库才**加载参数 + 初始化传感器/加热，无字库走引导页（默认参数、不熄屏、外设不初始化）
- **PTC 加热授权机制**：`PTC_Enable/PTC_Disable` + `ptc_permit` 门控，上电默认禁加热，许可后 `PTC_SetPower` 才输出，停止/暂停/安全异常/自整定结束立即断

#### 修复
- **音乐上传卡 11%（主机侧）**：末段残页（不足 256B）永不落盘 → 有数据即写；上传结束前清除所有残留扇区
- **音乐上传 ACK 时序**：本包数据全部落盘后才补 ACK，避免 ESP 提前收 ACK 重发 seq 错序
- **语言上传卡 14% / 100% 不跳转**：`esp_link` 字节循环补喂 `LangOta_FeedByte`；`SIZE4` 补回握手 ACK；写页保留头部 `LANG_FLAG` 区擦除态；结束帧完整即提交 + 可重试
- **语言库“传输成功但重启仍引导页”**：`LANG_FLAG` 写入字节序修正（小端 `01 00 A5 A5`）
- **外部字库下全角标点方块**：「当前烘干预设为：」「上传失败，再击重试」的 `：，` 改 ASCII 半角 `:,`

### 2026-09-14

#### 新增
- **CAN 搜索设备交互**：搜索中/发现设备/连接中/连接成功 或 未发现设备/请重试，末态 2 秒后消失；仅主机显示搜索/已连接行，通讯关闭时仅显示开关+退出，光标跳过隐藏行
- 音乐列表长按清空音乐分区（外部 Flash）；WiFi 在线时音乐 AP 直接 APSTA 共存不断网

#### 变更
- 电机页选项顺序：次数/休息移到驱动上方；驱动切换自动整屏刷新
- 网页数据推送改"变化即推"（字段变化才发，空闲不持续推送）
- 全部光标页统一两行局部刷新（设置/菜单/电机/CAN/音乐/PID/预设/关于/WiFi/主屏卡片），消除整屏闪烁
- 调参旋转加速恢复（慢转 1 步、快转最多 6 步）；中文字库补齐至 179 字；WiFi 开关 Y 轴居中
- 音量/背光弹窗只刷进度条+数字，不再整卡重绘

#### 修复
- PTC 上电默认导通/停止后仍加热/功率乱跳：关闭 TIM1_CH1 预装载 + 上电强制拉低 PA8 + 显式 PTC_SetPower(0)
- NTC 温度偏移撤销（归零）
- 音乐上传进度卡 0（会话开始预擦除旧分区）；上传 AP 打不开（补 EspUart_Init）；弹窗卡 100% 不关闭（1.8s 自动收起）
- 音乐播放屏幕变暗+闪烁（背光满占空恒亮）、进度框未初始化变量
- CAN 页两行刷新 + 从机行隐藏；ui_manager 隐式声明警告

### 2026-08-16

#### 新增
- Bootloader 完整 OTA 升级链路：ESP01S AP 模式 + Web 网页上传固件（两阶段：下载到外部 Flash → 拷贝到 App 分区）
- 上传固件暂存外部 Flash（W25Q128）→ 校验（CRC32 + 向量表）→ 刷入 App 分区
- App 端 Web 页面"固件升级"按钮：写入升级标志并复位跳转 Bootloader
- `bl_tft.c` 增加真实 5×7 点阵字体，屏幕可显示 AP 名称/密码/IP/升级进度
- 屏幕适配 1.14 寸 135×240 ST7789 横屏（240×135），UI 完整显示标题/状态/进度/AP 信息

#### 修复
- **修复 `ESP_WaitResponse` / `BL_ESP01S_Process` 的 RX 读取逻辑反转 bug**（`EspUart_ReadByte` 返回 1=有字节，原代码写成 `==0`，导致 ESP 回复的每个字节都被丢弃、AT 握手永远失败）
- **修复固件上传 256 字节缓冲索引回绕 bug**（`fw_buf_idx` 从 `uint8_t` 改为 `uint16_t`，原 255+1 回绕导致 `flush_fw_buffer()` 永不触发、固件无法写入 Flash）
- **修复 HTTP multipart/form-data 未剥离 bug**（新增 `feed_fw_byte()` 解析 boundary，只把真实固件数据写入 Flash）
- **修复 ESP `+IPD,id,len:` 分片前缀被当固件数据写入的问题**（按分片模型解析）
- **修复 `Content-Length` 解析**：改为流式检测（大小写不敏感），容忍 ESP 调试输出（`_STA_IP:`/`RECV`/`send ok` 等）穿插
- 修复 ESP 残留 STA 配置导致的热点延迟问题（移除 `AT+RST`，上电只初始化一次，AP 更快广播）
- 修复 WS2812 时序错误（改用 SysTick 精确周期延时，符合 800kHz 协议）
- 修复 TFT 背光初始化缺失 `GPIOB` 时钟导致背光无法点亮
- 修复 TFT SPI 分频过高（36MHz→9MHz）、SPI 收发无超时导致看门狗复位
- 修复 `bl_tft.c` `Delay_ms` 不喂狗导致的复位循环
- `EspUart_Init` 不再在初始化时切换 ESP 电源，避免复位抖动
- `EspUart_SetEnabled` 适配 P 沟道 MOS（AO3401）低电平导通逻辑
- **App：AHT20 假驱动改为真实软件 I2C 驱动**（原固定返回 25°C/50%，会导致加热器持续全功率）
- **App：PTC 过温保护接入主控制链**（`NTC_IsOverTemp` 超 85°C 立即切断加热）
- **App：修复烘干倒计时快 5 倍**（200ms 控制周期累计满 1 秒才递减）
- **App：修复冷却状态进不去**（`if(!drying_active) return` 挡住 STATE_COOLING）
- **App：参数读取移到 Flash 初始化后** + 参数语义范围校验 + 保留字节清零 + 写入检查
- **App：NTC ADC 读取加超时保护**（防外设异常卡死主循环）
- **App：PTC PID 自整定峰值检测修复**（上升/下降沿跟踪）
- **App：步进电机摆动模式修复**（启动电机）+ 长距离步数支持（int32_t）

#### 变更
- Bootloader 入口切换为 V2 引导（ESP AP 升级），移除 V1 引导文件
- OTA 上传协议改为两阶段（下载到外部 Flash → 重启 → 拷贝到 App），支持校验后写入
- **Flash 分区调整**：Bootloader 16KB→18KB，App 地址 `0x08004800`→`0x08005000`，大小 44KB
- 热点名称 `QiMingXing`，密码 `12345678`
- 屏幕 UI 按横屏（240×135）重新布局：标题栏 / 状态区 / 进度条 / AP 信息（SSID 左、密码右、IP 居中）
- 屏幕文字居中显示（除 WiFi 名称、密码），标题彩色分段 "lianyan & -E-"

### 2026-08-18

#### 新增
- **主界面浅色主题**：浅灰背景 + 白卡片 + 灰描边 + 各卡片淡彩底色（温度暖橙 / 湿度冷青 / 重量紫 / PTC 红 / 时间浅绿），替代原深色主题
- **圆角描边**：新增 `draw_frame_rounded()`，四角圆弧像素落在 `[r-SEL_FRAME_W, r]` 圆环带内，卡片与菜单选中项圆角带完整灰色外轮廓
- **数值+单位统一绘制**：抽取 `draw_value_unit()` 公共函数，单位（℃/g）与数字同字号 size2、紧跟数值后，全屏与局部刷新共用
- **时间栏增强**：运行状态文字（IDLE/HEAT/DRY/COOL/DONE）+ REM 剩余时间显示
- **SPI1 TX DMA 局部刷新**：新增 `Spi1Bus_TransferDma()`（DMA1_Channel3），`TFT_FillRect` 大矩形（≥32 像素）走 DMA、小矩形走轮询
- **字体扩展**：`bsp_tft_st7789.c` 新增 `&` 与撇号 `'` 字形

#### 修复
- 开屏 "QiMingXing" X 居中（180px）；"LianYan & -e-" 居中（156px）
- ℃ 圆圈位置修正：° 置于 C 左上角、不重叠；C/g 颜色与数字同步
- 旋转编码器文字闪烁：值文字底色随 `selected_item` 动态选择 + `last_val[4]` 值缓存（值不变不重绘）+ `last_sel` 选中项切换检测
- `draw_card_pulse` 末行缩进错乱修复
