#ifndef BOOTLOADER_BUILD
/*
 * mod_wifi_manager.c
 * ESP01S WiFi Manager
 */
#include "mod_wifi_manager.h"
#include "mod_wifi_config.h"
#include "mod_web_server.h"
#include "bsp_esp_uart.h"
#include "system_time.h"
#include "system_config.h"
#include "pin_config.h"
#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define STA_TIMEOUT     60000
#define SCAN_TIMEOUT    15000

static WiFiRunState_t wifi_state = WIFI_OFF;
static char wifi_ip[16] = {0};
static WiFiScanResult_t scan_results[WIFI_SCAN_MAX];
static uint8_t scan_count = 0;
static uint8_t scan_done = 0;
static uint8_t is_scanning = 0;

typedef enum {
    S_OFF, S_POWER_ON, S_CHECK_SAVED, S_TRY_STA, S_WAIT_STA, S_CONNECTED,
    S_TRY_NEXT, S_ALL_FAILED, S_START_AP, S_AP_RUNNING,
    S_SCAN_START, S_SCAN_WAIT, S_SCAN_DONE, S_CONNECT_FROM_SCAN,
} State_t;

static State_t state = S_OFF;
static uint32_t state_tick = 0;
static uint8_t current_wifi_idx = 0;
static char pending_ssid[WIFI_SSID_LEN];
static char pending_password[64];
static void ParseCWLAP(char *line);
static void ParseLine(char *line, uint16_t len);
static void Delay_ms(uint16_t ms);

static void ESP_Send(const char *s)
{
    uint16_t len = (uint16_t)strlen(s);
    EspUart_Write((const uint8_t*)s, len, 1000);
    uint8_t crlf[] = {'\r', '\n'};
    EspUart_Write(crlf, 2, 1000);
}

static int WaitResponse(const char *expect, uint32_t timeout)
{
    uint32_t start = SystemTime_Millis();
    char buf[256] = {0};
    uint8_t idx = 0;
    uint8_t byte;
    while (SystemTime_Millis() - start < timeout) {
        while (EspUart_ReadByte(&byte) == 0) {
            if (idx < 255) buf[idx++] = (char)byte;
            buf[idx] = '\0';
            if (strstr(buf, expect)) return 0;
            if (strstr(buf, "ERROR") || strstr(buf, "FAIL")) return -1;
        }
    }
    return -1;
}

void WiFiManager_Init(void)
{
    EspUart_Init();
    WiFiConfig_Init();
    GPIO_ResetBits(PIN_ESP_EN_PORT, PIN_ESP_EN_PIN); /* P-MOS: 低=开，上电 */
    state = S_POWER_ON;
    state_tick = SystemTime_Millis();
}

void WiFiManager_Process(void)
{
    static char line[512];
    static uint16_t li = 0;
    uint8_t byte;

    while (EspUart_ReadByte(&byte) == 0) {
        char c = (char)byte;
        if (!is_scanning) {
            if (li < 511) line[li++] = c;
            if (li >= 2 && line[li-2] == '\r' && line[li-1] == '\n') {
                line[li] = '\0';
                ParseLine(line, li);
                li = 0;
            }
        } else {
            if (li < 511) line[li++] = c;
            line[li] = '\0';
            if (li > 10 && strncmp(line, "+CWLAP:", 7) == 0) {
                if (line[li-2] == '\r' && line[li-1] == '\n') { ParseCWLAP(line); li = 0; }
            } else if (strstr(line, "OK\r\n") != NULL) {
                scan_done = 1; is_scanning = 0; state = S_AP_RUNNING; li = 0;
            }
        }
    }

    switch (state) {
        case S_POWER_ON:
            if (SystemTime_Millis() - state_tick > 1000) {
                ESP_Send("AT");
                if (WaitResponse("OK", 1000) == 0) { state = S_CHECK_SAVED; state_tick = SystemTime_Millis(); }
            }
            break;
        case S_CHECK_SAVED: {
            uint8_t saved_count = WiFiConfig_GetValidCount();
            if (saved_count > 0) { current_wifi_idx = 0; state = S_TRY_STA; state_tick = SystemTime_Millis(); }
            else { state = S_START_AP; state_tick = SystemTime_Millis(); }
            break;
        }
        case S_TRY_STA: {
            WiFiConfig_t cfg;
            WiFiConfig_Read(&cfg);
            if (current_wifi_idx < WIFI_MAX_SAVED && cfg.entries[current_wifi_idx].valid) {
                ESP_Send("AT+CWMODE=1");
                Delay_ms(200);
                char cmd[128];
                sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", cfg.entries[current_wifi_idx].ssid, cfg.entries[current_wifi_idx].password);
                ESP_Send(cmd);
                state = S_WAIT_STA;
                state_tick = SystemTime_Millis();
            } else { state = S_ALL_FAILED; }
            break;
        }
        case S_WAIT_STA:
            if (WaitResponse("OK", 15000) == 0) {
                wifi_state = WIFI_STA_CONNECTED; g_sys.wifi_connected = 1; g_sys.wifi_ap_mode = 0;
                ESP_Send("AT+CIFSR");
                Delay_ms(500);
                state = S_CONNECTED;
            } else if (SystemTime_Millis() - state_tick > 15000) {
                current_wifi_idx++;
                uint8_t saved_count = WiFiConfig_GetValidCount();
                if (current_wifi_idx < saved_count) { state = S_TRY_STA; state_tick = SystemTime_Millis(); }
                else { state = S_ALL_FAILED; }
            }
            break;
        case S_ALL_FAILED:
            wifi_state = WIFI_OFF; g_sys.wifi_connected = 0;
            state = S_START_AP; state_tick = SystemTime_Millis();
            break;
        case S_START_AP:
            ESP_Send("AT+CWMODE=2");
            Delay_ms(200);
            ESP_Send("AT+CWSAP=\"Dryer_001\",\"12345678\",5,0");
            Delay_ms(200);
            ESP_Send("AT+CIPMUX=1");
            Delay_ms(200);
            ESP_Send("AT+CIPSERVER=1,80");
            Delay_ms(200);
            ESP_Send("AT+CIFSR");
            Delay_ms(500);
            wifi_state = WIFI_AP_MODE; g_sys.wifi_ap_mode = 1; g_sys.wifi_connected = 1;
            state = S_AP_RUNNING;
            break;
        case S_AP_RUNNING:
            if (scan_done) { WebServer_SetScanResults((const char*)scan_results, scan_count); scan_done = 0; }
            break;
        default: break;
    }
}

static void ParseCWLAP(char *line)
{
    if (scan_count >= WIFI_SCAN_MAX) return;
    char *p = line + 7;
    while (*p == '(' || *p == ',') p++;
    int sec; sscanf(p, "%d", &sec);
    while (*p && *p != ',') p++; if (*p) p++;
    while (*p == '"') p++;
    int len = 0;
    while (p[len] && p[len] != '"') len++;
    if (len > 0 && len < WIFI_SSID_LEN) {
        strncpy(scan_results[scan_count].ssid, p, len);
        scan_results[scan_count].ssid[len] = '\0';
        scan_results[scan_count].secure = (sec != 0);
        scan_count++;
    }
}

static void ParseLine(char *line, uint16_t len)
{
    (void)len;
    if (strncmp(line, "+CIFSR", 6) == 0) {
        char *ip = strstr(line, "\"");
        if (ip) { ip++; char *end = strchr(ip, '"'); if (end) { *end = '\0'; strncpy(wifi_ip, ip, 15); wifi_ip[15] = '\0'; } }
        strncpy(g_sys.wifi_ip, wifi_ip, 15); g_sys.wifi_ip[15] = '\0';
    }
}

WiFiRunState_t WiFiManager_GetState(void) { return wifi_state; }
const char* WiFiManager_GetIP(void) { return wifi_ip; }
void WiFiManager_StartScan(void) { scan_count = 0; scan_done = 0; is_scanning = 1; ESP_Send("AT+CWLAP"); }
uint8_t WiFiManager_GetScanCount(void) { return scan_count; }
void WiFiManager_Connect(const char *ssid, const char *password)
{
    strncpy(pending_ssid, ssid, WIFI_SSID_LEN - 1); pending_ssid[WIFI_SSID_LEN - 1] = '\0';
    strncpy(pending_password, password, 63); pending_password[63] = '\0';
    WiFiConfig_Add(pending_ssid, pending_password);
    state = S_TRY_STA; state_tick = SystemTime_Millis();
}
void WiFiManager_Stop(void) { state = S_OFF; wifi_state = WIFI_OFF; g_sys.wifi_connected = 0; GPIO_SetBits(PIN_ESP_EN_PORT, PIN_ESP_EN_PIN); }

static void Delay_ms(uint16_t ms) { for (volatile uint32_t i = 0; i < ms * 7200; i++); }
#endif /* BOOTLOADER_BUILD */