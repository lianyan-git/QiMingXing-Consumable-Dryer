#include "bsp_w25q128.h"

#include "bsp_spi1_bus.h"
#include "platform_contract.h"

#define W25_CMD_WRITE_ENABLE  UINT8_C(0x06)
#define W25_CMD_READ_STATUS   UINT8_C(0x05)
#define W25_CMD_READ_DATA     UINT8_C(0x03)
#define W25_CMD_PAGE_PROGRAM  UINT8_C(0x02)
#define W25_CMD_SECTOR_ERASE  UINT8_C(0x20)
#define W25_CMD_READ_JEDEC_ID UINT8_C(0x9F)
#define W25_CMD_WRITE_STATUS  UINT8_C(0x01)

#define W25_SPI_TIMEOUT_MS     10U
#define W25_PROGRAM_TIMEOUT_MS 50U
#define W25_ERASE_TIMEOUT_MS   3000U

static W25Q128_ServiceFn service_callback;

void W25Q128_SetServiceCallback(W25Q128_ServiceFn service)
{
    service_callback = service;
}

static W25Q128_Status_t map_bus_status(Spi1BusStatus_t status)
{
    if (status == SPI1_BUS_ERROR_TIMEOUT) {
        return W25Q128_ERROR_TIMEOUT;
    }
    return (status == SPI1_BUS_OK) ? W25Q128_OK : W25Q128_ERROR_BUS;
}

static int range_is_valid(uint32_t address, uint32_t length)
{
    return (address <= PLATFORM_EXT_FLASH_SIZE) &&
           (length <= (PLATFORM_EXT_FLASH_SIZE - address));
}

static Spi1BusStatus_t transfer(const uint8_t *tx, uint8_t *rx, uint32_t length)
{
    return Spi1Bus_Transfer(tx, rx, length, W25_SPI_TIMEOUT_MS);
}

static W25Q128_Status_t command_only(uint8_t command)
{
    Spi1BusStatus_t bus_status;
    bus_status = Spi1Bus_Select(SPI1_BUS_OWNER_W25Q128);
    if (bus_status == SPI1_BUS_OK) {
        bus_status = transfer(&command, 0, 1U);
    }
    Spi1Bus_Deselect(SPI1_BUS_OWNER_W25Q128);
    return map_bus_status(bus_status);
}

static W25Q128_Status_t write_enable(void)
{
    return command_only(W25_CMD_WRITE_ENABLE);
}

static W25Q128_Status_t read_status(uint8_t *status)
{
    uint8_t command = W25_CMD_READ_STATUS;
    Spi1BusStatus_t bus_status;

    bus_status = Spi1Bus_Select(SPI1_BUS_OWNER_W25Q128);
    if (bus_status == SPI1_BUS_OK) {
        bus_status = transfer(&command, 0, 1U);
    }
    if (bus_status == SPI1_BUS_OK) {
        bus_status = transfer(0, status, 1U);
    }
    Spi1Bus_Deselect(SPI1_BUS_OWNER_W25Q128);
    return map_bus_status(bus_status);
}

W25Q128_Status_t W25Q128_WREN(void)
{
    return write_enable();
}

W25Q128_Status_t W25Q128_ReadSR1(uint8_t *sr1)
{
    return read_status(sr1);
}

static W25Q128_Status_t wait_ready(uint32_t timeout_ms)
{
    uint32_t attempts = timeout_ms + 1U;
    uint8_t status;
    W25Q128_Status_t result;

    while (attempts-- != 0U) {
        if (service_callback != 0) service_callback();
        result = read_status(&status);
        if (result != W25Q128_OK) {
            return result;
        }
        if ((status & 1U) == 0U) {
            return W25Q128_OK;
        }
    }
    return W25Q128_ERROR_TIMEOUT;
}

static W25Q128_Status_t send_addressed_command(uint8_t command,
                                                uint32_t address,
                                                const uint8_t *write_data,
                                                uint8_t *read_data,
                                                uint32_t length)
{
    uint8_t header[4];
    Spi1BusStatus_t bus_status;

    header[0] = command;
    header[1] = (uint8_t)(address >> 16);
    header[2] = (uint8_t)(address >> 8);
    header[3] = (uint8_t)address;
    bus_status = Spi1Bus_Select(SPI1_BUS_OWNER_W25Q128);
    if (bus_status == SPI1_BUS_OK) {
        bus_status = transfer(header, 0, sizeof(header));
    }
    if ((bus_status == SPI1_BUS_OK) && (length != 0U)) {
        bus_status = transfer(write_data, read_data, length);
    }
    Spi1Bus_Deselect(SPI1_BUS_OWNER_W25Q128);
    return map_bus_status(bus_status);
}

W25Q128_Status_t W25Q128_Init(void)
{
    uint32_t jedec_id;
    W25Q128_Status_t result;

    if (Spi1Bus_Init() != SPI1_BUS_OK) {
        return W25Q128_ERROR_BUS;
    }
    result = W25Q128_GetJedecId(&jedec_id);
    if (result != W25Q128_OK) {
        return result;
    }
    return (jedec_id == W25Q128_EXPECTED_JEDEC_ID) ? W25Q128_OK : W25Q128_ERROR_ID;
}

W25Q128_Status_t W25Q128_GetJedecId(uint32_t *jedec_id)
{
    uint8_t command = W25_CMD_READ_JEDEC_ID;
    uint8_t id[3];
    Spi1BusStatus_t bus_status;

    if (jedec_id == 0) {
        return W25Q128_ERROR_ARGUMENT;
    }
    bus_status = Spi1Bus_Acquire(SPI1_BUS_OWNER_W25Q128, 0U);
    if (bus_status != SPI1_BUS_OK) {
        return map_bus_status(bus_status);
    }
    bus_status = Spi1Bus_Select(SPI1_BUS_OWNER_W25Q128);
    if (bus_status == SPI1_BUS_OK) {
        bus_status = transfer(&command, 0, 1U);
    }
    if (bus_status == SPI1_BUS_OK) {
        bus_status = transfer(0, id, sizeof(id));
    }
    Spi1Bus_Deselect(SPI1_BUS_OWNER_W25Q128);
    Spi1Bus_Release(SPI1_BUS_OWNER_W25Q128);
    if (bus_status != SPI1_BUS_OK) {
        return map_bus_status(bus_status);
    }
    *jedec_id = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
    return W25Q128_OK;
}

W25Q128_Status_t W25Q128_Read(uint32_t address, uint8_t *buffer, uint32_t length)
{
    W25Q128_Status_t result;

    if (((buffer == 0) && (length != 0U))) {
        return W25Q128_ERROR_ARGUMENT;
    }
    if (!range_is_valid(address, length)) {
        return W25Q128_ERROR_RANGE;
    }
    if (length == 0U) {
        return W25Q128_OK;
    }
    if (Spi1Bus_Acquire(SPI1_BUS_OWNER_W25Q128, 0U) != SPI1_BUS_OK) {
        return W25Q128_ERROR_BUS;
    }
    result = send_addressed_command(W25_CMD_READ_DATA, address, 0, buffer, length);
    Spi1Bus_Release(SPI1_BUS_OWNER_W25Q128);
    return result;
}

W25Q128_Status_t W25Q128_Write(uint32_t address, const uint8_t *buffer, uint32_t length)
{
    uint32_t chunk;
    W25Q128_Status_t result = W25Q128_OK;

    if ((buffer == 0) && (length != 0U)) {
        return W25Q128_ERROR_ARGUMENT;
    }
    if (!range_is_valid(address, length)) {
        return W25Q128_ERROR_RANGE;
    }
    if (length == 0U) {
        return W25Q128_OK;
    }
    if (Spi1Bus_Acquire(SPI1_BUS_OWNER_W25Q128, 0U) != SPI1_BUS_OK) {
        return W25Q128_ERROR_BUS;
    }
    while ((length != 0U) && (result == W25Q128_OK)) {
        chunk = W25Q128_PAGE_SIZE - (address & (W25Q128_PAGE_SIZE - 1U));
        if (chunk > length) {
            chunk = length;
        }
        result = write_enable();
        if (result == W25Q128_OK) {
            result = send_addressed_command(W25_CMD_PAGE_PROGRAM, address, buffer, 0, chunk);
        }
        if (result == W25Q128_OK) {
            result = wait_ready(W25_PROGRAM_TIMEOUT_MS);
        }
        address += chunk;
        buffer += chunk;
        length -= chunk;
    }
    Spi1Bus_Release(SPI1_BUS_OWNER_W25Q128);
    return result;
}

W25Q128_Status_t W25Q128_EraseSector(uint32_t address)
{
    W25Q128_Status_t result;

    if (((address & (W25Q128_SECTOR_SIZE - 1U)) != 0U) ||
        !range_is_valid(address, W25Q128_SECTOR_SIZE)) {
        return W25Q128_ERROR_RANGE;
    }
    if (Spi1Bus_Acquire(SPI1_BUS_OWNER_W25Q128, 0U) != SPI1_BUS_OK) {
        return W25Q128_ERROR_BUS;
    }
    result = write_enable();
    if (result == W25Q128_OK) {
        result = send_addressed_command(W25_CMD_SECTOR_ERASE, address, 0, 0, 0U);
    }
    if (result == W25Q128_OK) {
        result = wait_ready(W25_ERASE_TIMEOUT_MS);
    }
    Spi1Bus_Release(SPI1_BUS_OWNER_W25Q128);
    return result;
}

W25Q128_Status_t W25Q128_ClearProtection(void)
{
    W25Q128_Status_t result;
    Spi1BusStatus_t bus_status;
    uint8_t cmd = W25_CMD_WRITE_STATUS;
    uint8_t data[2] = { 0x00, 0x00 };

    if (Spi1Bus_Acquire(SPI1_BUS_OWNER_W25Q128, 0U) != SPI1_BUS_OK) {
        return W25Q128_ERROR_BUS;
    }
    result = write_enable();
    if (result == W25Q128_OK) {
        bus_status = Spi1Bus_Select(SPI1_BUS_OWNER_W25Q128);
        if (bus_status == SPI1_BUS_OK) {
            bus_status = transfer(&cmd, 0, 1U);
        }
        if (bus_status == SPI1_BUS_OK) {
            bus_status = transfer(data, 0, sizeof(data));
        }
        Spi1Bus_Deselect(SPI1_BUS_OWNER_W25Q128);
        result = map_bus_status(bus_status);
    }
    if (result == W25Q128_OK) {
        result = wait_ready(W25_PROGRAM_TIMEOUT_MS);
    }
    Spi1Bus_Release(SPI1_BUS_OWNER_W25Q128);
    return result;
}

W25Q128_Status_t W25Q128_EraseRange(uint32_t address, uint32_t length)
{
    W25Q128_Status_t result;

    if (((address & (W25Q128_SECTOR_SIZE - 1U)) != 0U) ||
        ((length & (W25Q128_SECTOR_SIZE - 1U)) != 0U) ||
        !range_is_valid(address, length)) {
        return W25Q128_ERROR_RANGE;
    }
    while (length != 0U) {
        if (service_callback != 0) service_callback();
        result = W25Q128_EraseSector(address);
        if (result != W25Q128_OK) {
            return result;
        }
        address += W25Q128_SECTOR_SIZE;
        length -= W25Q128_SECTOR_SIZE;
    }
    return W25Q128_OK;
}

/* 非阻塞保存原语：只发命令不等待（配合外部轮询 ReadSR1）。
 * 每次只占用 SPI1 极短时间，可在后台与 TFT 刷新交错执行。 */
W25Q128_Status_t W25Q128_StartEraseSector(uint32_t address)
{
    W25Q128_Status_t result;

    if (((address & (W25Q128_SECTOR_SIZE - 1U)) != 0U) ||
        !range_is_valid(address, W25Q128_SECTOR_SIZE)) {
        return W25Q128_ERROR_RANGE;
    }
    if (Spi1Bus_Acquire(SPI1_BUS_OWNER_W25Q128, 0U) != SPI1_BUS_OK) {
        return W25Q128_ERROR_BUS;   /* TFT 正在使用总线，稍后再试 */
    }
    result = write_enable();
    if (result == W25Q128_OK) {
        result = send_addressed_command(W25_CMD_SECTOR_ERASE, address, 0, 0, 0U);
    }
    Spi1Bus_Release(SPI1_BUS_OWNER_W25Q128);
    return result;
}

W25Q128_Status_t W25Q128_StartWritePage(uint32_t address, const uint8_t *buffer, uint32_t length)
{
    W25Q128_Status_t result = W25Q128_OK;

    if (((address & (W25Q128_PAGE_SIZE - 1U)) != 0U) ||
        (buffer == 0) || (length == 0U) || (length > W25Q128_PAGE_SIZE) ||
        !range_is_valid(address, length)) {
        return W25Q128_ERROR_ARGUMENT;
    }
    if (Spi1Bus_Acquire(SPI1_BUS_OWNER_W25Q128, 0U) != SPI1_BUS_OK) {
        return W25Q128_ERROR_BUS;
    }
    result = write_enable();
    if (result == W25Q128_OK) {
        result = send_addressed_command(W25_CMD_PAGE_PROGRAM, address, buffer, 0, length);
    }
    Spi1Bus_Release(SPI1_BUS_OWNER_W25Q128);
    return result;
}
