/*
 * esp_link.h
 * App 运�时 ESP01S(QiMingXing �定义 AT 固件) 链路层：
 *   - WiFi �关机（AT+WEBSTART / AT+WEBCLOSE，连接判�� ESP 内部完成�
 *   - 配网 / 重新配网（AT+CFGAP，AT+CLRCFG+AT+CFGAP�
 *   - 1Hz 向网页推送本�(�)与所有已入网从机� DATA 状��
 *   - 解析浏�器� ESP �发来� JSON 命令（HELLO/PRESET_GET/SAVE/DELETE/APPLY/
 *     PARAM_SET/RUN/GLOBAL/CAN_MODE�
 * 串口为�协�：AT 命令 + � '{' �头的 JSON 行�
 */
#ifndef ESP_LINK_H
#define ESP_LINK_H

#include <stdint.h>

typedef enum {
    ESPLINK_OFF = 0,      /* WiFi 关闭，ESP �� */
    ESPLINK_BOOT,         /* ESP 上电等待 AT OK */
    ESPLINK_CONNECTING,   /* 已发 WEBSTART，等� +IP/+AP */
    ESPLINK_ONLINE,       /* STA 已连接（+IP� */
    ESPLINK_CONFIG        /* AP 配网模式�+AP� */
} EspLinkState_t;

void EspLink_Init(void);              /* 上电时序� Process 处理，需� main 调一� */
void EspLink_Process(void);           /* 主循�每圈调用 */
void EspLink_OnToggle(uint8_t on);    /* WiFi �关变化（UI 单击调用，含首�开机指令） */
void EspLink_StartConfig(void);       /* 重新配网：清5组参� + �配网AP */
void EspLink_OpenConfig(void);        /* 手动进入配网AP */
void EspLink_PushNow(uint8_t force);  /* 1Hz 推�入口（Process 内部也调�� */
EspLinkState_t EspLink_State(void);
const char *EspLink_IP(void);         /* "+IP" 回报� IP，未连接为空� */

/* 音乐上传 AP：开�/关闭（AP � APSTA，取决于�否已� STA� */
void EspLink_MusicOpenAp(void);
void EspLink_MusicCloseAp(void);
void EspLink_NotifyPresetsChanged(void); /* Ԥ����ɾ�ĺ�����Ԥ�������ҳ */

#endif /* ESP_LINK_H */
