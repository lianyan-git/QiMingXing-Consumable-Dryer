# 启明星耗材烘干箱固件（STM32F103CBT6）

基于 STM32F103CBT6（兼容 C8T6）+ ESP-01S 的耗材烘干箱固件，分为 **Bootloader（OTA 无线升级引导）** 与 **App（烘干控制逻辑）** 两部分。

## 主控选型说明（C8T6 → CBT6）

> **C8T6 与 CBT6 同为 medium-density（MD）系列**（64 KiB / 128 KiB Flash，20 KiB RAM，48 引脚 LQFP）。
> 已将主控切换到 **CBT6（128 KiB）**：
> - 启动文件：均为 `startup_stm32f10x_md.s`，不变
> - 编译宏：均为 `STM32F10X_MD`，不变
> - **向量表偏移 `VECT_TAB_OFFSET=0x3400` 不变**（App 起点仍是 `0x08003400`，由 Flash 分区绝对地址决定，与芯片 Flash 容量无关）
> - Flash 分区已按 128 KiB 扩容：`PLATFORM_FLASH_SIZE=0x20000`、`PLATFORM_APP_SIZE=0x1CC00`（115 KiB，占满芯片）、`PLATFORM_FIRMWARE_ERASE_SIZE=0x1D000`；
>   `PLATFORM_APP_END == PLATFORM_FLASH_END` 断言继续成立
> - IROM：Bootloader `0x08000000`（12 KiB）、App `0x08003400`（115 KiB）；SRAM 20 KiB 不变
>
> 同步改动路径：`shared/platform_contract.h`、`project/MDK(V5)/.eide/eide.yml`（及根 `.eide/eide.yml`）的 App IROM size、
> `scripts/configure_keil_targets.py`（App size、128 KiB 下载算法校验）。
> Keil 侧使用方式：MDK Options for Target → Device 选 **STM32F103CB**；若用 `configure_keil_targets.py` 生成工程会自动带 128 KiB 算法。

## 项目特性

- 🔥 **PTC 烘干控制**：PTC 加热 + 温度控制 + 过温保护 + **PID 自整定**（5 次升降循环取均值，进度实时刷新，99% 落盘 → 100%）
- 📡 **OTA 无线升级**：ESP-01S AP 模式，浏览器网页上传固件，无需数据线
- 🖥️ **TFT 屏幕**：1.14 寸 ST7789（135×240），显示升级状态/AP 信息/进度条
- 💾 **外部 Flash（W25Q128，16 MiB）**：参数/用户数据持久化（WiFi 配置存 ESP 端），SFUD 驱动
- 🎛️ **多传感器**：温湿度（AHT20，非阻塞 10Hz 采样 + 校准位校验）、称重（CS1237）、NTC 温度检测
- ⚙️ **电机控制**：步进电机（连续/摆动模式），支持 TMC2208/2209（静音开关）
- 📦 **预设管理**：耗材预设二级菜单（编辑/新增/删除），名称唯一身份（仅新建可编辑），长按切换当前预设
- 🎵 **音乐播放器**：蜂鸣器（TIM3 方波）播放，内置曲库 + 用户"音乐固件"（MUB1 容器）上传存储外部 Flash；ESP-01S 专用上传 AP，网页选文件上传（进度条 + 自动关闭），列表长名走马灯、播放中实时进度
- 📡 **CAN 多级级联**：主机/从机集群（CAN 总线 500kbps，最多 16 台从机），主机搜索广播/从机入网即关 WiFi/500ms 推数据/3s 离线裁剪；网页端卡片展示全部在线设备，RUN/GLOBAL/PARAM_SET 群控
- 🌐 **Web 仪表板**：ESP-01S 托管仪表板页面（设备卡片/温度湿度曲线/预设管理/级联开关），JSON 行协议 + ACK 重发，变化即推（任一字段变化才发，空闲不持续推送）
- 🎨 **RGB 灯效**：呼吸灯、干燥进度灯（14%/颗）、彩虹流动，开机随屏幕渐亮启动
- 🔒 **温度安全监控**：锚定初始温度（≤35℃）温升判据 + 1℃ 裕度防抖跌落保护，防误报箱体破损

## 环境要求

| 工具 | 用途 | 获取方式 |
|---|---|---|
| VSCode | 代码编辑 + 编译 | https://code.visualstudio.com |
| EIDE 扩展 | 嵌入式工程管理/构建 | VSCode 扩展市场搜索 "Embedded IDE" |
| Keil MDK（ARMCC5/AC5） | 编译工具链 | Keil 官网安装 MDK v5（含 ARM Compiler 5） |
| PW Link / ST-Link | 烧录下载 | 硬件调试器 + 对应烧录软件 |

> EIDE 构建时编译器路径在 `project/MDK(V5)/.eide/env.ini` 中配置（如 `D:\keil\ARM\ARMCC\bin`）。
> 若 Keil 安装在别处，需修改该文件中的编译器路径。

## 目录结构

```
├── app/              App 应用（烘干控制、UI、WiFi 配置、Web 管理）
├── board/            板级抽象（引脚定义、看门狗、硬件初始化）
├── bootloader/       Bootloader（ESP01S AP 模式 OTA 升级）
├── bsp/              外设驱动（SPI/Flash/USART/TFT/传感器等）
├── module/           App 业务模块（OTA、WiFi、Web 服务器、系统时间等）
├── shared/           平台契约（Flash 分区地址、共享协议）
├── libraries/        标准外设库 + CMSIS
└── project/MDK(V5)/  EIDE/Keil 工程文件
```

## Flash 分区布局（内部 Flash，128 KiB）

主控 STM32F103CBT6，内部 Flash 共 128 KiB，地址范围 `0x08000000` ~ `0x0801FFFF`（C8T6 64 KiB 时 App 段溢出版本需回缩 `PLATFORM_APP_SIZE`；当前项目固定按 CBT6）。布局如下：

| 区间 | 地址 | 分区大小 | 当前固件 | 说明 |
|---|---|---|---|---|
| Bootloader | `0x08000000` | 12 KiB（`0x3000`） | ~11.4 KiB | ESP 网页 OTA 升级引导 |
| 升级标志 | `0x08003000` | 1 KiB（`0x400`） | - | 升级请求标志 + 版本信息 |
| App | `0x08003400` | 115 KiB（`0x1CC00`） | - | 烘干控制固件（WiFi 参数已交给 ESP01S 存储，分区并入 App） |

分区校验（编译期静态断言）：
- Bootloader 结束 = 标志区起始 ✓
- 标志区结束 = App 起始 ✓
- App 结束 = Flash 末尾 ✓
- 各分区大小均为页大小（1 KiB）整数倍 ✓

> 注意：OTA 固件由 Bootloader 校验通过后**直写内部 App 分区**，不再经外部 Flash 暂存。

## 外部 Flash 布局（W25Q128，16 MiB）

W25Q128 外部 Flash 共 16 MiB，用于参数存储、用户数据等：

| 区间 | 地址 | 大小 | 说明 |
|---|---|---|---|
| 用户数据 | `0x00000000` | 12 MiB | 参数/日志等持久化数据（含 WiFi 配置） |
| 保留区 | `0x00C00000` | ~3.94 MiB | 预留（OTA 已改为直写内部 Flash，不再使用） |
| 元数据 | `0x00FF0000` | 64 KiB | 升级元数据（主/备份） |

## 编译

### 导入工程

1. VSCode 打开工程目录：`File → Open Folder` → 选择 **`project/MDK(V5)`**
2. 确保已安装 **EIDE** 扩展，自动加载 `.eide/eide.yml` 配置
3. EIDE 面板显示两个目标：**Bootloader** 和 **APP**（当前活动目标高亮）

### 构建两个目标

| 目标 | 操作 | 产物 |
|---|---|---|
| Bootloader | `Ctrl+Shift+B` → `Build Bootloader` | `Objects/Bootloader/dryer_bootloader.bin` |
| App | `Ctrl+Shift+B` → `Build APP` | `Objects/APP/dryer_app.bin` |

- Bootloader 目标带 `BOOTLOADER_BUILD` 宏（`eide.yml` 已配置），App 目标不带
- 两目标各自只有唯一的 `main` 入口，互不冲突
- 清除重编：EIDE 面板 → Clean / Rebuild

### 切换目标

点击 EIDE 面板顶部的目标下拉框，选择 `Bootloader` 或 `APP`，或直接运行对应任务。

## 烧录

使用 PW Link / ST-Link 等烧录器，将两个 bin 分别烧到对应地址：

| 文件 | 烧录地址 |
|---|---|
| `dryer_bootloader.bin` | `0x08000000` |
| `dryer_app.bin` | `0x08003400` |

烧录确认：
- Bootloader 当前 ~11.9 KiB（12160 字节），分区上限 12 KiB，不超限
- 两个文件地址不重叠（Bootloader 到 `0x08002FFF`，标志区从 `0x08003000` 开始）

## 代码入口

| 文件 | 作用 |
|---|---|
| `bootloader/bl_main.c` | Bootloader 主流程：开机判定、升级模式、跳转 App |
| `bootloader/bl_esp01s.c` | ESP-01S 通信：AT 命令、AP 配置、OTA 二进制串口协议接收（包级 ACK/NAK + CRC16/CRC32） |
| `bootloader/bl_tft.c` | Bootloader 侧屏幕驱动（ST7789 精简版，含 5×7 点阵字体） |
| `app/main.c` | App 主循环：初始化、传感器采集、控制逻辑（PID 自整定状态机驱动、倒计时、安全监控） |
| `app/ui_manager.c` | 全部界面绘制/交互：主界面卡片、菜单、预设二级菜单、PID 校准页（含 AIR/PTC 实时温度）、确认弹窗 |
| `app/system_config.c` | 参数持久化（外部 Flash，magic/版本/校验/范围校验）、预设/设备参数存取 |
| `module/esp_link.c` | App 运行时 ESP-01S 链路层（行协议 + JSON 命令分发/ACK/1Hz 数据推送）、Web 仪表板配套 |
| `module/can_cluster.c` | CAN 集群协议：主机搜索/从机加入/心跳/数据推送/离线裁剪 + 群控 |
| `module/music_play.c` | 音乐播放器：TIM3 方波按音符改 ARR，内置曲目（`music_data`）+ 外部固件曲目（`music_store`） |
| `module/music_store.c` | 音乐固件外部 Flash 存储（MUB1，MUSIC 分区 `0xC20000`），非阻塞擦/写 |
| `module/music_ota.c` | 网页上传音乐固件接收（`0xAA 0x55` 二进制串口协议）→ 写外部 Flash |
| `app/zh16.h` | 16×16 中文点阵字库（179 字，Unicode 码点索引，`TFT_DrawStringZh` 渲染） |
| `bsp/bsp_can.c` | CAN 总线驱动（bxCAN1，PB8=RX/PB9=TX，500kbps 标准帧） |
| `bsp/bsp_encoder.c` | 编码器输入：1kHz SysTick 采样不丢步 + 旋转加速、按键/长按处理 |
| `bsp/bsp_aht20.c` | AHT20 温湿度：非阻塞 10Hz 采样、上电 40ms 延时、初始化校准位校验 + 软复位重试 |
| `bsp/bsp_cs1237.c` | CS1237 称重：软件时序、4 帧中值滤波、DOUT 上拉、量程拒收、去皮（开机窗口限流） |
| `bsp/bsp_ptc.c` | PTC 加热 + PID 自整定（元件/空气两套，5 峰 ZN 法）、过温保护 |
| `bsp/bsp_rgb_led.c` | WS2812 RGB 灯效：呼吸灯、干燥进度、彩虹、校准状态灯 |
| `board/pin_config.h` | **全部引脚定义**（修改硬件设计需同步） |
| `shared/platform_contract.h` | Flash 分区、地址、平台常量（编译期断言验证） |

## 预设管理

耗材预设页面采用**二级菜单**结构：主菜单为「编辑预设 / 新增预设 / 删除预设 / 退出」四行；进入编辑/删除后为预设列表（超过 5 项自动出现滚动条），列表底部固定「退出」返回上一级。

- 预设由 **名称 + 温度 + 时间** 组成；**名称是唯一身份，仅新建时可编辑**，已保存预设的名称行光标不可选
- 列表中**长按预设 = 切换为当前预设**并持久化（温度/时间同步应用）
- 编辑页保留单一「退出」按钮：**有改动才弹"确认保存"**，无改动直接返回；保存后回到预设列表并停在当前行
- 时间弹窗数字间距/衬底/冒号对齐烘干时间卡二级菜单样式，底部实时显示「当前烘干预设为：XXX」

## PID 自整定

烘干控制有两套 PID：**元件 PID（pid_ntc_*）** 与 **空气 PID（pid_air_*）**。PTC 设置页的「PID自动校准」校准元件组，温度页的校准校准空气组；「PID调整」页按入口显示对应一组（PTC 页进入→元件组，温度页进入→空气组）。

自整定流程（继电反馈 + ZN 公式）：

1. 风扇锁定 100%，PTC 满功率 → 升到 `PTC_MAX−3℃` 断电 → 风冷降到 `PTC_MAX−8℃` 再升温（元件组）；空气组摆幅为目标 `−1℃ ↔ −5℃`
2. 峰谷检测（0.3℃ 滞回 + 极值锁存，每组峰检测状态启动时清零）累计 **5 个计数峰**，进度每峰 **20%** 实时刷新
3. 第 5 峰 → 进度 **99%**：后台按相邻周期/幅值均值计算 `Ku=4×100/(π·A)`，`Kp=0.6Ku`、`Ki=2Kp/Tu`、`Kd=Kp·Tu/8`，写入参数文件
4. 写入完成后显示 **100%** 并驻留 1.2s（此时 PID 调整页已可看到新值），随后退出校准态
5. 超时/160℃ 过温中止时，若已有 ≥2 组振荡数据也按均值落盘，避免整轮白跑

两个校准页右上角实时显示 `AIR xx.x / PTC xx.x`（与桌面卡片同源），方便观察升温进度。

## 自定义配置

### 修改 WiFi 热点

需要编辑 ESP01S的固件，固件见主页另一个仓库

### 修改引脚

编辑 `board/pin_config.h`（例如更换 ESP 串口、屏幕引脚时）

### 修改 Flash 分区

编辑 `shared/platform_contract.h` 中的 `PLATFORM_APP_ADDR` 等常量，同时同步：
- `project/MDK(V5)/.eide/eide.yml` 中两个目标的 `ro-base` 和 IROM 大小
- Keil 工程（`Project.uvprojx`）中 `<Cpu>` 的 IROM 参数
- 运行 `scripts/configure_keil_targets.py` 自动同步 Keil 工程

## OTA 升级流程

> OTA 链路采用 **自定义二进制串口协议**（非浏览器直传）：网页/HTTP 解析全部由 ESP-01S 完成，STM32 只在 UART 上按 1 KiB/包的二进制协议收固件并**直写内部 App 分区**，因此即使 STM32 仅 20 KB RAM 也能稳定升级。

### 进入升级模式

Bootloader 每次上电时执行以下判断：

1. 读取升级标志
2. **无有效 App** → 进入升级模式（开 AP 热点，等待上传）
3. 有有效 App 且无升级请求 → 正常跳转 App
4. **兜底：上电长按编码器按键**（约 3 秒窗口，PB5 按下为低电平）直接进入下载模式，无论 App 是否有效 —— 避免 OTA 写入坏固件后"变砖"无法再进升级界面

两种方式进入升级模式：

- **首次烧录**（只烧 Bootloader，没有 App）→ 自动进入升级模式
- **App 内升级**：App 的 Web 管理页面点击"固件升级"→ 写入升级标志并复位 → 进入 Bootloader 升级模式
- **强制下载**：设备上电时长按编码器按键 → 强制进入升级模式

### 上传固件流程（单阶段直写内部 Flash）

**阶段 — 浏览器 → ESP-01S → STM32 → 内部 App 分区**

1. 手机/电脑搜索 WiFi 热点 **`QiMingXing`**（密码 **`12345678`**），约数秒后出现
2. 连接后浏览器访问 **`http://192.168.4.1`**
3. 选择 `.bin` 固件文件，点击 **上传并更新**
4. 浏览器先在前端用 JS 计算文件 CRC32 并随 `?crc=` 上传；ESP 收到后**再次计算 CRC32**，若与浏览器上报值不一致则判定为 WiFi 上传链路污染，拒绝转发（返回 `FIRMWARE CRC MISMATCH`），避免坏固件进入 STM32
5. 校验通过的固件由 ESP 按二进制协议经 UART 转发给 STM32：
   - **握手**：ESP → STM32 `[0xAA 0x55 0x01] + [4 字节固件大小 大端]`；STM32 收到后**先整区擦除内部 App 分区**（约 2s），擦完回 `0x06`(ACK)（ESP 侧握手等待已放宽到 10s，擦除期间主机不发包）
   - **数据包**：每 1 KiB 一包 `[0xAA] + [2 字节包序号大端] + [≤1024 数据] + [2 字节 CRC16(Modbus)] + [0x55]`，STM32 回 `0x06`(ACK) 或 `0x15`(NAK 重传)
   - **结束**：ESP → STM32 `[0xAA 0x55 0x02] + [4 字节总 CRC32 大端]`，STM32 回 ACK
   - STM32 **边收边直写内部 Flash App 分区**（按 256 字节页编程），屏幕进度条每 1% 差分局部刷新
6. 收完后 STM32 **整包 CRC32 全量校验**（从内部 Flash 重算比对 + 向量表校验），通过后关闭网页，设备自动重启进入新 App

> 升级全程无需人工干预。包级 ACK/NAK + 整包 CRC32 双重防护。误入升级模式但未上传固件时**不会擦除 App**，旧固件保留。

### ESP-01S 自定义固件（独立仓库）

ESP-01S 已替换为自定义 Arduino 固件（`QiMingXing-ESP01S` 仓库），不再依赖官方 stock AT。自定义指令：

| 指令 | 说明 | STM32 用法 |
|---|---|---|
| `AT` | 探测 ESP 就绪，回 `OK` | Bootloader 上电先发 `AT` 等 `OK` |
| `AT+OTAAP` | 开 SoftAP(`QiMingXing`/`12345678`) + 网页上传固件，上传完按二进制协议转发给 STM32 | Bootloader 发 `AT+OTAAP\r\n`，等 `OK\r\n` 后进入 UART 接收状态 |
| `AT+CFGAP` | 开配网 AP，网页选周边 WiFi 并回 `+IP:xxx.xxx.xxx.xxx` | 需配网时发 `AT+CFGAP\r\n` |
| `AT+PUSHDATA=<str>` | 缓存数据，数据展示页每 2 秒轮询显示 | App 定时发送 |
| `AT+OTACLOSE` | 关闭所有 Web Server，ESP 进入 Modem-Sleep 低功耗（**由 STM32 控制时机**） | 固件升级完成/无需网络时发送 |

## 常见问题

**Q: 编译报错 `main` 重复定义？**
A: 检查目标宏。Bootloader 必须带 `BOOTLOADER_BUILD` 宏，App 目标不能带。若 `eide.yml` 的 excludeList 未正确排除，会导致 `app/main.c` 的 `main()` 进入 Bootloader 链接

**Q: 找不到热点？**
A: 插拔一下电源

**Q: 热点连上了但网页打不开？**
A: 确认已烧录**自定义 ESP-01S 固件**（`QiMingXing-ESP01S` 仓库），网页由 ESP 自身托管，不再依赖 Bootloader 的 `+IPD` 解析。Bootloader 只负责在 `AT+OTAAP` 后按二进制协议收固件

**Q: TFT 屏幕不亮？**
A: 确认背光引脚（PB0）配置为推挽输出并置高。若硬件上背光 MOS 已拆除，需检查背光有没有3.3V

## 更新日志

### 2026-09-15

#### 新增
- **开机语言字体引导页 + 在线字库上传**：上电检测不到有效字库时自动进入语言 AP 引导页，屏显 热点（QIMINGXING / 192.168.4.1）/ 上传页 与**大进度条 + 实时状态**（`待机 → 握手 → 接收 → 完成`）；手机连热点上传 `font_lang.bin` 到外部 Flash 语言分区，完成**自动重启**加载新字库；`module/lang_ota.c/h` 复用 OTA 同款 `0xAA 0x55` 二进制串口协议（帧型 `0x13` 数据 / `0x14` 结束 + 全文件 CRC32）；**AB 双区**（`0x140000` / `0x1A0000`，120KiB/区）+ 头部 `LANG_FLAG` 有效标记：新库写 B 区成功才切标志，失败/中断保留 A 区不砖机；`bsp/bsp_font_store.c/h` 外部字库渲染（ASCII 5×7 区 + 汉字区 + 字典表，`LangGetAscii/LangGetGlyph` O(1) 查表），引导页/无字库时用 `TFT_DrawChar` 内置 ASCII 回退字体自举（原内部 `zh16.h` 已删）
- **设置页「更新字库」入口**：设置页新增一行，单击弹出语言上传弹窗（与音乐弹窗同风格：单击开语言 AP → 显示热点信息 → 手机上传 → 完成自动重启）；无字库引导页锁定全部输入，设置页入口**传输中锁定、空闲可关**（未开始上传可退出）
- **音乐播放 RGB 音高灯效**：播放音乐时按音符频率点亮底部 RGB 灯带——中间起步、音高向两侧扩灯，色相 蓝→青→绿→橙 随音高过渡，正弦呼吸亮度；`RGB_MusicPitch(freq)`，播放中覆盖其它灯效
- **内置 5 首新歌（Music 目标）**：群青 / Bad Apple / LOVE_2000 / 恋爱吧少女 / 孤独摇滚；`tools/gen_music_fw.py` 从源码谱/五线谱转 MUB1，`module/music_fw.c` 汇总 5 首（构建 **Music** 目标生成 `Music.bin` 经网页上传到列表）

#### 变更
- **开机流程重排**：上电先做基础初始化（含外部 Flash 探测 / 清写保护），再检查字库——**有字库才**加载设置参数并初始化 I2C 温湿度 / 加热 / 风扇 / NTC / 称重 / 电机；无字库走语言引导页（用代码默认参数、不熄屏、外设不初始化），引导页不再因外部参数提前熄屏
- **PTC 加热授权机制**：新增 `PTC_Enable / PTC_Disable` + `ptc_permit` 门控，上电默认禁止加热；仅烘干开始 / 自整定启动时 `PTC_Enable` 后 `PTC_SetPower` 才输出，停止 / 暂停 / 安全异常 / 自整定结束时 `PTC_Disable` 立即关断；PA8 上电保持普通推挽低电平，首次请求 >0% 才切 `TIM1_CH1` AF 复用（杜绝上电随机导通）

#### 修复
- **音乐上传进度卡 11% 不动（主机侧）**：`music_store.c` 页缓冲原先只在**整页 256B 满**时才写盘，末段**不足 256B 的残页永不落盘、卡死** → 改为有数据即写；上传结束前**清除所有残留扇区**（已写数据末尾 → 文件尾），解决旧曲目更长的脏扇区残留污染 CRC16/seq
- **音乐上传 ACK 时序**：`music_ota.c` 改为**本包数据全部落盘后才补发 ACK**（原 spill 缓冲清空即发），避免 ESP 提前收到 ACK 重发导致 seq 错序 → NAK 失败
- **语言上传卡 14% / 到 100% 不跳转**：`esp_link.c` 字节循环补 `LangOta_FeedByte`（原先只喂 music，语言帧到不了状态机）；`lang_ota.c` `S_SIZE4` 补回 `ack(1)`（握手立即 ACK，懒擦改造时误删）；写页保留头部 `HDR_FLAG`（偏移 26）区为擦除态 `0xFF`；结束帧 `S_END_CRC4` 改**完整即提交**（每包 CRC16 已逐包校验，结束 CRC32 不一致仅记录、不致命）+ 失败可重试 + 结束等待 20s 超时兜底
- **语言库“传输成功但重启仍回引导页”**：`LangMarkValid` 写入的 `LANG_FLAG` 字节序写反（原 `A5 A5 00 01`）→ `LANG_FLAG_READY=0xA5A50001` 小端应为 `01 00 A5 A5`，修正后 `LangInit` 才能识别新写入的字库
- **外部字库下部分标点渲染为方块**：「当前烘干预设为：」「上传失败，再击重试」中的全角 `：` `，` 不在字库符号表 → 改为 ASCII 半角 `:` `,`

### 2026-09-14

#### 新增
- **CAN 搜索设备交互**：CAN 设置页「搜索设备」行默认不显示状态文字；单击（仅主机 + 通讯开）向 CAN 网络广播搜寻，状态序列：`搜索中 → 发现设备 → 连接中 → 连接成功`（已连接设备数 +1）或 `搜索中 → 未发现设备 → 请重试`，末态显示 2 秒后自动消失；「搜索设备 / 已连接设备」两行仅主机模式显示，**通讯关闭时仅显示「CAN通讯 / 退出」**，光标自动跳过隐藏行
- **音乐列表长按清空音乐分区**：长按编码器擦除外部 Flash 音乐分区全局头，列表立即为空（内容仅存外部 Flash，重烧 App 不消失）
- **WiFi 在线时音乐 AP 共存**：ESP 已在线（STA）时开启音乐上传直接切 APSTA（AP + WiFi 共存，不断网），仅离线/挂死才冷启动

#### 变更
- **电机页选项顺序**：「次数」「休息」移到「驱动」上方（联动/方向/速度/摆动/角度/次数/休息/驱动/电流/静音）；A4988 无电流项跳过逻辑同步更新
- **电机页驱动切换自动刷新**：编辑驱动（A4988↔TMC2208/2209）或退出编辑时立即重绘，行数/选项即时更新
- **网页数据推送改"变化即推"**：不再每秒无条件推送，仅任一字段变化（温度/湿度/重量/PTC/剩余时间/开关/设定值）才发送一次；网页改参数立即回推、主机改参数下一秒推、运行中剩余时间每秒变化每秒推、新会话首推全量
- **全部光标页统一两行局部刷新**：设置/菜单/电机/CAN/音乐主页/音乐列表/PID/预设菜单/预设编辑/关于/PTC 编辑×2/WiFi/主屏卡片，光标移动只重绘"旧行+新行"，不再整屏/整视口重绘（消除闪烁）；设置页选中残留修复（先清背景再绘制）；主屏卡片光标此前高亮不刷新（隐藏 bug）一并修复
- **调参旋转加速恢复**：亮度/角度/速度/温度/PID 等调参页步进与转速同步（慢转 1 步、快转最多 6 步），蜂鸣器音量(0-10)与熄屏超时(0-8)区间小保持 1 步
- **中文字库补齐至 179 字**：42 个自制字模统一替换为原库风格宋体字（137→179，含音乐/设置/CAN 相关文字）
- **WiFi 开关 Y 轴居中**：开关(16px)在行(18px)内居中显示
- **音量/背光弹窗局部刷新**：打开时整卡绘制一次，调值只刷新进度条+数字，不再整卡重绘闪烁

#### 修复
- **PTC 上电默认导通 / 停止烘干后仍加热 / 烘干中功率乱跳**：`bsp_ptc.c` 关闭 TIM1_CH1 预装载（CCR 写入立即生效）、初始化末尾显式 `PTC_SetPower(0)` + 强制更新事件、`PTC_Init` 开头先把 PA8 配普通推挽强制拉低——三重保险保证上电即关闭加热器
- **NTC 温度偏移撤销**：上一版加的 +2.0℃ 偏移导致温湿度传感器显示比 PTC 低 2 度，已归零（如需校准按参考温度计调 `NTC_CAL_OFFSET_10C` / `SHT40_TEMP_OFFSET_10C`）
- **音乐上传进度卡 0**：上传会话开始（`+MUSICAP`）即预擦除旧固件分区（头扇区 + 旧数据扇区），收数据时不再因擦除卡死
- **音乐上传 AP 打不开**：`EspLink_MusicOpenAp` 补 `EspUart_Init()`（WiFi 本轮从未开过时 UART 未初始化导致 AP 起不来）；AP 未就绪时再点 = 重新冷启动重试（不再误当"取消"关闭）
- **音乐上传弹窗卡 100% 不关闭**：完成/失败态进入时开始 1.8s 倒计时自动收起（修复 `last_ms==0` 永不关闭死角）
- **音乐播放屏幕变暗 + 闪烁**：背光(PB0/TIM3_CH3)与蜂鸣器(PB1/TIM3_CH4)共用 TIM3，播放期间背光固定满占空恒亮（亮度档≥71% 恒亮无闪），播完恢复设定亮度；音乐列表进度框未初始化变量导致的随机蓝块/闪烁一并修复
- **CAN 页整屏刷新 / 从机模式行显示残留**：改为两行局部刷新，主从切换后自动重绘隐藏行
- **`ui_manager.c` 函数隐式声明警告**：补 `CAN_Cluster_RequestSearch` 声明

### 2026-09-13

#### 新增
- **音乐播放功能（Music）**：蜂鸣器（TIM3 方波）旋律播放，音符表 `{freq_hz, dur_ms}`（freq=0 休止）；内置曲库 `module/music_data.c`（周杰伦《晴天》主旋律，编译宏 `music` 控制）；支持**独立"音乐固件"（MUB1 自描述容器：36B 头 + 曲目表 + 名称区 + 音符区）经 ESP-01S 专用上传 AP + 网页上传到外部 Flash**（`MUSIC` 分区 `0xC20000`），曲名（中文 UTF-8）与音符表全部随固件存外部 Flash；音乐列表长名**走马灯**滚动、播放中**实时进度**；`music_ota.c` 复用 OTA 同款 `0xAA 0x55` 二进制串口协议收歌；EIDE 新增 **Music** 构建目标：`module/music_fw.c` + `project/MDK(V5)/music.sct` 生成标准 MUB1 `Music.bin`
- **CAN 多级级联（can_cluster + bsp_can）**：新增 CAN 总线驱动 `bsp/bsp_can.c`（bxCAN1，PB8=RX / PB9=TX，500kbps 标准帧，命令帧全收 FIFO0）；集群协议：主机广播搜索（ID `0x100`）/ 从机加入应答（`0x101`）/ 主机槽位确认（`0x102`，最多 **16 台**从机）/ 从机数据帧（`0x180|槽位`：空气温度/湿度/重量/PTC 温度/烘干时间/剩余时间/运行状态）；从机入网**即时关 WiFi**（纯 CAN 数据传输）、500ms 推数据、主机 3s 无数据即从在线列表裁剪；网页端卡片展示全部在线设备，`RUN`/`GLOBAL`/`PARAM_SET` 群控
- **esp_link ESP 链路层 + Web 仪表板**：新增 App 运行时 ESP-01S（自定义 AT 固件）**行协议链路层** `module/esp_link.c/h`，`AT+WEBSTART/CFGAP/CFGCLR/WEBCLOSE`(其后 STM32 断电) + JSON 行收发；Web JSON 命令 `HELLO/PRESET_GET|SAVE|DELETE|APPLY/PARAM_SET/RUN/GLOBAL/CAN_MODE`，STM32 回 ACK 并 **1Hz 推 master + 在线从机 DATA 行**、按需回 `PRESET_LIST`；配套 Web 仪表板页面（`QiMingXing-ESP01S` 仓库 `web.txt`）：设备卡片 / 温度湿度趋势曲线 / 预设管理 / 级联开关，WS + ACK 重发
- **中文字库扩容（zh16.h）**：新增 **28 个宋体 16×16 字模**（乐/曲/表/播/放/进/失/败/热/再/试/部/闭/仅/指/入/接/搜/索/未/示/系/编/通/讯/该/连/辑），供音乐页与通信页使用；`ZH16_COUNT` 137→165，字体风格与原库统一

#### 变更
- **烘干倒计时启动条件放宽**：加热阶段由"空气温度达设定值"才开始倒计时，改为"**≥ 设定值 −0.5℃**"即进入 `STATE_DRYING` 开始倒计时；`ResumeDrying` 回热判定阈值同步从 −1.0℃ 改为 −0.5℃
- **空气 PID 稳定带（抑制超调）**：空气温度超过"设定值 +0.5℃"时把空气设定点拉低到"设定值 −0.5℃"，将温度拉回 `[设定−0.5, 设定+0.5]` 区间，解决"实际超过烘干温度一度多"的超调；稳定范围 ±0.5℃
- **音乐页 AP 交互**：「上传音乐」改为**单击直接开启上传 AP**（弹窗即时显示"连接热点 QIMINGXING 192.168.4.1 / 点击关闭"），**再单击关闭 AP 并关弹窗**；`popup=1` 仅保留给网页端取消上传（ESP 回 `+MUSICCLOSED`）后的待机态

#### 修复
- **Web 仪表板"初始化失败"**：仪表板 JS 的 `charts` 变量从未 `var` 声明，页面加载 `init()` 读 `charts[k]` 抛 `ReferenceError` 被 `init()` catch 捕获 → 显示"初始化失败"。`web.txt` 顶层补 `var charts={};`，`web2c.py` 重建 `web_page.h`
- **TFT 汉字丢失（字库编码混杂）**：`ui_manager.c` 曾因 GBK 往返弄坏 UTF-8 字符串段（汉字变乱码/消失）→ 整文件统一为合法 UTF-8 并加 BOM，`armcc` 按 UTF-8 解析，49 处损坏字面量逐条修复
- **音乐上传弹窗卡 100% 不关闭**：上传完成（`+MUSICOK`）后自动关闭弹窗（`music_popup=0`），不再停留在"上传中/100%"；音乐列表实时读外部 Flash，新曲目上传后立即可见
- **ESP-01S 配网存参持久化**：`save_cfg` 布局重叠导致真断电冷启动后 WiFi 失效 → 数据区起点修正、`CFG_MAGIC` 升版，断电重启保留配置

### 2026-09-08

#### 新增
- **预设二级菜单**：耗材预设主菜单改为「编辑预设 / 新增预设 / 删除预设 / 退出」；编辑/删除为二级列表（>5 项自动滚动条、底部「退出」返回上一级）；列表内**长按 = 切换当前预设**并持久化；预设名称唯一身份（仅新建可编辑）
- **PID 自整定 5 峰 ZN 法**：元件（`pid_ntc_*`）与空气（`pid_air_*`）两套，各跑 **5 个升降循环**取周期/幅值均值，进度每峰 **20%** 实时刷新；第 5 峰 → **99%**（后台计算并写入参数文件）→ **100%** 驻留 1.2s；超时/160℃ 过温中止前若有 ≥2 组振荡数据也按均值落盘
- **校准页实时温度**：两个 PID 校准页右上角显示 `AIR xx.x / PTC xx.x`（与桌面卡片同源，0.1℃ 变化局部刷新）
- **PID 调整页按入口区分参数组**：PTC 页进入显示/编辑元件 PID，温度页进入显示/编辑空气 PID（此前恒显示空气组，导致"校准后值没刷新"的错觉）
- **AHT20 非阻塞 10Hz 采样**：状态机触发→读回自动衔接，主循环不再阻塞；上电 40ms 延时、初始化后读状态寄存器校验校准位（bit3），未就绪软复位重试
- **编码器 1kHz 相位采样**：SysTick 中断采样 A/B 相不丢步，阻塞期间转过的格数补发（解决"转好几圈才动一次"）
- **外部 Flash 引入 SFUD 驱动**：`bsp/sfud*` 通用 Flash 库

#### 修复
- **称重被"自动清零"**：烘干中电桥温漂越过阈值触发自动去皮，把盘上真实物料（240g）误当新零点清掉 → 自动去皮限为开机 6~25s 空闲窗口，烘干中绝不触发；DOUT 上拉、量程（±8,000,000 LSB）拒收、4 帧中值滤波
- **AHT20 读数不准/跳变**：测量等待不足（原 ~16ms ≪ 80ms）读到忙位/半更新数据 → 状态机满 80ms 再读
- **PID 校准"50% 后消失、参数没写入"**：峰检测 static 跨次残留 + 空气校准降温阶段风扇被关（元件余热顶温、自然冷却极慢凑不满周期）→ 启动清零检测状态、降温保持风扇 100%、5 峰流程落盘
- **刚烘干就报"箱体破损"**：温升监控跌落判据过苛（比初始锚点低 0.1℃/300ms 即报）→ 改为 1℃ 裕度 + 500ms 防抖 + 加热确认后启用；AHT20 触发帧不再计入失败次数
- **倒计时偏慢**：每秒重置 `last_tick=now` 丢弃不足一秒余数 → 改固定步进 +1000ms 结转，断档 >5s 只重锚定
- **确认弹窗只有"是/否"没文字**：标题曾用 ASCII 字体画中文 → 改用 `TFT_DrawStringZh` 居中；文案改纯中文"确认保存/确认删除该预设"
- **预设编辑页长按残留**：名称/温度行长按退出编辑并整页刷新；列表长按切换后重绘清旧 `> ` 标记
- **RGB 开机僵硬亮着**：背光渐亮前灯条熄灭，屏幕渐亮同时直接运行灯效
- **编码器卡顿**：详见上方 1kHz 采样修复；另去除 CS1237 5 点滑动平均/EMA/双帧确认/闲时降频（改 4 帧中值），降低主循环波动

#### 变更
- 编码器「保存并退出 + 退出」合并为单个「退出」（有改动才弹确认保存）
- 烘干时间卡二级菜单底部显示「当前烘干预设为：XXX」（黄色标签）
- 预设名称旁易混淆的「(内置)」标记删除

### 2026-08-24

#### 变更（主控换 CBT6 + 分区扩容）
- **主控切换 STM32F103CBT6（128 KiB）**：C8/CB 同为 medium-density，启动文件/宏/`VECT_TAB_OFFSET=0x3400` 均不变；App 分区由 51 KiB 扩容至 **115 KiB（`0x1CC00`）**，占满 128 KiB（`PLATFORM_FLASH_SIZE=0x20000`）；同步 `project/MDK(V5)/.eide/eide.yml`、根 `.eide/eide.yml`、`scripts/configure_keil_targets.py`（App size + 128 KiB 下载算法 `-FL020000` 校验）
- **WS2812 位冲重写（硬 NOP 时序）**：原 SysTick/DWT 循环延时含函数调用开销，0 码高电平实超 380ns → 全白。改用 `NOP16/NOP44/NOP58` 宏展开（每 NOP 精确 1 周期），0 码高 ~278ns / 1 码高 ~667ns；灯效：空闲彩色渐变平滑流动、烘干进度条已完成部分呼吸灯、PB6 状态灯（空闲灭/烘干红/完成绿 30s）、设置"灯光开关"控制两条灯条
- **烘干控制重构（双 PID）**：AHT20 空气温度为主控维持设定温度（PID 输出 PTC 功率），NTC 加热器温度由第二个 PID 限制在 `ptc_max_temp` 内（NTC 到上限自动压功率）；加热阶段 AHT20 达目标才开始倒计时
- **风扇恒全功率**：删除 `update_fan_for_ptc` 调速算法，烘干/校准全程 100%
- **开机安全机制**：进度条期间读取 AHT20/NTC，NTC 高于冷却温度自动开风扇散热；冷却温度仅检测 NTC

#### 修复
- **NTC 温度计算**：改为 ADC 原始值直接反算（含 8 次采样平均 + EMA 滤波），消除 LUT/VREF 偏差；实测 70°C 假读为接线虚焊，修后室温正常
- **RGB 全白**：0 码高电平总时长（含延时函数开销）超 380ns → 0 判 1 → 全白，硬 NOP 修复
- **电机摆动只反转一次**：改为 `osc_dir` 交替正/反转、幅度恒定（由设置角度控制）
- **TMC2209 电流**：UART 写入 `IHOLD_IRUN`（初始 50→100 风扇、电流 CS=motor_current×18/10），接收加超时防死锁
- **步进脉冲宽度**：3 NOP 提升至 2µs 级（TMC 需 ≥1µs），避免抖动/复位
- **AHT20 阻塞缩短**：500k→200k NOP，消除主循环卡顿

### 2026-08-21

#### 新增（中文界面 + 编码器加速 + 翻页滚动）
- **全界面汉化**：主界面/菜单/温度/PTC/电机/称重/时间/设置/WiFi/关于/OTA/安全警报 全部改为中文显示；新增 16×16 中文点阵字库 `app/zh16.h`（106 字，阳码横向逐行，Unicode 码点索引，约 3.4 KiB 存内部 Flash），`bsp_tft_st7789.c` 新增 `TFT_DrawStringZh()` 支持 UTF-8 中文 + ASCII 混排渲染
- **编码器旋转加速步进**：`bsp_encoder.c` 综合"事件间隔(轮询快时按时间递增)"与"单次轮询相位累计量(轮询被阻塞时按累计量取大)"，上限 10；仅作用于有进度条的选项：屏幕亮度(上限10)、烘干温度/PTC最高温度/PTC冷却温度(上限5)；蜂鸣器音量不加速
- **设置页局部刷新**：`UI_RefreshSettingsValue()` 只重绘当前项的数值文本+进度条，不再全屏重绘（消除编辑时的闪屏与主循环阻塞）
- **翻页 + 滚动条**：菜单/电机设置/设置 超过一页(5项)时支持翻页滚动，右侧滚动条实时反映当前选中项位置；长按退出子页后光标固定落主界面温度卡(第0项)，避免误触湿度卡长按启动烘干

#### 修复
- 冷却温度下限调整到 30（原 40），`SCREEN_PTC_COOLING_EDIT` 旧硬编码 25 同步修正

### 2026-08-19

#### 变更（分区收敛 + OTA 直写）
- **Bootloader 分区缩至 12 KiB（`0x08000000`~`0x08002FFF`）**：体积瘦身（移除全部 `sprintf/printf`，省约 1.2 KiB printf 核心库），原 ~12.6 KiB bin 降至 ~11.4 KiB
- **移除内部 WiFi 配置分区**：WiFi 参数已交给 ESP01S/外部 Flash 存储；分区并入 App，App 起点改为 `0x08003400`、大小 51 KiB（`0xCC00`），`VECT_TAB_OFFSET=0x3400`
- **OTA 改为直写内部 App 分区**：删除外部 Flash 暂存/二次拷贝流程（`verify_staged_image`/`flash_staged_to_app`/`EnterCopyMode`）；握手校验大小合法后擦除 App 分区，数据包边收边直写，收完整包 CRC32 + 向量表校验通过即复位跳转
- **擦除时序**：握手 → 先整区擦除（约 2s）→ 回 ACK；ESP 侧握手等待放宽至 10s，擦除期间 ESP 不发数据包，消除 USART 溢出丢字节导致的首包损坏
- **FSM 容错**：`S_PKT_AA`/`S_PKT_TAIL` 对噪声/残串忽略而非置错；握手后 20s 无完整包超时中止重试，不再死等
- **进度条差分刷新**：每 1% 只画 2~3px 增量（40ms 节流），文字不动，人眼不可见刷新
- **SPI1 频率 36 MHz**（`SPI_BaudRatePrescaler_2`，F103 极限）

#### 修复（按键进菜单 + UI 闪烁）— 8 月 19 日测试正常版
- **App 启用全局中断**：Bootloader 在 `BootloaderV2_JumpToApp()` 跳转前调用 `__disable_irq()`，而 App 的 `SystemInit`/`main` 从不重新开中断 → `PRIMASK` 保持 1 → SysTick 永不触发 → `SystemTime_Millis()` 冻结 → 编码器**单击/长按**（依赖 millis 计时）全部失效（旋转仍可用，因其只轮询 GPIO）。`main.c` 在 `SystemTime_Init()` 后新增 `__enable_irq()`，恢复 SysTick 走时 → 长按 1s 进菜单、单击导航均恢复。
- **上电强制下载窗口**：原代码先 `Encoder_Process()`（内部消费事件）再 `Encoder_GetEvent()`，永远拿不到 `LONG_PRESS`；改为直接轮询 PB5 引脚，强制进 Bootloader 真正可用。
- **主界面时间栏闪烁**：`UI_UpdateMainDynamic()` 原每 50ms 无条件重绘底部时间文字，`TFT_DrawChar` 先填字符格背景再画前景 → 直接写 GRAM 无双缓冲撕裂闪；改为仅当时间字符串变化时重绘（值未变不画），REM 倒计时同理 gating 并在退出烘干时清残留行。
- **菜单旋转无效**：`UI_Update()` 同屏动态分支无 `SCREEN_MENU`，旋转改 `selected_item` 后不重绘；新增 `SCREEN_MENU` 局部分支 + `UI_RefreshMenuSel(old,new)` 只重绘旧/新两行（不整屏刷新），旋转即可移动选中项。
- 顺带：ESP-01S 握手 ACK 等待 3s→10s，覆盖 STM32 擦除 App 分区（约 2s）耗时（详见 `QiMingXing-ESP01S` 仓库）。
- **Keil Bootloader IROM 区校正**：`project/MDK(V5)/Project.uvprojx` 的 Bootloader 目标 IROM 大小由 `0x4800`(18 KiB) 改回 `0x3000`(12 KiB)，与分区契约 `PLATFORM_BOOT_SIZE=0x3000`（及 EIDE `eide.yml`、Flash 分区表）一致，强制 Bootloader 不超过 12 KiB 分区上限（此前 18 KiB 区会静默放行溢出、覆盖升级标志区 `0x08003000` 乃至 App 区 `0x08003400`）。

### 2026-08-16

#### 新增
- Bootloader 完整 OTA 升级链路：ESP01S AP + Web 网页上传固件（两阶段：下载到外部 Flash → 拷贝到 App 分区）
- 上传固件暂存 W25Q128 → 校验向量表 → 刷入内部 Flash App 分区
- App 端 Web 页面"固件升级"按钮：写入升级标志并复位跳转 Bootloader
- `bl_tft.c` 实现 5×7 点阵字体，屏幕可显示 AP 名称/密码/IP/升级进度
- 屏幕适配 1.14 寸 135×240 ST7789 横屏（240×135），UI 完整显示标题/状态/进度/AP 信息

#### 修复
- **OTA 分块上传协议**：改为 1 KiB 分块 POST（替代单次 multipart 大 POST），每块响应 `Content-Length: 0`，让浏览器立即完成 XHR，解决"卡 10%"
- **HTTP 解析**：`\r\n\r\n` 后进入 body 阶段，避免把换行符当固件数据写入
- **+IPD 帧残留**：`GET /done` / `GET / ` 等处理提前返回后，剩余帧字节被 `ESP_WaitResponse` 消耗，导致 `in_ipd`/`ipd_remain` 状态残留、吃掉下一请求数据——新增 `ipd_skip_pending` 机制丢弃剩余帧字节
- **ESP 电源关断**：`EnterCopyMode` 和 `JumpToApp` 中初始化 ESP_EN 引脚（PA12）为推挽输出并置高，同时 PA9（UART TX）输出低电平克服 ESP 内部上拉电阻导致的回灌供电（2.6V 伪供电）
- **拷贝模式 SPI1 未初始化**：`EnterCopyMode` 中先调 `W25Q128_Init`（含 `Spi1Bus_Init`）再 `BL_TFT_Init`，解决屏幕黑屏
- **超时处理**：10s 无数据时若 `fw_received == total_expected` 直接完成传输，不等浏览器 `/done`，避免浏览器响应丢失导致"Upload Error"
- `verify_staged_image` 修正向量表在偏移 0 时被误判"未找到"
- 上传页 JS 精简：移除浏览器进度条、"OK, restarting..." 文字（节省 ~400 B）

#### 变更
- **移除旧 v1 Bootloader**（`boot_main.c`/`boot_updater.c`/`boot_jump.c`/`boot_recovery.c`/`boot_platform_stm32.c` 及对应 `.h`），已由 V2 引导（`bl_main.c`）替代
- 清理 host 测试及 Makefile 中旧 v1 bootloader 引用
- `eide.yml` App 分区修正为 `0x08005000`/`0xB000`（原 `0x08004800`/`0xB800` 错误，会覆盖升级标志区）
- `scripts/configure_keil_targets.py` Bootloader 入口改 `bl_main.c`，分区地址与 `platform_contract.h` 一致
- Keil 工程（`Project.uvprojx`、`Project.uvoptx`）已同步至新分区配置

### 2026-08-18

#### 新增
- **主界面浅色主题**：浅灰背景 + 白卡片 + 灰描边 + 各卡片淡彩底色（温度暖橙 / 湿度冷青 / 重量紫 / PTC 红 / 时间浅绿），替代原深色蓝黑主题
- **圆角描边**：新增 `draw_frame_rounded()`，四角圆弧像素落在 `[r-SEL_FRAME_W, r]` 圆环带内，卡片与菜单选中项圆角均带完整灰色外轮廓
- **数值+单位统一绘制**：抽取 `draw_value_unit()` 公共函数，单位（℃/g）与数字同字号 size2、紧跟数值后，全屏与局部刷新共用，消除单位字号/位置不一致
- **时间栏增强**：烘干时间栏增加运行状态文字（IDLE/HEAT/DRY/COOL/DONE）与 REM 剩余时间显示
- **SPI1 TX DMA 局部刷新**：新增 `Spi1Bus_TransferDma()`（DMA1_Channel3），`TFT_FillRect` 大矩形（≥32 像素）走 DMA、小矩形（字符笔画等）走轮询，降低 SPI 刷新 CPU 占用
- **字体扩展**：`bsp_tft_st7789.c` 新增 `&` 与撇号 `'` 字形

#### 修复
- 开屏 "QiMingXing" X 方向居中（10 字符 × 18px = 180px，原误用 198px）；"LianYan & -e-" 居中（156px，原 168px）
- ℃ 单位圆圈位置修正：° 小圆圈（scale=1）置于 C 左上角，与 C 分离不重叠；C/g 颜色与数字同步（原灰色 `UI_TEXT_DIM`）
- **旋转编码器文字闪烁**：`UI_UpdateMainDynamic()` 原每 50ms 无条件用固定 `CARD_BG_*` 底色重绘值文字、未考虑选中态底色（`UI_CARD_HI`），导致选中卡文字反复擦写闪烁；改为值文字底色随 `selected_item` 动态选择，并加 `last_val[4]` 缓存（值不变不重绘）+ `last_sel` 检测（切换选中项时统一刷底色）
- `draw_card_pulse` 末行缩进错乱修复

#### OTA 协议重构（与 `QiMingXing-ESP01S` 自定义固件配套）
- **HTTP 解析从 STM32 转移到 ESP-01S**：Bootloader 不再解析 `+IPD`/HTTP，改由 ESP 自定义固件托管网页、接收固件并以二进制串口协议转发给 STM32（1 KiB/包，包级 ACK/NAK + 包内 CRC16 Modbus + 整包 CRC32 IEEE802.3），STM32 仅 `BL_ESP01S_Process()` 收二进制并写 W25Q128
- **整包 CRC32 全量校验**：纠正旧版"只校验向量表"漏检大固件静默损坏的问题；ESP 端入口校验——浏览器端算文件 CRC32 随 `?crc=` 上传，ESP 重算不一致即拒绝转发（`FIRMWARE CRC MISMATCH`），挡住 WiFi 上传链路污染
- **ESP 休眠由 STM32 控制**：新增 `BL_ESP01S_CloseWeb()`（发 `AT+OTACLOSE`），在下载收完并写 DOWNLOADED 标志、复位前调用，让 ESP 关闭网页/AP 进入 Modem-Sleep
- **上电兜底强制下载**：`EncoderButton_HeldAtBoot()` 长按编码器（PB5）直接进入下载模式，避免坏固件变砖
- **W25Q128 写保护清除**：拷贝/下载前调用 `W25Q128_ClearProtection()` 清 BP 位，避免页编程被硬件忽略；写前回读 WEL 确认
- `BL_ESP01S_StartAP()` 重命名为 `BL_ESP01S_StartOta()`（发 `AT+OTAAP`）；移除 `BL_ESP01S_GetBodyLen()` 等 HTTP 相关接口
