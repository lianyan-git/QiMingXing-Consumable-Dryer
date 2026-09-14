/*
 * wifi_config.h
 * WiFi配置管理 - 支持3个WiFi，循环覆盖
 */

#ifndef MOD_WIFI_CONFIG_H
#define MOD_WIFI_CONFIG_H

#include <stdint.h>

#define WIFI_MAX_SAVED          3       // 最多保存3个WiFi
#define WIFI_SSID_LEN           32
#define WIFI_PASS_LEN           64

#define WIFI_CONNECT_TIMEOUT_MS 60000   // 1分钟连接超时
#define WIFI_MAGIC              0xA5A5A5A5

// 单个WiFi配置
typedef struct {
    char ssid[WIFI_SSID_LEN];
    char password[WIFI_PASS_LEN];
    uint8_t valid;              // 0=无效, 1=有效
} WiFiEntry_t;

// WiFi配置集合
typedef struct {
    uint32_t magic;
    uint32_t version;           // 写入版本号(磨损均衡)
    WiFiEntry_t entries[WIFI_MAX_SAVED];
    uint8_t last_used_index;    // 上次使用的WiFi索引
    uint32_t checksum;
} WiFiConfig_t;

// 初始化
void WiFiConfig_Init(void);

// 读取配置
int WiFiConfig_Read(WiFiConfig_t *config);

// 写入配置 (自动磨损均衡)
int WiFiConfig_Write(WiFiConfig_t *config);

// 添加/更新WiFi (自动找空位或覆盖最旧的)
int WiFiConfig_Add(const char *ssid, const char *password);

// 获取下一个写入位置 (循环覆盖)
uint8_t WiFiConfig_GetNextSlot(void);

// 获取有效WiFi数量
uint8_t WiFiConfig_GetValidCount(void);

#endif /* MOD_WIFI_CONFIG_H */
