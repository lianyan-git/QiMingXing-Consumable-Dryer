/*
 * esp_link.c
 * App 运�时 ESP01S(�定义 AT 固件) 链路层（行协�）：
 *   STM32→ESP: "AT+WEBSTART" / "AT+CFGAP"(配网) / "AT+CFGCLR"(�5组存�) /
 *              "AT+WEBCLOSE"(其后 STM32 ��) / � '{' �头的 JSON �(�发给浏�器)
 *   ESP→STM32: "OK" / "+IP:x.x.x.x"(STA 已连) / "+AP"(进入配网AP) / "+DISC"(掉线) /
 *              � '{' �头的 JSON �(浏�器命令原文)
 * JSON 命令对应 web.txt 协�：HELLO/PRESET_GET|SAVE|DELETE|APPLY/PARAM_SET/RUN/
 *   GLOBAL/CAN_MODE；STM32 � ACK，并 1Hz 推� master+在线从机 DATA 行�按�� PRESET_LIST�
 */
#ifndef BOOTLOADER_BUILD

#include "system_config.h"
#include "system_time.h"
#include "bsp_esp_uart.h"
#include "pin_config.h"
#include "can_cluster.h"
#include "esp_link.h"
#include "music_ota.h"
#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern void StartDrying(void);
extern void StopDrying(void);

#define LINK_BUF      256
#define PUSH_MS       1000U
#define BOOT_POLL_MS  500U
#define START_TMO_MS  9000U

static EspLinkState_t s_state = ESPLINK_OFF;
static uint8_t  s_pow_ms;              /* 上电稳定倒�时(×100ms) */
static uint32_t s_state_tick = 0;
static uint32_t s_last_push = 0;
static char     s_ip[16] = "";
static char     s_line[LINK_BUF];
static uint16_t s_li = 0;
static uint8_t  s_pending_cfg = 0;     /* OK 前收到的配网请求 */
static uint8_t  s_pending_clr = 0;     /* OK 前收到的重新配网请求 */
static uint8_t  s_pending_music = 0;   /* OK 前收到的音乐上传 AP 请求 */
static uint8_t  s_closing = 0;
static uint32_t s_close_tick = 0;
static char     s_out[896];            /* JSON 组包复用缓冲�12 预�最� ~850B� */

static void link_send(const char *s)
{
    EspUart_Write((const uint8_t *)s, (uint16_t)strlen(s), 500);
    EspUart_Write((const uint8_t *)"\r\n", 2, 100);
}

static void esp_power(uint8_t on)
{
    if (on) GPIO_ResetBits(PIN_ESP_EN_PORT, PIN_ESP_EN_PIN);   /* P-MOS：低=供电 */
    else    GPIO_SetBits(PIN_ESP_EN_PORT, PIN_ESP_EN_PIN);
}

/* ---------- � JSON 解析（格式由��代码生成/约定，键名不�特殊字�） ---------- */
static const char *jfind(const char *s, const char *key)
{
    char pat[20];
    sprintf(pat, "\"%s\":", key);
    return strstr(s, pat);
}

static const char *jval(const char *s, const char *key)
{
    const char *p = jfind(s, key);
    if (!p) return 0;
    p += strlen(key) + 3;
    while (*p == '"' || *p == ' ') p++;
    return p;
}

static long jnum(const char *s, const char *key, long dflt)
{
    const char *p = jval(s, key);
    if (!p) return dflt;
    return strtol(p, NULL, 10);
}

static int jbool(const char *s, const char *key)    /* true/1 视为� */
{
    const char *p = jval(s, key);
    if (!p) return 0;
    return (*p == 't' || *p == 'T' || *p == '1') ? 1 : 0;
}

static int jstr(const char *s, const char *key, char *buf, uint16_t n)
{
    const char *p = jfind(s, key);
    uint16_t i = 0;
    if (!p) { if (n) buf[0] = 0; return 0; }
    p += strlen(key) + 3;
    if (*p == '"') p++;
    while (*p && *p != '"' && i < (uint16_t)(n - 1)) buf[i++] = *p++;
    buf[i] = '\0';
    return (i != 0) ? 1 : 0;
}

/* 定位消息� "d": 之后的位�。网页� JSON � {"id":"RUN_169..","t":"RUN","d":{...}}�
 * 信封层也� "id" �—�直� jstr(line,"id") 会取到信� id 而非 d.id�
 * 导致 RUN/PARAM_SET 的目标��恒� "RUN_169.."（master 分支永不命中，启停烘干无效）�
 * d 内的��律从 droot(line) 起解析� */
static const char *droot(const char *s)
{
    const char *p = strstr(s, "\"d\":");
    return p ? (p + 4) : s;
}

static void send_ack(const char *line)
{
    char id[48];
    if (jstr(line, "id", id, sizeof(id))) {
        sprintf(s_out, "{\"t\":\"ACK\",\"d\":{\"cmd\":\"%s\"}}", id);
        link_send(s_out);
    }
}

/* ---------- 预�（主���部 flash，System_Save 落盘� ---------- */
static int preset_find(const char *name)
{
    uint8_t i;
    for (i = 0; i < g_sys.params.preset_count && i < PRESET_MAX; i++)
        if (strcmp(g_sys.params.presets[i].name, name) == 0) return (int)i;
    return -1;
}

static void preset_upsert(const char *name, long temp, long h, long m, long s)
{
    uint32_t sec = (uint32_t)h * 3600U + (uint32_t)m * 60U + (uint32_t)s;
    int idx;
    if (name[0] == 0 || temp < TEMP_MIN || temp > TEMP_MAX || sec == 0 || sec > 172799U) return;
    idx = preset_find(name);
    if (idx < 0) {
        if (g_sys.params.preset_count >= PRESET_MAX) return;
        idx = (int)g_sys.params.preset_count++;
    }
    strncpy(g_sys.params.presets[idx].name, name, PRESET_NAME_MAX);
    g_sys.params.presets[idx].name[PRESET_NAME_MAX] = 0;
    g_sys.params.presets[idx].temp = (uint8_t)temp;
    g_sys.params.presets[idx].time_sec = sec;
    System_RequestSave();
}

/* PRESET_DELETE {remain:[{name,temp,h,m,s}...]} � � remain 重建列表 */
static void preset_rebuild_from_remain(const char *line)
{
    Preset_t tmp[PRESET_MAX];
    uint8_t cnt = 0;
    const char *cur = strstr(line, "\"remain\"");
    const char *eod;
    if (!cur) return;
    eod = strstr(cur, "]");
    while (cnt < PRESET_MAX) {
        const char *p = strstr(cur, "\"name\"");
        char name[16];
        long t, h, m, s;
        if (!p || (eod && p > eod)) break;
        if (jstr(p, "name", name, sizeof(name))) {
            t = jnum(p, "temp", -1); h = jnum(p, "h", -1);
            m = jnum(p, "m", -1);    s = jnum(p, "s", -1);
            if (t >= TEMP_MIN && t <= TEMP_MAX && h >= 0 && m >= 0 && s >= 0) {
                strncpy(tmp[cnt].name, name, PRESET_NAME_MAX);
                tmp[cnt].name[PRESET_NAME_MAX] = 0;
                tmp[cnt].temp = (uint8_t)t;
                tmp[cnt].time_sec = (uint32_t)h * 3600U + (uint32_t)m * 60U + (uint32_t)s;
                cnt++;
            }
        }
        cur = p + 6;
    }
    if (cnt > 0) {
        memcpy(g_sys.params.presets, tmp, (size_t)cnt * sizeof(Preset_t));
        g_sys.params.preset_count = cnt;
        if (g_sys.params.current_preset >= cnt)
            g_sys.params.current_preset = (uint8_t)(cnt - 1U);
        System_RequestSave();
    }
}

/* 从机 h/m/s 分量�改：以集群内已收到的 dry_time_sec 为底重算 */
static uint32_t slave_time_apply(const char *id, const char *param, long v)
{
    const CanSlave_t *sl;
    uint32_t sec, s;
    int slot = atoi(id + 6) - 1;
    if (slot < 0 || slot >= CAN_SLAVE_MAX) return 0;
    sl = CAN_Cluster_GetSlave((uint8_t)slot);
    sec = sl ? sl->dry_time_sec : g_sys.params.dry_time_sec;
    s = sec;
    {
        uint32_t hh = sec / 3600U, mm = (sec % 3600U) / 60U, ss = sec % 60U;
        if (param[1] == 'H') hh = (uint32_t)v;
        else if (param[1] == 'M') mm = (uint32_t)v;
        else ss = (uint32_t)v;
        s = hh * 3600U + mm * 60U + ss;
    }
    if (s > 0 && s <= 172799U) CAN_Cluster_SendCtrl((uint8_t)slot, CAN_CTRL_SET_TIME, (int32_t)s);
    return s;
}

/* PARAM_SET 应用到指定�� */
static void apply_target(const char *id, const char *param, long v)
{
    int slave = (id[0] == 's' && id[1] == 'l');
    if (!slave) {
        if (!strcmp(param, "setTemp") && v >= TEMP_MIN && v <= TEMP_MAX) {
            g_sys.params.target_temp = (uint8_t)v;
            System_RequestSave();
        } else if (!strcmp(param, "setPtc") && v >= PTC_TEMP_MIN && v <= PTC_TEMP_MAX) {
            g_sys.params.ptc_max_temp = (uint8_t)v;
            System_RequestSave();
        } else if (param[0] == 't' && (param[1] == 'H' || param[1] == 'M' || param[1] == 'S')) {
            uint32_t sec = g_sys.params.dry_time_sec;
            uint32_t hh = sec / 3600U, mm = (sec % 3600U) / 60U, ss = sec % 60U;
            if (param[1] == 'H') hh = (uint32_t)v;
            else if (param[1] == 'M') mm = (uint32_t)v;
            else ss = (uint32_t)v;
            sec = hh * 3600U + mm * 60U + ss;
            if (sec > 0 && sec <= 172799U) {
                g_sys.params.dry_time_sec = sec;
                System_RequestSave();
            }
        }
    } else {
        int slot = atoi(id + 6) - 1;
        if (slot < 0 || slot >= CAN_SLAVE_MAX) return;
        if (!strcmp(param, "setTemp"))            CAN_Cluster_SendCtrl((uint8_t)slot, CAN_CTRL_SET_TEMP, v);
        else if (!strcmp(param, "setPtc"))        CAN_Cluster_SendCtrl((uint8_t)slot, CAN_CTRL_SET_PTC, v);
        else if (param[0] == 't')                 slave_time_apply(id, param, v);
    }
}

static int has_device(const char *line, const char *dev)
{
    const char *t = strstr(line, "\"targets\"");
    char pat[16];
    if (!t) return 0;
    sprintf(pat, "\"%s\"", dev);
    return strstr(t, pat) != NULL;
}

/* ---------- PRESET_LIST 推� ---------- */
static void send_preset_list(void)
{
    uint16_t len;
    uint8_t i, n = g_sys.params.preset_count;
    if (n > PRESET_MAX) n = PRESET_MAX;
    len = (uint16_t)sprintf(s_out,
        "{\"t\":\"PRESET_LIST\",\"d\":{\"current\":%u,\"connected\":%u,\"list\":[",
        (unsigned)g_sys.params.current_preset, (unsigned)g_sys.can_connected);
    for (i = 0; i < n && (uint32_t)(len + 96) < sizeof(s_out); i++) {
        len = (uint16_t)(len + (uint16_t)sprintf(s_out + len,
            "%s{\"name\":\"%s\",\"temp\":%u,\"h\":%lu,\"m\":%lu,\"s\":%lu}",
            i ? "," : "",
            g_sys.params.presets[i].name,
            (unsigned)g_sys.params.presets[i].temp,
            (unsigned long)(g_sys.params.presets[i].time_sec / 3600U),
            (unsigned long)((g_sys.params.presets[i].time_sec % 3600U) / 60U),
            (unsigned long)(g_sys.params.presets[i].time_sec % 60U)));
    }
    if (len + 4 < sizeof(s_out)) { strcpy(s_out + len, "]}}"); }
    link_send(s_out);
}

/* ---------- 命令分发 ---------- */
static void send_devs(void)
{
    int i;
    uint16_t len = 0;
    /* 主机 + 在线从机列表：名�=设�+序列号，序列号即 About 页序列号(device_id) */
    len = (uint16_t)sprintf(s_out, "{\"t\":\"DEVS\",\"d\":{\"list\":[");
    len += (uint16_t)sprintf(s_out + len, "{\"id\":\"master\",\"sn\":\"%lu\"}",
                             (unsigned long)System_GetDeviceId());
    for (i = 0; i < CAN_SLAVE_MAX; i++) {
        const CanSlave_t *sl = CAN_Cluster_GetSlave((uint8_t)i);
        char dev[12];
        if (!sl || !sl->online || sl->device_id == 0) continue;
        sprintf(dev, "slave_%d", i + 1);
        len += (uint16_t)sprintf(s_out + len, ",{\"id\":\"%s\",\"sn\":\"%lu\"}", dev,
                                 (unsigned long)sl->device_id);
    }
    if (len + 4 < sizeof(s_out)) { strcpy(s_out + len, "]}}"); }
    link_send(s_out);
}

static void web_cmd(const char *line)
{
    const char *d = droot(line);
    char t[16] = "";
    if (!jstr(line, "t", t, sizeof(t))) return;
    if (!strcmp(t, "ACK")) return;
    send_ack(line);

    if (!strcmp(t, "HELLO") || !strcmp(t, "PRESET_GET")) {
        send_devs();
        send_preset_list();
        EspLink_PushNow(1);
    }
    else if (!strcmp(t, "PRESET_SAVE")) {
        char name[16]; jstr(d, "name", name, sizeof(name));
        preset_upsert(name, jnum(d, "temp", -1), jnum(d, "h", 0), jnum(d, "m", 0), jnum(d, "s", 0));
        g_sys.ui_force_redraw = 1;
        link_send("{\"t\":\"PRESET_SAVED\"}");
    }
    else if (!strcmp(t, "PRESET_DELETE")) {
        preset_rebuild_from_remain(line);
        g_sys.ui_force_redraw = 1;
        link_send("{\"t\":\"PRESET_DELETED\"}");
    }
    else if (!strcmp(t, "PRESET_APPLY")) {
        char name[16];
        long temp = jnum(d, "temp", -1);
        uint32_t sec = (uint32_t)jnum(d, "h", 0) * 3600U +
                       (uint32_t)jnum(d, "m", 0) * 60U + (uint32_t)jnum(d, "s", 0);
        int i, idx;
        jstr(d, "name", name, sizeof(name));
        idx = preset_find(name);
        if (has_device(line, "master") && name[0] && temp >= TEMP_MIN && temp <= TEMP_MAX
            && sec > 0 && sec <= 172799U) {
            g_sys.params.target_temp = (uint8_t)temp;
            g_sys.params.dry_time_sec = sec;
            if (idx >= 0) g_sys.params.current_preset = (uint8_t)idx;
            System_RequestSave();
        }
        for (i = 0; i < CAN_SLAVE_MAX; i++) {
            char dev[12];
            const CanSlave_t *sl = CAN_Cluster_GetSlave((uint8_t)i);
            sprintf(dev, "slave_%d", i + 1);
            if (!sl || !sl->online) continue;
            if (has_device(line, dev)) {
                if (temp >= TEMP_MIN && temp <= TEMP_MAX)
                    CAN_Cluster_SendCtrl((uint8_t)i, CAN_CTRL_SET_TEMP, (int32_t)temp);
                if (sec > 0 && sec <= 172799U)
                    CAN_Cluster_SendCtrl((uint8_t)i, CAN_CTRL_SET_TIME, (int32_t)sec);
            }
        }
        g_sys.ui_force_redraw = 1;    /* 主机界面预�/时间立即刷新，不必�出页面再� */
        EspLink_PushNow(1);
    }
    else if (!strcmp(t, "PARAM_SET")) {
        char id[12] = "", p[8] = "";
        jstr(d, "id", id, sizeof(id));
        jstr(d, "param", p, sizeof(p));
        apply_target(id, p, jnum(d, "value", -1));
        EspLink_PushNow(1);           /* 参数变动立即回推同� */
    }
    else if (!strcmp(t, "RUN")) {
        char id[12] = "master";
        int run = jbool(d, "run");
        jstr(d, "id", id, sizeof(id));
        if (!strcmp(id, "master")) { if (run) StartDrying(); else StopDrying(); }
        else {
            int slot = atoi(id + 6) - 1;
            if (slot >= 0 && slot < CAN_SLAVE_MAX) CAN_Cluster_SendCtrl((uint8_t)slot, CAN_CTRL_RUN, run);
        }
        EspLink_PushNow(1);
    }
    else if (!strcmp(t, "GLOBAL")) {
        int on = jbool(d, "on");
        if (on) StartDrying(); else StopDrying();
        CAN_Cluster_SendCtrl(0xFF, CAN_CTRL_RUN, on);
        EspLink_PushNow(1);
    }
    else if (!strcmp(t, "CAN_MODE")) {
        int on = jbool(d, "on");
        if (on) {
            g_sys.params.can_enabled = 1;
            g_sys.params.can_role = 0;
        } else {
            g_sys.params.can_enabled = 0;
        }
        System_RequestSave();
        g_sys.ui_force_redraw = 1;    /* CAN 设置页开关状态立即显� */
        EspLink_PushNow(1);
    }
}

/* ---------- DATA 推� ---------- */
static void push_dev(const char *id, int run, float temp, float humi,
                     long wt, float ptc, uint32_t sec, uint32_t rem, int has_target)
{
    long st = (long)g_sys.params.target_temp;
    long sp = (long)g_sys.params.ptc_max_temp;
    if (has_target) {
        sprintf(s_out,
            "{\"t\":\"DATA\",\"d\":{\"id\":\"%s\",\"run\":%s,\"can\":%s,\"setTemp\":%ld,\"setPtc\":%ld,\"temp\":%.1f,\"humi\":%.1f,\"wtG\":%ld,\"ptc\":%.1f,\"tH\":%lu,\"tM\":%lu,\"tS\":%lu,\"rem\":%lu}}",
            id, run ? "true" : "false", (g_sys.params.can_enabled ? "true" : "false"), st, sp, temp, humi, wt, ptc,
            (unsigned long)(sec / 3600U), (unsigned long)((sec % 3600U) / 60U), (unsigned long)(sec % 60U),
            (unsigned long)rem);
    } else {
        sprintf(s_out,
            "{\"t\":\"DATA\",\"d\":{\"id\":\"%s\",\"run\":%s,\"temp\":%.1f,\"humi\":%.1f,\"wtG\":%ld,\"ptc\":%.1f,\"tH\":%lu,\"tM\":%lu,\"tS\":%lu,\"rem\":%lu}}",
            id, run ? "true" : "false", temp, humi, wt, ptc,
            (unsigned long)(sec / 3600U), (unsigned long)((sec % 3600U) / 60U), (unsigned long)(sec % 60U),
            (unsigned long)rem);
    }
    link_send(s_out);
}

void EspLink_PushNow(uint8_t force)
{
    uint32_t now = SystemTime_Millis();
    int i;
    int m_run;
    uint32_t m_rem;
    if (s_state != ESPLINK_ONLINE) return;
    if (MusicOta_Active()) return;   /* 音乐上传接收期间不推 DATA，避免抢 UART */
    if (!force && (int32_t)(now - s_last_push) < (int32_t)PUSH_MS) return;
    s_last_push = now;
    /* run �反映"正在烘干"（加�/烘干/暂停）；rem：运行中=剩余，空�=设定时长�
     * 保证非烘干状态下"剩余"�"烘干时长"显示�� */
    m_run = (g_sys.run_state == STATE_HEATING || g_sys.run_state == STATE_DRYING ||
             g_sys.run_state == STATE_PAUSED) ? 1 : 0;
    m_rem = m_run ? ((uint32_t)(g_sys.remaining_sec ? g_sys.remaining_sec : g_sys.params.dry_time_sec))
                  : (uint32_t)g_sys.params.dry_time_sec;
    push_dev("master", m_run,
             g_sys.current_temp, g_sys.current_humidity,
             (long)g_sys.weight_g, g_sys.ptc_temp,
             g_sys.params.dry_time_sec, m_rem, 1);
    for (i = 0; i < CAN_SLAVE_MAX; i++) {
        const CanSlave_t *sl = CAN_Cluster_GetSlave((uint8_t)i);
        char id[12];
        int s_run;
        uint32_t s_rem;
        if (!sl || !sl->online || sl->device_id == 0) continue;
        s_run = (sl->run_state == STATE_HEATING || sl->run_state == STATE_DRYING ||
                 sl->run_state == STATE_PAUSED) ? 1 : 0;
        s_rem = s_run ? ((uint32_t)(sl->remaining_sec ? sl->remaining_sec : sl->dry_time_sec))
                      : (uint32_t)sl->dry_time_sec;
        sprintf(id, "slave_%d", i + 1);
        push_dev(id, s_run,
                 (float)sl->air_temp_x10 / 10.0f, (float)sl->humidity_x10 / 10.0f,
                 (long)sl->weight_g, (float)sl->ptc_temp_x10 / 10.0f,
                 sl->dry_time_sec, s_rem, 0);
    }
}

/* ---------- 接收行�理 ---------- */
static void link_rx_line(char *line)
{
    size_t l = strlen(line);
    while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n')) line[--l] = 0;
    if (l == 0) return;

    if (line[0] == '+') {
        if (!strncmp(line, "+IP:", 4)) {
            strncpy(s_ip, line + 4, 15); s_ip[15] = 0;
            strncpy(g_sys.wifi_ip, s_ip, 15); g_sys.wifi_ip[15] = 0;
            g_sys.wifi_connected = 1; g_sys.wifi_ap_mode = 0;
            s_state = ESPLINK_ONLINE;
            EspLink_PushNow(1);
        } else if (!strcmp(line, "+AP")) {
            g_sys.wifi_connected = 0; g_sys.wifi_ap_mode = 1;
            s_ip[0] = 0;
            s_state = ESPLINK_CONFIG;
        } else if (!strcmp(line, "+DISC")) {
            g_sys.wifi_connected = 0; s_ip[0] = 0;
            if (s_state == ESPLINK_ONLINE) s_state = ESPLINK_CONNECTING;
        } else if (!strcmp(line, "+MUSICAP")) {
            /* 音乐上传 AP 已开�（AP，或已连 STA � APSTA� */
            g_sys.music_ota_active = 1;
            g_sys.music_popup = 2;
        } else if (!strcmp(line, "+MUSICCLOSED")) {
            g_sys.music_ota_active = 0;
            if (g_sys.music_popup == 2) g_sys.music_popup = 1;    /* 用户取消上传：回到待�� */
            /* 音乐会话结束：若 WiFi �关仍�且非 CONFIG 配网�，恢复自动连� */
            if (g_sys.wifi_enabled && s_state != ESPLINK_CONNECTING) {
                s_state = ESPLINK_CONNECTING;
                s_state_tick = SystemTime_Millis();
                link_send("AT+WEBSTART");
            }
        } else if (!strcmp(line, "+MUSICOK")) {
            g_sys.music_ota_active = 0;
            g_sys.music_popup = 0;    /* 完成后自动关弹窗 */
            g_sys.ui_force_redraw = 1;
        } else if (!strcmp(line, "+MUSICERR")) {
            g_sys.music_ota_active = 0;
            g_sys.music_popup = 5;    /* 失败 */
        }
    } else if (line[0] == '{') {
        web_cmd(line);
    } else if (strstr(line, "OK") && s_state == ESPLINK_BOOT) {
        if (s_pending_cfg || s_pending_clr) {
            if (s_pending_clr) { s_pending_clr = 0; link_send("AT+CFGCLR"); }
            s_pending_cfg = 0;
            link_send("AT+CFGAP");
        } else if (s_pending_music) {
            s_pending_music = 0;
            link_send("AT+MUSICAP");
            g_sys.music_popup = 2;
            g_sys.music_ota_active = 1;
            s_state = ESPLINK_CONFIG;   /* 音乐上传 AP 会话：不再自� WEBSTART */
            s_ip[0] = 0;
            g_sys.wifi_ap_mode = 1;
        } else {
            s_state = ESPLINK_CONNECTING;
            s_state_tick = SystemTime_Millis();
            link_send("AT+WEBSTART");
        }
    }
}

/* ---------- 对� ---------- */
void EspLink_Init(void)
{
    if (g_sys.wifi_enabled) {
        EspUart_Init();          /* �� USART1 已初始化（App �径不再走 EspAt_Init� */
        esp_power(1);
        s_pow_ms = 30;                     /* ~3s 供电稳定 */
        s_state_tick = SystemTime_Millis();
        s_state = ESPLINK_BOOT;
    }
}

void EspLink_Process(void)
{
    uint32_t now = SystemTime_Millis();
    uint8_t b;

    if (s_state == ESPLINK_OFF && !s_closing) return;

    if (s_pow_ms) {
        if ((uint32_t)(now - s_state_tick) < 100U) { return; }
        s_state_tick = now;
        if (--s_pow_ms) return;
        EspUart_ClearRx();
        EspUart_SetEnabled(1);
        s_state = ESPLINK_BOOT;
        s_state_tick = now;
        return;
    }

    if (s_state == ESPLINK_BOOT && (uint32_t)(now - s_state_tick) >= BOOT_POLL_MS) {
        s_state_tick = now;
        link_send("AT");
    }
    if (s_state == ESPLINK_CONNECTING && (uint32_t)(now - s_state_tick) >= START_TMO_MS) {
        s_state_tick = now;
        link_send("AT+WEBSTART");
    }

    if (s_closing && (uint32_t)(now - s_close_tick) >= 300U) {
        s_closing = 0;
        esp_power(0);
        EspUart_SetEnabled(0);
        s_state = ESPLINK_OFF;
        s_ip[0] = 0;
        s_li = 0;
        g_sys.wifi_connected = 0; g_sys.wifi_ap_mode = 0;
    }

    /* 接收溢出恢�：内部 Flash 擦写期间 CPU � stall，单字节 RX 会溢出�致
     * 行���。�测到溢出后丢弃当前残行并复位，避免卡在半� JSON 上� */
    if (EspUart_HasOverflow()) {
        EspUart_ClearRx();
        s_li = 0;
    }

    while (EspUart_ReadByte(&b) != 0) {
        /* 音乐 FSM 常驻�帧（空闲时只� 0xAA 帧头，不干扰行协�）；
         * �旦握手成功（收流�）则�占字节，行协�暂停� */
        MusicOta_FeedByte(b);
        if (MusicOta_Active()) continue;
        if (b == '\n') {
            if (s_li) { s_line[s_li] = 0; link_rx_line(s_line); s_li = 0; }
        } else if (s_li < LINK_BUF - 2) {
            s_line[s_li++] = (char)b;
        } else {
            s_li = 0;
        }
    }

    if (!s_closing) EspLink_PushNow(0);

    /* 音乐上传接收 FSM：需要时推进落盘与超时（� ACK 补发� */
    MusicOta_Poll();   /* 音乐 FSM 常驻：空闲扫�/收流推进，均无副作用 */
}

void EspLink_OnToggle(uint8_t on)
{
    if (on) {
        if (s_state == ESPLINK_OFF) {
            EspUart_Init();      /* App 首开 WiFi 时补� UART 初�化 */
            esp_power(1);
            s_pow_ms = 30;
            s_state_tick = SystemTime_Millis();
            s_state = ESPLINK_BOOT;
        } else if (s_state == ESPLINK_BOOT) {
            /* � OK 后自动发 */
        } else {
            s_state = ESPLINK_CONNECTING;
            s_state_tick = SystemTime_Millis();
            link_send("AT+WEBSTART");
        }
    } else if (s_state != ESPLINK_OFF) {
        link_send("AT+WEBCLOSE");
        s_closing = 1;
        s_close_tick = SystemTime_Millis();
    }
}

void EspLink_OpenConfig(void)
{
    if (s_state == ESPLINK_BOOT || s_state == ESPLINK_OFF) s_pending_cfg = 1;
    else { link_send("AT+CFGAP"); }
}

void EspLink_StartConfig(void)
{
    if (s_state == ESPLINK_BOOT || s_state == ESPLINK_OFF) { s_pending_clr = 1; }
    else { link_send("AT+CFGCLR"); link_send("AT+CFGAP"); }
}

/* 音乐上传 AP：ESP 决定 AP 或（已连 STA 时）APSTA */
void EspLink_MusicOpenAp(void)
{
    if (s_state == ESPLINK_OFF) {          /* ESP �上电：先供电，OK 后发 MUSICAP */
        esp_power(1);
        s_pow_ms = 30;
        s_state_tick = SystemTime_Millis();
        s_state = ESPLINK_BOOT;
        s_pending_music = 1;
        g_sys.music_popup = 2;
    } else if (s_state == ESPLINK_BOOT) {
        s_pending_music = 1;
        g_sys.music_popup = 2;
    } else {
        link_send("AT+MUSICAP");
        g_sys.music_popup = 2;
    }
}

void EspLink_MusicCloseAp(void)
{
    link_send("AT+MUSICCLOSE");
    s_pending_music = 0;
    g_sys.music_ota_active = 0;
}

/* 主机上���删改后，把�新��表推给网页（网� handleMsg 'PRESET_LIST' 实时刷新� */
void EspLink_NotifyPresetsChanged(void)
{
    send_preset_list();
}

EspLinkState_t EspLink_State(void) { return s_state; }
const char *EspLink_IP(void) { return s_ip; }

#endif /* BOOTLOADER_BUILD */
