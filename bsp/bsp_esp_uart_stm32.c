#include "bsp_esp_uart.h"

#include "pin_config.h"
#include "system_time.h"

static volatile uint8_t rx_buffer[ESP_UART_RX_CAPACITY];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile int rx_overflow;

void EspUart_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef uart;

    rx_head = 0U;
    rx_tail = 0U;
    rx_overflow = 0;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin = PIN_ESP_TX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_ESP_TX_PORT, &gpio);
    gpio.GPIO_Pin = PIN_ESP_RX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(PIN_ESP_RX_PORT, &gpio);
    gpio.GPIO_Pin = PIN_ESP_EN_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(PIN_ESP_EN_PORT, &gpio);
    /* 注意：不要在这里改动 ESP 电源。供电完全由 EspUart_SetEnabled() 控制，
     * 避免 UART 初始化触发一次无谓的断电/上电，导致 ESP 重启异常。 */

    USART_StructInit(&uart);
    uart.USART_BaudRate = 115200U;
    uart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &uart);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    NVIC_EnableIRQ(USART1_IRQn);
    USART_Cmd(USART1, ENABLE);
}

void EspUart_SetEnabled(int enabled)
{
    /* P-MOS (AO3401) 高边开关：低电平导通，高电平关断 */
    if (enabled) GPIO_ResetBits(PIN_ESP_EN_PORT, PIN_ESP_EN_PIN);
    else GPIO_SetBits(PIN_ESP_EN_PORT, PIN_ESP_EN_PIN);
}

EspUartStatus_t EspUart_Write(const uint8_t *data,
                              uint16_t length, uint32_t timeout_ms)
{
    uint16_t index;
    uint32_t start;

    if ((data == 0) && (length != 0U)) return ESP_UART_ERROR_ARGUMENT;
    start = SystemTime_Millis();
    for (index = 0U; index < length; ++index) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
            if ((SystemTime_Millis() - start) >= timeout_ms) {
                return ESP_UART_ERROR_TIMEOUT;
            }
        }
        USART_SendData(USART1, data[index]);
    }
    return ESP_UART_OK;
}

int EspUart_ReadByte(uint8_t *byte)
{
    if ((byte == 0) || (rx_tail == rx_head)) return 0;
    *byte = rx_buffer[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % ESP_UART_RX_CAPACITY);
    return 1;
}

int EspUart_HasOverflow(void)
{
    return rx_overflow;
}

void EspUart_ClearRx(void)
{
    rx_tail = rx_head;
    rx_overflow = 0;
}

void EspUart_RxIrqHandler(void)
{
    /* 溢出（ORE）：CPU 在内部 Flash 擦写期间被 stall，单字节接收寄存器
     * 来不及读取，下一字节到达即溢出。读 SR 再读 DR 可清除 ORE+RXNE。 */
    if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET) {
        (void)USART1->SR;
        (void)USART1->DR;
        rx_overflow = 1;
    }
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t byte = (uint8_t)USART_ReceiveData(USART1);
        uint16_t next = (uint16_t)((rx_head + 1U) % ESP_UART_RX_CAPACITY);
        if (next == rx_tail) {
            rx_overflow = 1;
        } else {
            rx_buffer[rx_head] = byte;
            rx_head = next;
        }
    }
}

void USART1_IRQHandler(void)
{
    EspUart_RxIrqHandler();
}
