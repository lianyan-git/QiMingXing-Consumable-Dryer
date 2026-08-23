#ifndef BOOTLOADER_BUILD
#include "bsp_stepper.h"
#include "pin_config.h"
#include "stm32f10x.h"
#include "system_time.h"
#include "system_config.h"

#define STEP_PERIOD_US 1000

/* TMC2209 ?? UART 9600 baud — 72MHz/(720 ?*~10 ??/?) ? 100us/bit */
#define TMC_UART_DELAY  720

static volatile uint8_t stepper_enabled = 0;
static volatile uint8_t motor_running = 0;
static volatile int32_t motor_remaining_steps = 0;
static volatile uint8_t motor_direction_cw = 1;
static volatile uint16_t step_period_us = STEP_PERIOD_US;
static volatile uint32_t last_step_time = 0;
static volatile int32_t target_steps = 0;
static volatile int8_t  osc_dir = 1;       /* ?????1=??, -1=?? */
static uint8_t tmc_initialized = 0;

/* ?? TMC2209 ?? UART ?? ?? */
static void tmc_delay(void) { volatile uint32_t d; for (d = 0; d < TMC_UART_DELAY; d++); }

static void tmc_uart_send_byte(uint8_t byte)
{
    uint8_t i;
    GPIO_ResetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);  tmc_delay();  /* start */
    for (i = 0; i < 8; i++) {
        if (byte & 0x01) GPIO_SetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);
        else              GPIO_ResetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);
        byte >>= 1;  tmc_delay();
    }
    GPIO_SetBits(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN);  tmc_delay();  /* stop */
}

static uint8_t tmc_uart_recv_byte(void)
{
    uint8_t i, byte = 0;
    volatile uint32_t timeout = 0;
    while (GPIO_ReadInputDataBit(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN)) {
        if (++timeout > 20000) return 0xFF;          /* ??~20ms????? */
    }
    tmc_delay();                                          /* mid-start */
    for (i = 0; i < 8; i++) {
        tmc_delay();
        if (GPIO_ReadInputDataBit(PIN_STEP_UART_PORT, PIN_STEP_UART_PIN)) byte |= (1 << i);
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

/* ? TMC2209 ???????????? 0=??? */
static uint8_t tmc_write_reg(uint8_t reg, uint32_t data)
{
    uint8_t buf[7], crc = 0, i;
    buf[0] = 0x00;  buf[1] = reg;
    buf[2] = (uint8_t)(data);        buf[3] = (uint8_t)(data >> 8);
    buf[4] = (uint8_t)(data >> 16);  buf[5] = (uint8_t)(data >> 24);
    for (i = 0; i < 6; i++) crc ^= buf[i];
    buf[6] = crc;

    tmc_uart_tx_mode();
    tmc_uart_send_byte(0x55);                        /* sync */

    tmc_uart_rx_mode();
    if (tmc_uart_recv_byte() != 0x55) {              /* echo */
        tmc_uart_tx_mode();
        return 1;
    }

    tmc_uart_tx_mode();
    for (i = 0; i < 7; i++) tmc_uart_send_byte(buf[i]);
    tmc_uart_rx_mode();
    return 0;
}

/* ?? TMC2209 ??/?????motor_current ?? 0.1A (2=0.2A...6=0.6A)
 * CS = motor_current*18/10 —— ?? 0.11? ???? + VREF~2.5V ???? */
static uint8_t tmc_set_current(uint8_t motor_current)
{
    uint8_t cs = (uint8_t)((uint16_t)motor_current * 18U / 10U);
    if (cs < 1) cs = 1;  if (cs > 31) cs = 31;
    uint32_t val = (uint32_t)cs | ((uint32_t)cs << 8) | ((uint32_t)5 << 16);
    return tmc_write_reg(0x10, val);   /* IHOLD_IRUN??? 0=?? */
}

/* ?? Stepper ??? ?? */

void Stepper_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    g.GPIO_Pin = PIN_STEP_EN_PIN | PIN_STEP_STEP_PIN | PIN_STEP_DIR_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_STEP_EN_PORT, &g);

    GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN);   /* ???? (???=?) */
    GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_STEP_PIN);
    GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_DIR_PIN);

    tmc_initialized = 0;
}

void Stepper_Enable(uint8_t enable)
{
    stepper_enabled = enable;
    if (enable) {
        GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN);
        if (!tmc_initialized && g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209) {
            if (tmc_set_current(g_sys.params.motor_current) == 0) tmc_initialized = 1;
            else tmc_initialized = 1;    /* ??UART????????????????VREF?? */
        }
    } else {
        GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_EN_PIN);
        motor_running = 0;
        motor_remaining_steps = 0;
    }
}

static void set_dir(uint8_t fwd)
{
    motor_direction_cw = fwd;
    if (fwd) GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_DIR_PIN);
    else     GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_DIR_PIN);
}

void Stepper_SetSpeed(uint16_t steps_per_sec)
{
    if (steps_per_sec == 0) steps_per_sec = 1;
    uint32_t period = 1000000UL / steps_per_sec;
    if (period < 50) period = 50;
    step_period_us = (uint16_t)period;
}

void Stepper_Move(int32_t steps)
{
    if (!stepper_enabled) return;
    if (steps > 0) { set_dir(1); motor_remaining_steps = steps; }
    else if (steps < 0) { set_dir(0); motor_remaining_steps = steps; }
    else { motor_remaining_steps = 0; }
    if (motor_remaining_steps != 0) { motor_running = 1; last_step_time = SystemTime_Millis(); }
}

void Stepper_SetOscillate(int32_t steps)
{
    target_steps = steps;
    osc_dir = 1;                           /* ????? */
    if (stepper_enabled && target_steps != 0 && !motor_running) {
        motor_running = 1;
        last_step_time = SystemTime_Millis();
        set_dir(1);
        motor_remaining_steps = target_steps;
    }
}

void Stepper_Update(void)
{
    if (!stepper_enabled || !motor_running) return;
    uint32_t now_ms = SystemTime_Millis();
    if ((uint32_t)(now_ms - last_step_time) < ((step_period_us + 999) / 1000)) return;

    GPIO_SetBits(PIN_STEP_EN_PORT, PIN_STEP_STEP_PIN);
    /* ?????? ~2µs?TMC2209 ?? ?1µs? */
    { volatile uint32_t d; for (d = 0; d < 150; d++); }
    GPIO_ResetBits(PIN_STEP_EN_PORT, PIN_STEP_STEP_PIN);
    last_step_time = now_ms;

    if (motor_remaining_steps > 0) motor_remaining_steps--;
    else if (motor_remaining_steps < 0) motor_remaining_steps++;

    if (motor_remaining_steps == 0) {
        motor_running = 0;
        if (target_steps > 0) {
            osc_dir = -osc_dir;            /* ????????????… */
            Stepper_Move(osc_dir * target_steps);
            /* ??????????????????? */
        }
    }
}

uint8_t Stepper_IsRunning(void)
{
    return motor_running;
}
#endif /* BOOTLOADER_BUILD */