/*
 * web_server_v3.h
 * 网页服务器 v3 - 支持WiFi扫描+选择配网
 */

#ifndef MOD_WEB_SERVER_H
#define MOD_WEB_SERVER_H

#include <stdint.h>

// 初始化
void WebServer_Init(void);

// 主循环处理 (AP模式下调用)
void WebServer_Process(void);

// 处理HTTP请求
void WebServer_HandleRequest(int conn_id, uint8_t *data, uint16_t len);

// 设置扫描结果回调
void WebServer_SetScanResults(const char *ssids, uint8_t count);

#endif /* MOD_WEB_SERVER_H */
