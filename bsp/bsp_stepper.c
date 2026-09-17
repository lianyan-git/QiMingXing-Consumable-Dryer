#ifndef BOOTLOADER_BUILD
#include "bsp_stepper.h"
#include "pin_config.h"
#include "board.h"                 /* Watchdog_Kick: TMC 后台事务前后喂狗 */
#include "stm32f10x.h"
#include "system_config.h"
#include "system_time.h"

/* TMC2208/2209 单线 UART 位时序（9600bps，SysTick 实测校准） */

static volatile uint8_t stepper_enabled = 0;
static volatile uint8_t motor_running = 0;
static volatile int32_t motor_remaining_steps = 0;
static volatile uint8_t motor_dir = 1;
static volatile uint32_t step_period_us = 1000;
static volatile int32_t target_steps = 0;
static volatile int8_t  osc_dir = 1;

/* 9600bps bit-bang 硬件计时：直接读 SysTick->VAL（每 µs 72 ticks，计数器在 TICKINT
 * 关闭时仍自由运行）。旧版用"空循环+校准"，但校准(中断开)与事务(中断关)环境不同，
 * 采样点逐位漂移→跨字节累积→F2(有边沿但解错)。VAL 计时与中断/O0 无关。
 * SysTick 重载=1ms(71999)：104µs 等待不会跨越重载边界，无回绕处理需求。 */
static uint32_t tmc_sys_freq = 72000000UL;

static void tmc_wait_us(uint32_t us)
{
    uint32_t ticks = us * (tmc_sys_freq / 1000000UL);
    uint32_t t0 = SysTick->VAL;
    while ((uint32_t)(t0 - SysTick->VAL) < ticks) { /* spin */ }
}
static void tmc_delay(void) { tmc_wait_us(104); }      /* 1 bit @9600 */

static uint8_t tmc_initialized = 0;
static uint8_t tmc_last_err = 0;    /* 读事务失败分类: 0 ok/无 1 零边沿(线路死) 2 有边沿但坏(时序/波形) */
static uint8_t tmc_dbg_raw[8] = {0};  /* 最近一次读事务实际收到的回复字节(调试: 徽章显示前两字节) */
static uint8_t tmc_dbg_raw_cnt = 0;

uint8_t Stepper_TmcErr(void) { return tmc_initialized ? 0U : tmc_last_err; }
/* 诊断码 = (已收字节数 << 8) | 首字节值。例: 0x0105 = 收到1字节且首字节0x05(芯片在回) */
uint16_t Stepper_TmcRaw16(void)
{
    return ((uint16_t)tmc_dbg_raw_cnt << 8) | tmc_dbg_raw[0];
}
/* 线路空闲电平自检: 1=PB15 读高(上拉正常/模块在回), 0=读低(线被拉死/模块未上电) */
uint8_t Stepper_TmcLineIdle(void)
{
    GPIO_InitTypeDef g;
    uint8_t v;
    g.GPIO_Pin = PIN_STEP_UART_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(PIN_STEP_UART_PORT, &g);
    v = GPIO_ReadInputDataBit(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN) ? 1U : 0U;
    GPIO_SetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);
    return v;
}

/* 已下发到 TMC 的配置指纹: 型号/电流/静音任一变化 → 必须重新完整 tmc_configure,
 * 否则"UI 改了 0.2A→0.4A 但芯片仍跑旧 IRUN""换 2208/2209/A4988 后跳过重配"。
 * 0xFF=从未应用。tmc_initialized 语义收窄为"当前指纹下通讯已确认"。 */
static uint8_t tmc_applied_drv  = 0xFF;
static uint8_t tmc_applied_curr = 0xFF;
static uint8_t tmc_applied_sil  = 0xFF;

/* TMC2208/2209 UART 协议常量（官方单线 UART 帧格式） */
#define TMC_SLAVE_ADDR   0x00U   /* 出厂假设地址 0(MS1/MS2 接地); 实际以 tmc_slave_addr 为准 */
static uint8_t tmc_slave_addr = TMC_SLAVE_ADDR;   /* 通讯不通时自动在 0-3 中探测(MS1/MS2 拨码未知) */
#define TMC_REG_GCONF    0x00U   /* bit2 en_spreadCycle: 0=StealthChop 静音, 1=SpreadCycle */
#define TMC_REG_GSTAT    0x01U   /* 写 1 清除复位/错误标志：无副作用，可当"探针写" */
#define TMC_REG_IFCNT    0x02U   /* 成功收到"写"报文的 4bit 循环计数；读报文不改变它 */
#define TMC_REG_IHOLD    0x10U   /* IHOLD_IRUN */
#define TMC_REG_CHOPCONF 0x6CU
#define TMC_REG_DRVSTAT  0x6FU

/* work/rest state machine */
static volatile uint8_t  motor_osc_mode = 0;   /* 1=oscillate, 0=continuous */
static volatile uint8_t  last_dir = 1;         /* continuous rotation dir backup */
static volatile uint32_t work_limit = 0;       /* work count limit, 0=forever */
static volatile uint32_t rest_ms = 0;          /* rest duration ms */
static volatile uint32_t work_done = 0;        /* completed work units */
static volatile uint32_t steps_in_unit = 0;    /* steps in current work unit */
static volatile uint32_t work_unit_steps = 200;/* steps per unit (rotation=200, oscillate=2x target) */
static volatile uint32_t rest_until_ms = 0;    /* rest end timestamp */
static volatile uint8_t  resting = 0;          /* 1=resting */

/* CRC8 forward decl: used by write/read before its definition */
static uint8_t tmc_crc8(const uint8_t *d, uint8_t n);
static void tmc_bus_lock(void);
static void tmc_bus_unlock(void);

static void tmc_uart_send_byte(uint8_t byte)
{
    uint8_t i;
    GPIO_ResetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);  tmc_delay();  /* start */
    for (i = 0; i < 8; i++) {
        if (byte & 0x01U) GPIO_SetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);
        else               GPIO_ResetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);
        byte >>= 1;  tmc_delay();
    }
    GPIO_SetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);  tmc_delay();  /* stop */
}

/* 收 1 字节：1=收到，0=等待起始位超时。返回值与数据分离（数据本身可能合法为 0xFF，
 * 不能拿 0xFF 当"无应答"标记）。 */
static uint8_t tmc_uart_recv_byte(uint8_t *out)
{
    uint8_t i, byte = 0;
    volatile uint32_t timeout = 0;
    while (GPIO_ReadInputDataBit(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN)) {
        if (++timeout > 200000U) return 0;              /* timeout no reply */
    }
    tmc_wait_us(78);                                       /* 采样点取位宽 3/4(78µs): 抗弱上拉慢沿 */
    for (i = 0; i < 8; i++) {
        tmc_delay();
        if (GPIO_ReadInputDataBit(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN)) byte |= (1U << i);
    }
    tmc_delay();
    *out = byte;
    return 1;
}

static void tmc_uart_tx_mode(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = PIN_STEP_UART_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;    /* 推挽输出: 高电平主动驱动, 不依赖外部上拉;
                                        * 半双工(仅 TMC 回复时才切输入), 无冲突风险 */
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_STEP_UART_PORT, &g);
    GPIO_SetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);
}

static void tmc_uart_rx_mode(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = PIN_STEP_UART_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(PIN_STEP_UART_PORT, &g);
}

/* 写帧（官方格式，8 字节）：[0x05][slave][reg|0x80][d3][d2][d1][d0][crc8(前7字节)]
 * TMC 对写不回复，本函数只表示"帧已发出"；是否被芯片接收用 tmc_write_reg_checked（IFCNT 验证）。 */
static uint8_t tmc_write_reg(uint8_t reg, uint32_t data)
{
    uint8_t buf[8], i;
    buf[0] = 0x05U;  buf[1] = tmc_slave_addr;  buf[2] = (uint8_t)(reg | 0x80U);
    buf[3] = (uint8_t)(data >> 24);
    buf[4] = (uint8_t)(data >> 16);
    buf[5] = (uint8_t)(data >> 8);
    buf[6] = (uint8_t)(data);
    buf[7] = tmc_crc8(buf, 7);

    tmc_bus_lock();
    tmc_uart_tx_mode();
    for (i = 0; i < 8U; i++) tmc_uart_send_byte(buf[i]);
    tmc_uart_rx_mode();
    tmc_bus_unlock();
    return 0;
}

/* TMC UART CRC8：官方数据手册算法（多项式 0x07，初值 0，数据按字节 LSB→MSB 逐位、
 * 校验和按 MSB 位移：if ((crc>>7)^(byte&1)) crc=(crc<<1)^0x07 else crc<<=1）。
 * 注意：这不是反射式 CRC-8/MAXIM(0x8C)——此前用 0x8C 会算出与芯片不匹配的 CRC，
 * 帧格式全对也永远被芯片静默拒收。 */
static uint8_t tmc_crc8(const uint8_t *d, uint8_t n)
{
    uint8_t crc = 0;
    uint8_t i, j;
    for (i = 0; i < n; i++) {
        uint8_t cur = d[i];
        for (j = 0; j < 8; j++) {
            if (((crc >> 7) ^ (cur & 0x01U)) != 0U) crc = (uint8_t)((crc << 1) ^ 0x07U);
            else                                    crc = (uint8_t)(crc << 1);
            cur >>= 1;
        }
    }
    return crc;
}

/* bit-bang 事务锁: 9600bps 位时间仅 104us, SysTick(1ms)/TIM2(步进 50us) 的 ISR
 * 延迟会把采样点挤出位眼 → CRC 随机错。SysTick 是内核系统异常(SysTick_IRQn=-1),
 * 不能用 NVIC_DisableIRQ(越界 UB, 实际遮不住任何东西)——改寄存器级关 TICKINT:
 * 计数器照常走, 只停中断, 解锁后补挂起的那次溢出计数(整事务 ≤数百ms, 误差可接受)。
 * USART1 不遮(保持 ESP RX; 上传会话与 TMC 事务都只在电机停止时进行, 互斥)。 */
static void tmc_bus_lock(void)
{
    NVIC_DisableIRQ(TIM2_IRQn);
    SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
}
static void tmc_bus_unlock(void)
{
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
    NVIC_EnableIRQ(TIM2_IRQn);
}

/* 读请求（官方格式，4 字节）：[0x05][slave][reg][crc8(前3字节)]
 * 回复（8 字节）：            [0x05][0xFF][reg][d3][d2][d1][d0][crc8(前7字节)]，
 * 回复第 3 字节 bit7=1 表示芯片报错。
 * SENDDELAY 默认 8 bit：请求发完先等约 0.83ms（9600bps）再接收，避免把线上毛刺
 * 当起始位。整段事务由 tmc_bus_lock 保护（见上）。失败/超时返回 0xFFFFFFFF。 */
static uint32_t tmc_read_reg(uint8_t reg)
{
    uint8_t req[4], rsp[8], i, b;
    req[0] = 0x05U;  req[1] = tmc_slave_addr;  req[2] = reg;
    req[3] = tmc_crc8(req, 3);
    tmc_bus_lock();
    tmc_uart_tx_mode();
    for (i = 0; i < 4U; i++) tmc_uart_send_byte(req[i]);
    /* 不预设 SENDDELAY 等待长度: 发完立即切 RX, recv_byte 自己等起始沿。
     * 旧版固定等 8 bit 再收, 若芯片实际回复更快, 起始位已过, 我们抓到的是
     * 数据位里的 0 沿 → 整帧错位却仍收满 8 字节 = F2 08xx 的典型特征。
     * SENDDELAY 期间线上为高, 起始沿等待不会误触发; 超时窗口覆盖最长 SENDDELAY。 */
    tmc_uart_rx_mode();
    for (i = 0; i < 8U; i++) {
            if (!tmc_uart_recv_byte(&b)) {
                tmc_bus_unlock();
                tmc_dbg_raw_cnt = i;               /* 已收字节数(供徽章诊断) */
                /* 错误源分类(UI FAIL 徽章显示): 1=第0字节就没起沿(线路死/缺上拉);
                 * 2=半途超时(有边沿但时序/波形坏, 芯片在回但读不齐)。 */
                tmc_last_err = (i == 0U) ? 1U : 2U;
                return 0xFFFFFFFFU;
            }
        tmc_dbg_raw[i] = b;
        rsp[i] = b;
    }
    tmc_dbg_raw_cnt = 8U;
    tmc_bus_unlock();
    if (rsp[0] != 0x05U || rsp[1] != 0xFFU) { tmc_last_err = 2U; return 0xFFFFFFFFU; }
    if ((rsp[2] & 0x80U) || (rsp[2] & 0x7FU) != reg) { tmc_last_err = 2U; return 0xFFFFFFFFU; }
    if (tmc_crc8(rsp, 7) != rsp[7]) { tmc_last_err = 2U; return 0xFFFFFFFFU; }
    tmc_last_err = 0U;
    return ((uint32_t)rsp[3] << 24) | ((uint32_t)rsp[4] << 16) |
           ((uint32_t)rsp[5] << 8) | (uint32_t)rsp[6];
}

/* IFCNT 验证写：芯片只在收到校验通过的写报文后把 IFCNT +1（读不影响），
 * 写前后各读一次即可确认芯片真实收到（读帧本身已含 SYNC/地址/寄存器/CRC 四重校验，
 * 共线干扰无法伪造）。返回 0=已被芯片接收，1=无应答或计数未递增。 */
static uint8_t tmc_write_reg_checked(uint8_t reg, uint32_t data)
{
    uint32_t pre = tmc_read_reg(TMC_REG_IFCNT);
    if (pre == 0xFFFFFFFFU) return 1;
    if (tmc_write_reg(reg, data) != 0U) return 1;
    uint32_t post = tmc_read_reg(TMC_REG_IFCNT);
    if (post == 0xFFFFFFFFU) return 1;
    return ((post & 0xFU) == ((pre + 1U) & 0xFU)) ? 0U : 1U;
}

/* TMC 电流档位: motor_current 单位 ~0.1A(2=0.2A..6=0.6A), cs=current*18/10 是本板经验映射
 * (经验锚点 VREF≈2.5V/0.11V 步进)。注意: TMC2209 实际 RMS 电流 = f(IRUN, VFS, Rsense,
 * vsense 内/外部配置) 非线性, "档位"≠实测安培 —— UI 数字只能当相对强度。
 * 校验 UART 好坏一律看 IFCNT/CRC 应答, 不得用电流实测值推断。 */
static uint8_t tmc_set_current(uint8_t motor_current)
{
    uint8_t cs = (uint8_t)((uint16_t)motor_current * 18U / 10U);
    uint32_t val;
    if (cs < 1) cs = 1;  if (cs > 31) cs = 31;
    /* TMC2209 IHOLD_IRUN(0x10) 位域：IHOLD=bits4:0，IRUN=bits12:8，IHOLDDELAY=bits19:16。
     * 设定：IHOLD=cs/2（保持半流降发热）、IRUN=cs、IHOLDDELAY=5。 */
    val = (uint32_t)(cs / 2U) | ((uint32_t)cs << 8) | (5UL << 16);
    return tmc_write_reg_checked(TMC_REG_IHOLD, val);
}

/* 地址自动探测: MS1/MS2 拨码与假设不符时, 帧/CRC 全对芯片也充耳不闻。
 * 仅在"当前地址彻底无应答"时扫 0..3(一次读 ~14ms, 4 地址最坏 60ms, 停机时刻做)。 */
static uint8_t tmc_detect_addr(void)
{
    uint8_t a, keep = tmc_slave_addr;
    for (a = 0U; a < 4U; a++) {
        tmc_slave_addr = a;
        if (tmc_read_reg(TMC_REG_IFCNT) != 0xFFFFFFFFU) return 0U;
    }
    tmc_slave_addr = keep;
    return 1U;                        /* 4 个地址全无应答 */
}

/* 一次性 TMC 配置（顺序关键）：
 *   读 GCONF → 置 pdn_disable(bit6)=1（PDN_UART 引脚作为 UART 使用，官方要求），
 *   按 silent 设 en_spreadCycle(bit2)（0=StealthChop 静音）→ IFCNT 验证写回 →
 *   IHOLD/IRUN 电流（内置 IFCNT 验证）。任一步失败返回 1，绝不进入"半配置"状态。 */
static uint8_t tmc_configure(uint8_t silent, uint8_t motor_current)
{
    uint32_t v = tmc_read_reg(TMC_REG_GCONF);
    if (v == 0xFFFFFFFFU) {                   /* 当前地址不应答: 先试地址探测 */
        if (tmc_detect_addr() != 0U) return 1;
        v = tmc_read_reg(TMC_REG_GCONF);
        if (v == 0xFFFFFFFFU) return 1;
    }
    v |= (1UL << 6);                                /* pdn_disable=1 */
    if (silent) v &= ~(1UL << 2);                   /* StealthChop */
    else        v |=  (1UL << 2);                   /* SpreadCycle */
    if (tmc_write_reg_checked(TMC_REG_GCONF, v) != 0U) return 1;
    if (tmc_set_current(motor_current) != 0U) return 1;
    return 0;
}

/* ?? ?????? TIM2?1�s ???????? step_period_us ?????
 *    ????????? ? ?????????????????? ?? */

static void stepper_dir_set(uint8_t fwd)
{
    motor_dir = fwd;
    if (fwd) GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_DIR_PIN);
    else     GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_DIR_PIN);
}

static void stepper_timer_reload(void)
{    uint32_t period = step_period_us;
    if (period < 50) period = 50;      /* ?? 20k steps/s??????? */
    TIM_SetAutoreload(TIM2, period - 1U);
    TIM_SetCounter(TIM2, 0);
}

static void stepper_start(int32_t steps)
{
    if (steps == 0) { TIM_Cmd(TIM2, DISABLE); motor_running = 0; return; }
    motor_remaining_steps = steps;
    motor_running = 1;
    stepper_timer_reload();
    TIM_Cmd(TIM2, ENABLE);
}

void Stepper_Init(void)
{
    GPIO_InitTypeDef g;
    TIM_TimeBaseInitTypeDef tb;
    NVIC_InitTypeDef n;

    tmc_sys_freq = SystemCoreClock;   /* 位定时按实际系统时钟(72MHz), 防时钟配置变动后漂移 */

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    g.GPIO_Pin = PIN_STEP_EN_PIN | PIN_STEP_STEP_PIN | PIN_STEP_DIR_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_STEP_EN_PORT, &g);

    GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN);   /* EN high = driver off */
    GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_STEP_PIN);
    GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_DIR_PIN);

    /* TIM2: 1us tick, update IRQ drives STEP */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    TIM_DeInit(TIM2);
    tb.TIM_Prescaler = (uint16_t)(SystemCoreClock / 1000000UL - 1UL);
    tb.TIM_Period = (uint16_t)(step_period_us - 1U);
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &tb);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    n.NVIC_IRQChannel = TIM2_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 2;
    n.NVIC_IRQChannelSubPriority = 0;
    n.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&n);

    tmc_initialized = 0;
}

void Stepper_Update(void);
void Stepper_SyncProfile(void);
void Stepper_SetSilent(uint8_t en);

/* 已下发到 TMC 的配置指纹声明见文件头; Stepper_Enable 里做漂移检测 */

static uint8_t tmc_driver_is_tmc(void)
{
    return (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
            g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209) ? 1U : 0U;
}

/* 使能路径绝不碰 TMC UART(v10 复盘: StartDrying→Enable→configure→地址扫描→
 * IFCNT 验证全部串在按键上下文, UART 不通时每读 200000 轮询超时×多次 > IWDG 4s
 * → 看门狗复位 → 电机"不转")。EN/STEP/DIR 是主链路, TMC 是附加配置:
 * 配置移到 Stepper_Update 停机空闲后台窗口, 限流+丢狗保护, 失败只标记通讯故障。 */
static uint32_t s_tmc_next_try_ms = 0;

void Stepper_Enable(uint8_t enable)
{
    stepper_enabled = enable;
    if (enable) {
        GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN);
        s_tmc_next_try_ms = 0;   /* 强制空闲窗口尽快配置一次(仍在停机上下文) */
    } else {
        GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN);
        TIM_Cmd(TIM2, DISABLE);
        motor_running = 0;
        motor_remaining_steps = 0;
        resting = 0;
        work_done = 0;
        steps_in_unit = 0;
    }
}

/* 停机空闲窗口的后台 TMC 服务: 漂移检测 + 完整配置(静音位随 configure 一并下发)。
 * 只在 stepper_enabled 且未步进(含休息期——正是要配的好时机)时进入;
 * 每 2s 至多一次事务且事务前后喂狗; 通讯坏 → tmc_initialized=0(页面 TMC-COM FAIL),
 * 电机照常运转。 */
static void tmc_background_service(void)
{
    uint8_t is_tmc = tmc_driver_is_tmc();
    uint32_t now;
    if (!stepper_enabled || motor_running) return;
    if (tmc_initialized &&
        (is_tmc == 0U ||
         tmc_applied_drv != g_sys.params.motor_driver ||
         tmc_applied_curr != g_sys.params.motor_current ||
         tmc_applied_sil  != g_sys.params.motor_stealthchop)) {
        tmc_initialized = 0;           /* 型号/电流/静音漂移: 整体重配 */
    }
    if (!is_tmc || tmc_initialized) return;
    now = SystemTime_Millis();
    if ((uint32_t)(now - s_tmc_next_try_ms) >= 2000U) {
        s_tmc_next_try_ms = now;
        Watchdog_Kick();
        if (tmc_configure(g_sys.params.motor_stealthchop, g_sys.params.motor_current) == 0U) {
            tmc_initialized   = 1;
            tmc_applied_drv   = g_sys.params.motor_driver;
            tmc_applied_curr  = g_sys.params.motor_current;
            tmc_applied_sil   = g_sys.params.motor_stealthchop;
        }
        Watchdog_Kick();
    }
}

void Stepper_SetSpeed(uint16_t steps_per_sec)
{
    if (steps_per_sec == 0) steps_per_sec = 1;
    uint32_t period = 1000000UL / steps_per_sec;
    if (period < 50) period = 50;
    step_period_us = period;
    if (motor_running) stepper_timer_reload();
}

/* 一整圈 360° 对应步数：连续旋转专用常数（与摆动标定无关，见 bsp_stepper.h） */
static uint32_t steps_per_rotation(void)
{
    return TRAY_STEPS_PER_360;
}

/* 从最新系统参数重建运动曲线：休息结束进入下一个转动周期时调用，
 * 使转速/角度/次数/休息时长等修改在"下一次转动"生效（烘干中无需停止重启） */
void Stepper_SyncProfile(void)
{
    work_limit = g_sys.params.motor_work_count;
    rest_ms = (uint32_t)g_sys.params.motor_rest_sec * 1000U;
    Stepper_SetSpeed((uint16_t)g_sys.params.motor_speed * 200U);

    motor_osc_mode = g_sys.params.motor_oscillate;
    if (motor_osc_mode) {
        uint32_t t = (uint32_t)g_sys.params.motor_oscillate_angle * SWING_BASE_STEPS_PER_DEG
                   * g_sys.params.motor_swing_cal / 10000U;
        if (t < 1U) t = 1U;
        target_steps = (int32_t)t;
        work_unit_steps = t * 2U;          /* 一次往返 = 1 个工作单位 */
    } else {
        target_steps = 0;
        work_unit_steps = steps_per_rotation();  /* 一次整圈 360° = 1 个"次数" */
    }
}

/* 静音切换走 GCONF(0x00) bit2=en_spreadCycle（0=StealthChop 静音）。
 * 注意：CHOPCONF(0x6C) bit30 是 DISS2G（关短路保护），与静音无关——旧代码写错位置，
 * UART 一旦修通会关掉短路保护而不是切静音，现已改到这里（读-改-写保留其余配置位）。 */
/* 静音切换走 GCONF(0x00) bit2=en_spreadCycle（0=StealthChop 静音）。
 * 注意：CHOPCONF(0x6C) bit30 是 DISS2G（关短路保护），与静音无关——旧代码写错位置，
 * UART 一旦修通会关掉短路保护而不是切静音，现已改到这里（读-改-写保留其余配置位）。
 * item: 未确认通讯(tmc_initialized=0)时不盲写, 避免 UI 显示"静音开"而芯片没变;
 *       步进中直接跳过(参数已存, 下次使能按指纹重配生效), 严禁运行中做 UART 事务丢步。
 * 写入经 IFCNT 验证成功才更新静音指纹; 任一环节失败 → tmc_initialized=0, 下次使能重建。 */
void Stepper_SetSilent(uint8_t en)
{
    uint32_t v;
    if (!tmc_driver_is_tmc()) return;
    if (!tmc_initialized) return;
    if (motor_running) return;
    v = tmc_read_reg(TMC_REG_GCONF);
    if (v == 0xFFFFFFFFU) { tmc_initialized = 0; return; }   /* 读不到=链路失效, 下轮重配 */
    if (en) v &= ~(1UL << 2);              /* en_spreadCycle=0 → StealthChop */
    else    v |=  (1UL << 2);              /* en_spreadCycle=1 → SpreadCycle */
    if (tmc_write_reg_checked(TMC_REG_GCONF, v) == 0U) {
        tmc_applied_sil = en;               /* 芯片确认收到才算真实生效 */
    } else {
        tmc_initialized = 0;
    }
}

void Stepper_Move(int32_t steps)
{
    if (!stepper_enabled) return;
    target_steps = 0;                  /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷 */
    motor_osc_mode = 0;
    work_unit_steps = steps_per_rotation();   /* 1 ??工作单?= 一整?(360° 标定步?)*/
    if (steps > 0) { last_dir = 1; stepper_dir_set(1); }
    else if (steps < 0) { last_dir = 0; stepper_dir_set(0); }
    stepper_start(steps);
}

void Stepper_SetOscillate(int32_t steps)
{
    target_steps = steps;
    osc_dir = 1;                       /* ??? */
    motor_osc_mode = 1;
    if (steps > 0) work_unit_steps = (uint32_t)steps * 2U;  /* 1 ????? = ???? */
    if (stepper_enabled && target_steps != 0 && !motor_running) {
        stepper_dir_set(1);
        stepper_start(target_steps);
    }
}

/* sync work/rest params from settings */
void Stepper_Update(void)
{
    work_limit = g_sys.params.motor_work_count;
    rest_ms = (uint32_t)g_sys.params.motor_rest_sec * 1000U;

    tmc_background_service();   /* TMC 配置全部走这里(停机空闲+限流), 使能/启动路径零 UART */

    /* ???????????? */
    if (resting) {
        uint32_t now = SystemTime_Millis();
        if ((int32_t)(now - rest_until_ms) >= 0) {
            resting = 0;
            work_done = 0;
            steps_in_unit = 0;
            if (stepper_enabled) {
                GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN); /* 重启前重新使能(首步至少在 ~ms 后, 满足 EN setup) */
                Stepper_SyncProfile();   /* 速度/角度/次数等修改在下一个转动周期生效 */
                if (motor_osc_mode && target_steps > 0) {
                    osc_dir = 1;
                    stepper_dir_set(1);
                    motor_remaining_steps = target_steps;
                    motor_running = 1;
                    stepper_timer_reload();
                    TIM_Cmd(TIM2, ENABLE);
                } else if (!motor_osc_mode) {
                    Stepper_Move(last_dir ? 100000 : -100000);
                }
            }
        }
        return;
    }

    /* ?????TMC ??? 1s ? DRV_STATUS(0x6F)?OT(bit25)/OTPW(bit26)
     * over-temp: rest duration = param, fallback 30s */
    /* UART 未验证通（tmc_initialized=0）时不做读轮询；且只在电机不步进时查询
     * (一次读≈14ms 关 TIM2, 步进中必丢步)。休息/暂停窗口照常查,
     * 步进进行中的过热兜底由 NTC 环 + control_update 的 160℃ 绝对闸负责。 */
    if (stepper_enabled && !motor_running && tmc_initialized &&
        (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
         g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209)) {
        static uint32_t last_check = 0;
        uint32_t now = SystemTime_Millis();
        if ((uint32_t)(now - last_check) >= 1000U) {
            last_check = now;
            uint32_t st = tmc_read_reg(TMC_REG_DRVSTAT);
            if (st != 0xFFFFFFFFU && (st & ((1UL << 25) | (1UL << 26)))) {
                TIM_Cmd(TIM2, DISABLE);
                motor_running = 0;
                resting = 1;
                rest_until_ms = SystemTime_Millis() + ((rest_ms > 0) ? rest_ms : 30000U);
                GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN);   /* 过温休息同样断使能 */
            }
        }
    }
}

void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == RESET) return;
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

    if (!motor_running) { TIM_Cmd(TIM2, DISABLE); return; }

    /* ???? STEP ???????? ~1.4�s?TMC ?? ?1�s? */
    GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_STEP_PIN);
    { volatile uint32_t d; for (d = 0; d < 100; d++); }
    GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_STEP_PIN);

    if (motor_remaining_steps > 0)     motor_remaining_steps--;
    else if (motor_remaining_steps < 0) motor_remaining_steps++;

    /* work counting: per work unit (1 rotation / 1 oscillate cycle)
     * reaching limit (>0) -> stop and rest; resumed by Stepper_Update */
    if (work_limit != 0 && work_unit_steps > 0) {
        if (++steps_in_unit >= work_unit_steps) {
            steps_in_unit = 0;
            if (++work_done >= work_limit) {
                TIM_Cmd(TIM2, DISABLE);
                motor_running = 0;
                resting = 1;
                rest_until_ms = SystemTime_Millis() + rest_ms;
                GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN);   /* 休息期断使能: 线圈失电散热, 托盘可自由转动 */
            }
        }
    }

    if (motor_remaining_steps == 0 && !resting) {
        TIM_Cmd(TIM2, DISABLE);
        motor_running = 0;
        if (target_steps > 0) {                 /* ????????? */
            osc_dir = -osc_dir;
            stepper_dir_set(osc_dir > 0);
            motor_remaining_steps = (osc_dir > 0) ? target_steps : -target_steps;
            motor_running = 1;
            stepper_timer_reload();
            TIM_Cmd(TIM2, ENABLE);
        }
    }
}

uint8_t Stepper_IsRunning(void)
{
    return motor_running;
}

/* UI 轻量在线查询: 读 IFCNT 一次, 帧含 SYNC/从机地址/寄存器/CRC 四重校验, 通过即真实在线。
 * 电机步进中禁止事务(~14ms 关 TIM2 丢步): 上次成功结果按 10s 内视为有效缓存。 */
uint8_t Stepper_TmcComOk(void)
{
    static uint8_t s_ok_last = 0;
    static uint32_t s_ok_ms = 0;
    uint32_t now = SystemTime_Millis();
    uint32_t v;
    if (motor_running) {
        return ((uint32_t)(now - s_ok_ms) < 10000U) ? s_ok_last : 0U;
    }
    v = tmc_read_reg(TMC_REG_IFCNT);
    s_ok_last = (v != 0xFFFFFFFFU) ? 1U : 0U;
    s_ok_ms = now;
    return s_ok_last;
}

/* TMC 通讯详查：IFCNT 只随"成功写入的报文"递增（读请求不改变计数），所以两次读之间
 * 夹一次无害写（GSTAT=0：清错误标志，无副作用）。真芯片第二次读必然 = 第一次 +1；
 * 共线拉高/干扰无法伪造出地址+寄存器+CRC 全对的回复，也无法模拟递增。
 * returns 1=ok 0=fail/timeout */
uint8_t Stepper_TmcProbe(uint32_t *ifcnt)
{
    uint32_t a;
    uint32_t b;
    if (motor_running) { if (ifcnt) *ifcnt = 0U; return 0U; }  /* 步进中禁做: 一次 Probe≈40ms 关 TIM2, 必丢步 */
    a = tmc_read_reg(TMC_REG_IFCNT);
    if (a == 0xFFFFFFFFU) { if (ifcnt) *ifcnt = 0; return 0; }
    tmc_write_reg(TMC_REG_GSTAT, 0U);
    b = tmc_read_reg(TMC_REG_IFCNT);
    if (ifcnt) *ifcnt = b;
    if (b == 0xFFFFFFFFU) return 0;
    return ((b & 0xFU) == ((a + 1U) & 0xFU)) ? 1U : 0U;
}
#endif /* BOOTLOADER_BUILD */