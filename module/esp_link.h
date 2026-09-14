/*
 * esp_link.h
 * App 杩愯屾椂 ESP01S(QiMingXing 鑷瀹氫箟 AT 鍥轰欢) 閾捐矾灞傦細
 *   - WiFi 寮鍏虫満锛圓T+WEBSTART / AT+WEBCLOSE锛岃繛鎺ュ垽鏂鍦 ESP 鍐呴儴瀹屾垚锛
 *   - 閰嶇綉 / 閲嶆柊閰嶇綉锛圓T+CFGAP锛孉T+CLRCFG+AT+CFGAP锛
 *   - 1Hz 鍚戠綉椤垫帹閫佹湰鏈(涓)涓庢墍鏈夊凡鍏ョ綉浠庢満鐨 DATA 鐘舵佽
 *   - 瑙ｆ瀽娴忚堝櫒缁 ESP 杞鍙戞潵鐨 JSON 鍛戒护锛圚ELLO/PRESET_GET/SAVE/DELETE/APPLY/
 *     PARAM_SET/RUN/GLOBAL/CAN_MODE锛
 * 涓插彛涓鸿屽崗璁锛欰T 鍛戒护 + 浠 '{' 寮澶寸殑 JSON 琛屻
 */
#ifndef ESP_LINK_H
#define ESP_LINK_H

#include <stdint.h>

typedef enum {
    ESPLINK_OFF = 0,      /* WiFi 鍏抽棴锛孍SP 鏂鐢 */
    ESPLINK_BOOT,         /* ESP 涓婄數绛夊緟 AT OK */
    ESPLINK_CONNECTING,   /* 宸插彂 WEBSTART锛岀瓑寰 +IP/+AP */
    ESPLINK_ONLINE,       /* STA 宸茶繛鎺ワ紙+IP锛 */
    ESPLINK_CONFIG        /* AP 閰嶇綉妯″紡锛+AP锛 */
} EspLinkState_t;

void EspLink_Init(void);              /* 涓婄數鏃跺簭鐢 Process 澶勭悊锛岄渶鍦 main 璋冧竴娆 */
void EspLink_Process(void);           /* 涓诲惊鐜姣忓湀璋冪敤 */
void EspLink_OnToggle(uint8_t on);    /* WiFi 寮鍏冲彉鍖栵紙UI 鍗曞嚮璋冪敤锛屽惈棣栨″紑鏈烘寚浠わ級 */
void EspLink_StartConfig(void);       /* 閲嶆柊閰嶇綉锛氭竻5缁勫弬鏁 + 寮閰嶇綉AP */
void EspLink_OpenConfig(void);        /* 鎵嬪姩杩涘叆閰嶇綉AP */
void EspLink_PushNow(uint8_t force);  /* 1Hz 鎺ㄩ佸叆鍙ｏ紙Process 鍐呴儴涔熻皟鐢锛 */
EspLinkState_t EspLink_State(void);
const char *EspLink_IP(void);         /* "+IP" 鍥炴姤鐨 IP锛屾湭杩炴帴涓虹┖涓 */

/* 闊充箰涓婁紶 AP锛氬紑鍚/鍏抽棴锛圓P 鎴 APSTA锛屽彇鍐充簬鏄鍚﹀凡杩 STA锛 */
void EspLink_MusicOpenAp(void);
void EspLink_MusicCloseAp(void);
void EspLink_NotifyPresetsChanged(void); /* 预设增删改后推送预设表给网页 */

#endif /* ESP_LINK_H */
