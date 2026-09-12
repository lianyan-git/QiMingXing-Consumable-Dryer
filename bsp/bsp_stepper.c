#ifndef BOOTLOADER_BUILD
#include "bsp_stepper.h"
#include "pin_config.h"
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
static uint32_t tmc_bit_delay = 900;
static uint32_t tmc_half_bit_delay = 450;

static uint8_t tmc_initialized = 0;

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

static void tmc_timing_calibrate(void)
{
    volatile uint32_t d;
    uint32_t t0m, t0v, t1m, t1v, reload, ticks, us;
    t0m = SystemTime_Millis();
    t0v = SysTick->VAL;
    for (d = 0; d < 20000U; d++);
    t1m = SystemTime_Millis();
    t1v = SysTick->VAL;
    reload = SysTick->LOAD;
    ticks = (t1m - t0m) * (reload + 1U);
    if (t0v >= t1v) ticks += (t0v - t1v);
    else ticks += (t0v + (reload + 1U) - t1v);
    us = (ticks * 1000UL) / (reload + 1U);
    if (us == 0U) us = 1U;
    tmc_bit_delay = (20000UL * 104UL) / us;
    tmc_half_bit_delay = tmc_bit_delay / 2U;
}

/* CRC8 forward decl: used by write/read before its definition */
static uint8_t tmc_crc8(const uint8_t *d, uint8_t n);

/* TMC single-wire UART byte helpers (9600bps) */
static void tmc_delay(void) { volatile uint32_t d; for (d = 0; d < tmc_bit_delay; d++); }
static void tmc_half_delay(void) { volatile uint32_t d; for (d = 0; d < tmc_half_bit_delay; d++); }

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

static uint8_t tmc_uart_recv_byte(void)
{
    uint8_t i, byte = 0;
    volatile uint32_t timeout = 0;
    while (GPIO_ReadInputDataBit(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN)) {
        if (++timeout > 200000U) return 0xFF;          /* timeout no reply */
    }
    tmc_half_delay();                                     /* sample after half-bit */
    for (i = 0; i < 8; i++) {
        tmc_delay();
        if (GPIO_ReadInputDataBit(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN)) byte |= (1U << i);
    }
    tmc_delay();
    return byte;
}

static void tmc_uart_tx_mode(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = PIN_STEP_UART_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
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

static uint8_t tmc_write_reg(uint8_t reg, uint32_t data)
{
    uint8_t buf[7], i;
    buf[0] = 0x00;  buf[1] = reg;
    buf[2] = (uint8_t)(data >> 24);
    buf[3] = (uint8_t)(data >> 16);
    buf[4] = (uint8_t)(data >> 8);
    buf[5] = (uint8_t)(data);
    buf[6] = tmc_crc8(buf, 6);

    tmc_uart_tx_mode();
    for (i = 0; i < 7; i++) tmc_uart_send_byte(buf[i]);
    tmc_uart_rx_mode();
    return 0;
}

/* TMC UART CRC8 ??? 0x8C?LSB ??? */
static uint8_t tmc_crc8(const uint8_t *d, uint8_t n)
{
    uint8_t crc = 0;
    uint8_t i, j;
    for (i = 0; i < n; i++) {
        uint8_t b = d[i];
        for (j = 0; j < 8; j++) {
            crc = (crc ^ b) & 1U ? (uint8_t)((crc >> 1) ^ 0x8C) : (uint8_t)(crc >> 1);
            b >>= 1;
        }
    }
    return crc;
}

/* Read TMC register: request [0x05][reg][0][0][0][0][crc], reply [0x05][reg][d3..d0][crc].
 * Returns 32-bit value on success, 0xFFFFFFFF on failure/timeout. */
static uint32_t tmc_read_reg(uint8_t reg)
{
    uint8_t req[7], rsp[7];
    uint8_t i;
    req[0] = 0x05;  req[1] = reg;
    req[2] = req[3] = req[4] = req[5] = 0;
    req[6] = tmc_crc8(req, 6);
    tmc_uart_tx_mode();
    for (i = 0; i < 7; i++) tmc_uart_send_byte(req[i]);
    tmc_uart_rx_mode();
    for (i = 0; i < 7; i++) {
        rsp[i] = tmc_uart_recv_byte();
    if (rsp[i] == 0xFF) return 0xFFFFFFFFU;   /* no reply/timeout */
    }
    if (rsp[0] != 0x05 || rsp[1] != reg) return 0xFFFFFFFFU;
    if (tmc_crc8(rsp, 6) != rsp[6]) return 0xFFFFFFFFU;
    return ((uint32_t)rsp[2] << 24) | ((uint32_t)rsp[3] << 16) |
           ((uint32_t)rsp[4] << 8) | (uint32_t)rsp[5];
}

/* TMC current: motor_current x0.1A (2=0.2A...6=0.6A)
 * CS = motor_current*18/10 �� ? 0.11 ?? + VREF~2.5V ?? */
static uint8_t tmc_set_current(uint8_t motor_current)
{
    uint8_t cs = (uint8_t)((uint16_t)motor_current * 18U / 10U);
    if (cs < 1) cs = 1;  if (cs > 31) cs = 31;
    uint32_t val = (uint32_t)cs | ((uint32_t)cs << 8) | ((uint32_t)5 << 16);
    return tmc_write_reg(0x10, val);   /* IHOLD_IRUN 0=???5=?? */
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

    tmc_timing_calibrate();   /* ???? TMC UART ????9600bps? */

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

void Stepper_Enable(uint8_t enable)
{
    stepper_enabled = enable;
    if (enable) {
        GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN);
        if (!tmc_initialized &&
            (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
             g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209)) {
            if (tmc_set_current(g_sys.params.motor_current) == 0) tmc_initialized = 1;
            else tmc_initialized = 1;    /* UART????????VREF???? */
        }
        if (tmc_initialized) Stepper_SetSilent(g_sys.params.motor_stealthchop);
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

/* TMC2208/2209 CHOPCONF(0x6C) bit30=1 → stealthChop 静音模式（读改写保留其它位） */
void Stepper_SetSilent(uint8_t en)
{
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    if (!is_tmc) return;
    uint32_t v = tmc_read_reg(0x6C);
    if (v == 0xFFFFFFFFU) return;          /* UART 无应答：不盲写 */
    if (en) v |= (1UL << 30);
    else    v &= ~(1UL << 30);
    tmc_write_reg(0x6C, v);
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

    /* ???????????? */
    if (resting) {
        uint32_t now = SystemTime_Millis();
        if ((int32_t)(now - rest_until_ms) >= 0) {
            resting = 0;
            work_done = 0;
            steps_in_unit = 0;
            if (stepper_enabled) {
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
    if (stepper_enabled && motor_running &&
        (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
         g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209)) {
        static uint32_t last_check = 0;
        uint32_t now = SystemTime_Millis();
        if ((uint32_t)(now - last_check) >= 1000U) {
            last_check = now;
            uint32_t st = tmc_read_reg(0x6F);
            if (st != 0xFFFFFFFFU && (st & ((1UL << 25) | (1UL << 26)))) {
                TIM_Cmd(TIM2, DISABLE);
                motor_running = 0;
                resting = 1;
                rest_until_ms = SystemTime_Millis() + ((rest_ms > 0) ? rest_ms : 30000U);
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

/* ?? TMC ?? UART ???????
 * ? IFCNT(0x02)??????????????????????? CRC ????????
     * returns 1=ok 0=fail/timeout */
uint8_t Stepper_TmcComOk(void)
{
    uint32_t v = tmc_read_reg(0x02);
    return (v != 0xFFFFFFFFU) ? 1 : 0;
}

/* TMC 通讯详查（比单次应答更可靠）：连读 IFCNT(0x02) 两次。
 * IFCNT 是芯片"已收到的有效报文计数"（4bit 循环）：真芯片第二次应答必然比第一次 +1；
 * 共线拉高/干扰造成的假 0x05 应答无法模拟递增。判定在线时回报最新计数。 */
uint8_t Stepper_TmcProbe(uint32_t *ifcnt)
{
    uint32_t a = tmc_read_reg(0x02);
    uint32_t b = tmc_read_reg(0x02);
    if (ifcnt) *ifcnt = b;
    if (a == 0xFFFFFFFFU || b == 0xFFFFFFFFU) return 0;
    return ((b & 0xFU) == ((a + 1U) & 0xFU)) ? 1U : 0U;
}
#endif /* BOOTLOADER_BUILD */