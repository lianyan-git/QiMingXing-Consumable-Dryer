/*
 * SFUD 配置：单设备 W25Q128，关闭 SFDP（用内置芯片表），无调试输出。
 */
#ifndef _SFUD_CFG_H_
#define _SFUD_CFG_H_

/* #define SFUD_DEBUG_MODE */

/* #define SFUD_USING_SFDP */

#define SFUD_USING_FLASH_INFO_TABLE

enum {
    SFUD_W25Q128_DEVICE_INDEX = 0,
};

#define SFUD_FLASH_DEVICE_TABLE                                                \
{                                                                              \
    [SFUD_W25Q128_DEVICE_INDEX] = {.name = "W25Q128", .spi.name = "SPI1"},     \
}

/* #define SFUD_USING_QSPI */

#endif /* _SFUD_CFG_H_ */
