/*
 * esp01s_wifi_v4.h
 * ESP01S WiFi管理 v4 - 支持扫描附近WiFi
 * 
 * 功能:
 * 1. 开机尝试连接已保存WiFi (STA模式)
 * 2. 1分钟连不上 → 自动开AP模式
 * 3. AP模式下可扫描附近WiFi
 * 4. 选择WiFi + 输入密码 → 保存并连接
 */

#ifndef MOD_WIFI_MANAGER_H
#define MOD_WIFI_MANAGER_H

#include <stdint.h>

#define WIFI_SCAN_MAX       20      // 最大扫描数量
#define WIFI_SSID_LEN       32

// WiFi扫描结果
typedef struct {
    char ssid[WIFI_SSID_LEN];
    int8_t rssi;            // 信号强度 (负数，绝对值越小越强)
    uint8_t secure;         // 0=开放, 1=加密
} WiFiScanResult_t;

// WiFi运行状态
typedef enum {
    WIFI_STA_CONNECTING,
    WIFI_STA_CONNECTED,
    WIFI_AP_MODE,
    WIFI_SCANNING,          // 正在扫描
    WIFI_OFF,
} WiFiRunState_t;

// 初始化
void WiFiManager_Init(void);
void WiFiManager_Process(void);

// 获取状态
WiFiRunState_t WiFiManager_GetState(void);
const char* WiFiManager_GetIP(void);

// 扫描WiFi (AP模式下调用)
void WiFiManager_StartScan(void);
uint8_t WiFiManager_GetScanResults(WiFiScanResult_t *results, uint8_t max_count);
uint8_t WiFiManager_IsScanDone(void);

// 连接指定WiFi
void WiFiManager_Connect(const char *ssid, const char *password);

// 强制开AP
void WiFiManager_ForceAP(void);
void WiFiManager_Stop(void);

#endif /* MOD_WIFI_MANAGER_H */
