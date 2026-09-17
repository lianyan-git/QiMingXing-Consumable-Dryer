#ifndef BSP_ESP_UART_H
#define BSP_ESP_UART_H

#include <stdint.h>

#define ESP_UART_RX_CAPACITY 8192U

typedef enum {
    ESP_UART_OK = 0,
    ESP_UART_ERROR_ARGUMENT = -1,
    ESP_UART_ERROR_TIMEOUT = -2
} EspUartStatus_t;

void EspUart_Init(void);
void EspUart_SetEnabled(int enabled);
EspUartStatus_t EspUart_Write(const uint8_t *data,
                              uint16_t length, uint32_t timeout_ms);
int EspUart_ReadByte(uint8_t *byte);
int EspUart_HasOverflow(void);
void EspUart_ClearRx(void);
void EspUart_ResetOverflow(void);   /* 只清溢出标志, 保留已缓冲数据(新上传会话建立时调用) */
void EspUart_RxIrqHandler(void);

#endif /* BSP_ESP_UART_H */
