/*
 * SFUD 平台移植：SPI1 与 TFT 共享，经 Spi1Bus 仲裁。
 * 每次 spi->wr 调用 = 一次完整 SPI1 总线事务（Acquire->Select->Transfer->Deselect->Release）。
 * W25Q128 的 WREN 与命令可分属两次 CS 会话（WEL 锁存保持），因此跨调用被 TFT 抢占也安全。
 */

#include <sfud.h>
#include "bsp_spi1_bus.h"

/* 空日志：本项目无调试串口输出 */
void sfud_log_debug(const char *file, const long line, const char *format, ...) {
    (void)file; (void)line; (void)format;
}
void sfud_log_info(const char *format, ...) {
    (void)format;
}

/* 100us 延时（等待 Flash 忙碌时调用） */
static void retry_delay_100us(void) {
    volatile uint32_t d = 600;
    while (d--) { }
}

/* SPI 写+读：CS 在一个事务内保持低 */
static sfud_err spi_write_read(const sfud_spi *spi, const uint8_t *write_buf, size_t write_size,
                               uint8_t *read_buf, size_t read_size) {
    Spi1BusStatus_t bs;
    (void)spi;

    if (Spi1Bus_Acquire(SPI1_BUS_OWNER_W25Q128, 0U) != SPI1_BUS_OK) {
        return SFUD_ERR_TIMEOUT;   /* TFT 正占用总线：调用方重试 */
    }
    bs = Spi1Bus_Select(SPI1_BUS_OWNER_W25Q128);
    if (bs == SPI1_BUS_OK && write_size != 0U) {
        bs = Spi1Bus_Transfer(write_buf, 0, write_size, 10U);
    }
    if (bs == SPI1_BUS_OK && read_size != 0U) {
        bs = Spi1Bus_Transfer(0, read_buf, read_size, 10U);
    }
    Spi1Bus_Deselect(SPI1_BUS_OWNER_W25Q128);
    Spi1Bus_Release(SPI1_BUS_OWNER_W25Q128);
    return (bs == SPI1_BUS_OK) ? SFUD_SUCCESS : SFUD_ERR_TIMEOUT;
}

/* 总线仲裁按事务完成，无需额外锁 */
static void spi_lock(const sfud_spi *spi) { (void)spi; }
static void spi_unlock(const sfud_spi *spi) { (void)spi; }

sfud_err sfud_spi_port_init(sfud_flash *flash) {
    switch (flash->index) {
    case SFUD_W25Q128_DEVICE_INDEX: {
        flash->spi.wr = spi_write_read;
        flash->spi.lock = spi_lock;
        flash->spi.unlock = spi_unlock;
        flash->spi.user_data = NULL;
        flash->retry.delay = retry_delay_100us;
        flash->retry.times = 60 * 10000;   /* 约60s超时 */
        break;
    }
    default:
        break;
    }
    return SFUD_SUCCESS;
}
