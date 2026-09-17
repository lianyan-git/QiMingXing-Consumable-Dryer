/*
 * esp_link.c
 * App 杩愯屾椂 ESP01S(鑷瀹氫箟 AT 鍥轰欢) 閾捐矾灞傦紙琛屽崗璁锛夛細
 *   STM32鈫扙SP: "AT+WEBSTART" / "AT+CFGAP"(閰嶇綉) / "AT+CFGCLR"(娓5缁勫瓨鍙) /
 *              "AT+WEBCLOSE"(鍏跺悗 STM32 鏂鐢) / 浠 '{' 寮澶寸殑 JSON 琛(杞鍙戠粰娴忚堝櫒)
 *   ESP鈫扴TM32: "OK" / "+IP:x.x.x.x"(STA 宸茶繛) / "+AP"(杩涘叆閰嶇綉AP) / "+DISC"(鎺夌嚎) /
 *              浠 '{' 寮澶寸殑 JSON 琛(娴忚堝櫒鍛戒护鍘熸枃)
 * JSON 鍛戒护瀵瑰簲 web.txt 鍗忚锛欻ELLO/PRESET_GET|SAVE|DELETE|APPLY/PARAM_SET/RUN/
 *   GLOBAL/CAN_MODE锛汼TM32 鍥 ACK锛屽苟 1Hz 鎺ㄩ master+鍦ㄧ嚎浠庢満 DATA 琛屻佹寜闇鍥 PRESET_LIST銆
 */
#ifndef BOOTLOADER_BUILD

#include "system_config.h"
#include "system_time.h"
#include "bsp_esp_uart.h"
#include "bsp_rgb_led.h"
#include "pin_config.h"
#include "can_cluster.h"
#include "esp_link.h"
#include "music_ota.h"
#include "lang_ota.h"
#include "music_store.h"
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
static uint8_t  s_pow_ms;              /* 涓婄數绋冲畾鍊掕℃椂(脳100ms) */
static uint32_t s_state_tick = 0;
static uint32_t s_last_push = 0;
static char     s_ip[16] = "";
static char     s_line[LINK_BUF];
static uint16_t s_li = 0;
static uint8_t  s_pending_cfg = 0;     /* OK 鍓嶆敹鍒扮殑閰嶇綉璇锋眰 */
static uint8_t  s_pending_clr = 0;     /* OK 鍓嶆敹鍒扮殑閲嶆柊閰嶇綉璇锋眰 */
static uint8_t  s_pending_music = 0;   /* OK 鍓嶆敹鍒扮殑闊充箰涓婁紶 AP 璇锋眰 */
static uint8_t  s_pending_lang = 0;
static uint8_t  s_closing = 0;
static uint8_t  s_pow_on = 0;
static uint32_t s_boot_t0 = 0;      /* BOOT 状态首次进入时刻(超时判定用), 0=尚未计时 */
static uint32_t s_close_tick = 0;
static char     s_out[896];            /* JSON 缁勫寘澶嶇敤缂撳啿锛12 棰勮炬渶闀 ~850B锛 */

/* push-if-changed: keep last pushed signature per device, only send DATA when changed */
typedef struct {
    int32_t a, b, c, d, e, f, g, h, i, j;
} PushSig_t;
static PushSig_t s_sig[CAN_SLAVE_MAX + 1];
static uint8_t   s_sig_valid = 0;

static void link_send(const char *s)
{
    EspUart_Write((const uint8_t *)s, (uint16_t)strlen(s), 500);
    EspUart_Write((const uint8_t *)"\r\n", 2, 100);
}

static void esp_power(uint8_t on)
{
    if (on) GPIO_ResetBits(PIN_ESP_EN_PORT, PIN_ESP_EN_PIN);   /* P-MOS锛氫綆=渚涚數 */
    else    GPIO_SetBits(PIN_ESP_EN_PORT, PIN_ESP_EN_PIN);
}

/* ---------- 灏 JSON 瑙ｆ瀽锛堟牸寮忕敱鏈绔浠ｇ爜鐢熸垚/绾﹀畾锛岄敭鍚嶄笉鍚鐗规畩瀛楃︼級 ---------- */
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

static int jbool(const char *s, const char *key)    /* true/1 瑙嗕负鐪 */
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

/* 瀹氫綅娑堟伅浣 "d": 涔嬪悗鐨勪綅缃銆傜綉椤电 JSON 涓 {"id":"RUN_169..","t":"RUN","d":{...}}锛
 * 淇″皝灞備篃鏈 "id" 閿鈥斺旂洿鎺 jstr(line,"id") 浼氬彇鍒颁俊灏 id 鑰岄潪 d.id锛
 * 瀵艰嚧 RUN/PARAM_SET 鐨勭洰鏍囪惧囨亽涓 "RUN_169.."锛坢aster 鍒嗘敮姘镐笉鍛戒腑锛屽惎鍋滅儤骞叉棤鏁堬級銆
 * d 鍐呯殑閿涓寰嬩粠 droot(line) 璧疯В鏋愩 */
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

/* ---------- 棰勮撅紙涓昏惧囧栭儴 flash锛孲ystem_Save 钀界洏锛 ---------- */
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

/* PRESET_DELETE {remain:[{name,temp,h,m,s}...]} 鈫 浠 remain 閲嶅缓鍒楄〃 */
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

/* 浠庢満 h/m/s 鍒嗛噺淇鏀癸細浠ラ泦缇ゅ唴宸叉敹鍒扮殑 dry_time_sec 涓哄簳閲嶇畻 */
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

/* PARAM_SET 搴旂敤鍒版寚瀹氳惧 */
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

/* ---------- PRESET_LIST 鎺ㄩ ---------- */
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

/* ---------- 鍛戒护鍒嗗彂 ---------- */
static void send_devs(void)
{
    int i;
    uint16_t len = 0;
    /* 涓绘満 + 鍦ㄧ嚎浠庢満鍒楄〃锛氬悕绉=璁惧+搴忓垪鍙凤紝搴忓垪鍙峰嵆 About 椤靛簭鍒楀彿(device_id) */
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
        g_sys.ui_force_redraw = 1;    /* 涓绘満鐣岄潰棰勮/鏃堕棿绔嬪嵆鍒锋柊锛屼笉蹇呴鍑洪〉闈㈠啀杩 */
        EspLink_PushNow(1);
    }
    else if (!strcmp(t, "PARAM_SET")) {
        char id[12] = "", p[8] = "";
        jstr(d, "id", id, sizeof(id));
        jstr(d, "param", p, sizeof(p));
        apply_target(id, p, jnum(d, "value", -1));
        EspLink_PushNow(1);           /* 鍙傛暟鍙樺姩绔嬪嵆鍥炴帹鍚屾 */
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
        g_sys.ui_force_redraw = 1;    /* CAN 璁剧疆椤靛紑鍏崇姸鎬佺珛鍗虫樉绀 */
        EspLink_PushNow(1);
    }
}

/* ---------- DATA 鎺ㄩ ---------- */
/* item11: "temp":NN.N 形式的两位定点小数直写(负值含 '-'; 0.05 进位语义对齐 %f)
   注意: 本工程推送温度/湿度均为 °C/% 非负, %f 的 "-0.0" 分支不可达, 故不特判负零 */
static int fmt_num(char *s, int t)
{
    char *p = s;
    if (t < 0) { *p++ = '-'; t = -t; }
    {
        int w = t / 10, f = t % 10;
        if (w >= 100) *p++ = (char)('0' + (w / 100) % 10);
        if (w >= 10)  *p++ = (char)('0' + (w / 10) % 10);
        *p++ = (char)('0' + w % 10);
        *p++ = '.';
        *p++ = (char)('0' + f);
    }
    return (int)(p - s);
}

static void push_dev(const char *id, int run, float temp, float humi,
                     long wt, float ptc, uint32_t sec, uint32_t rem, int has_target)
{
    long st = (long)g_sys.params.target_temp;
    long sp = (long)g_sys.params.ptc_max_temp;
    /* item11: %.1f → fmt_num 定点直写(JSON 输出字节一致; 来源已是十分位值) */
    char tf[16], hf[16], pf[16];
    tf[fmt_num(tf, (int)(temp * 10.0f + (temp >= 0.0f ? 0.5f : -0.5f)))] = 0;
    hf[fmt_num(hf, (int)(humi * 10.0f + (humi >= 0.0f ? 0.5f : -0.5f)))] = 0;
    pf[fmt_num(pf, (int)(ptc * 10.0f + (ptc >= 0.0f ? 0.5f : -0.5f)))] = 0;
    if (has_target) {
        sprintf(s_out,
            "{\"t\":\"DATA\",\"d\":{\"id\":\"%s\",\"run\":%s,\"can\":%s,\"setTemp\":%ld,\"setPtc\":%ld,\"temp\":%s,\"humi\":%s,\"wtG\":%ld,\"ptc\":%s,\"tH\":%lu,\"tM\":%lu,\"tS\":%lu,\"rem\":%lu}}",
            id, run ? "true" : "false", (g_sys.params.can_enabled ? "true" : "false"), st, sp, tf, hf, wt, pf,
            (unsigned long)(sec / 3600U), (unsigned long)((sec % 3600U) / 60U), (unsigned long)(sec % 60U),
            (unsigned long)rem);
    } else {
        sprintf(s_out,
            "{\"t\":\"DATA\",\"d\":{\"id\":\"%s\",\"run\":%s,\"temp\":%s,\"humi\":%s,\"wtG\":%ld,\"ptc\":%s,\"tH\":%lu,\"tM\":%lu,\"tS\":%lu,\"rem\":%lu}}",
            id, run ? "true" : "false", tf, hf, wt, pf,
            (unsigned long)(sec / 3600U), (unsigned long)((sec % 3600U) / 60U), (unsigned long)(sec % 60U),
            (unsigned long)rem);
    }
    link_send(s_out);
}
static void push_if_changed(uint8_t idx, const char *id, int8_t run,
                            int16_t temp, int16_t humi, int16_t wt, int16_t ptc,
                            int32_t sec, int32_t rem, uint8_t has_target)
{
    PushSig_t sg;
    sg.a = temp; sg.b = humi; sg.c = wt; sg.d = ptc;
    sg.e = sec;  sg.f = rem;  sg.g = run;
    sg.h = has_target ? (int16_t)g_sys.params.target_temp : 0;
    sg.i = has_target ? (int16_t)g_sys.params.ptc_max_temp : 0;
    sg.j = has_target ? (int16_t)g_sys.params.can_enabled : 0;
    if (s_sig_valid && memcmp(&s_sig[idx], &sg, sizeof(sg)) == 0) return;
    s_sig[idx] = sg;
    push_dev(id, run, (float)temp / 10.0f, (float)humi / 10.0f,
             wt, (float)ptc / 10.0f, sec, rem, has_target);
}


void EspLink_PushNow(uint8_t force)
{
    uint32_t now = SystemTime_Millis();
    int i;
    int m_run;
    uint32_t m_rem;
    if (s_state != ESPLINK_ONLINE) return;
    if (MusicOta_Active()) return;
    if (!force && (int32_t)(now - s_last_push) < (int32_t)PUSH_MS) return;
    s_last_push = now;
    /* push only when a field changed (no continuous beacon when idle).
       rem changes every second while running -> pushed once per second then. */
    m_run = (g_sys.run_state == STATE_HEATING || g_sys.run_state == STATE_DRYING ||
             g_sys.run_state == STATE_PAUSED) ? 1 : 0;
    m_rem = m_run ? ((uint32_t)(g_sys.remaining_sec ? g_sys.remaining_sec : g_sys.params.dry_time_sec))
                  : (uint32_t)g_sys.params.dry_time_sec;
    push_if_changed(0, "master", (int8_t)m_run,
                    (int16_t)(g_sys.current_temp * 10.0f),
                    (int16_t)(g_sys.current_humidity * 10.0f),
                    (int16_t)g_sys.weight_g,
                    (int16_t)(g_sys.ptc_temp * 10.0f),
                    (int32_t)g_sys.params.dry_time_sec, (int32_t)m_rem, 1);
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
        push_if_changed((uint8_t)(i + 1), id, (int8_t)s_run,
                        (int16_t)sl->air_temp_x10, (int16_t)sl->humidity_x10,
                        (int16_t)sl->weight_g, (int16_t)sl->ptc_temp_x10,
                        (int32_t)sl->dry_time_sec, (int32_t)s_rem, 0);
    }
}

/* ---------- 鎺ユ敹琛屽勭悊 ---------- */
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
            s_sig_valid = 0;          /* new session: push full on first tick */
            EspLink_PushNow(1);
        } else if (!strcmp(line, "+AP")) {
            g_sys.wifi_connected = 0; g_sys.wifi_ap_mode = 1;
            s_ip[0] = 0;
            s_state = ESPLINK_CONFIG;
        } else if (!strcmp(line, "+DISC")) {
            g_sys.wifi_connected = 0; s_ip[0] = 0;
            if (s_state == ESPLINK_ONLINE) s_state = ESPLINK_CONNECTING;
                } else if (!strcmp(line, "+MUSICAP")) {
            /* 会话建立: 先复位语言/音乐 FSM, 防止上次会话残留 s_st 吞掉本次握手字节 */
            LangOta_Abort();
            MusicOta_Init();
            g_sys.music_ota_active = 1;
            g_sys.music_popup = 2;
            g_sys.wifi_ap_mode = 1;    /* AP 确认已开: 弹窗单击据此区分“关闭”与“重试” */
            /* 音乐上传: MUSICAP 已开, 握手交由 music_ota 立即 ACK, 擦除由 Poll 按需推进 */
} else if (!strcmp(line, "+MUSICCLOSED")) {
            MusicOta_Abort();   /* 清除残留接收态, 保证行协议/Push 恢复 */
            g_sys.music_ota_active = 0;
            g_sys.wifi_ap_mode = 0;
            if (g_sys.music_popup == 2) g_sys.music_popup = 1;    /* 鐢ㄦ埛鍙栨秷涓婁紶锛氬洖鍒板緟寮鍚 */
            /* 闊充箰浼氳瘽缁撴潫锛氳嫢 WiFi 寮鍏充粛寮涓旈潪 CONFIG 閰嶇綉涓锛屾仮澶嶈嚜鍔ㄨ繛鎺 */
            if (g_sys.wifi_enabled && s_state != ESPLINK_CONNECTING) {
                s_state = ESPLINK_CONNECTING;
                s_state_tick = SystemTime_Millis();
                link_send("AT+WEBSTART");
            }
        } else if (!strcmp(line, "+MUSICOK")) {
            MusicOta_Abort();
            g_sys.music_ota_active = 0;
            g_sys.wifi_ap_mode = 0;
            g_sys.music_popup = 4;    /* 完成态：帧循环 1.8s 后自动收起 */    /* 瀹屾垚鍚庤嚜鍔ㄥ叧寮圭獥 */
            g_sys.ui_force_redraw = 1;
        } else if (!strcmp(line, "+LANGAP")) {
            /* 字库 AP 确认: 严格按 ESP01S 返回 +LANGAP 才认为已开, 才显示弹窗/热点 */
            LangOta_Init();
            g_sys.lang_ap_active = 1;
            g_sys.lang_popup = 2;
            g_sys.wifi_ap_mode = 1;
            g_sys.lang_upload_pct = 0;
        } else if (!strcmp(line, "+MUSICERR")) {
            MusicOta_Abort();
            g_sys.music_ota_active = 0;
            g_sys.wifi_ap_mode = 0;
            g_sys.music_popup = 5;    /* 澶辫触 */
        }
    } else if (line[0] == '{') {
        web_cmd(line);
    } else if (strstr(line, "OK") && s_state == ESPLINK_BOOT) {
        if (s_pending_cfg || s_pending_clr) {
            if (s_pending_clr) { s_pending_clr = 0; link_send("AT+CFGCLR"); }
            s_pending_cfg = 0;
            link_send("AT+CFGAP");
        } else if (s_pending_music || s_pending_lang) {
            uint8_t is_lang = s_pending_lang;
            s_pending_music = 0;
            s_pending_lang = 0;
            link_send(is_lang ? "AT+LANGAP" : "AT+MUSICAP");
            /* 弹窗/active 严格等 ESP 确认行(+MUSICAP/+LANGAP)再置位, 不提前显示热点 */
            s_state = ESPLINK_CONFIG;   /* 闊充箰涓婁紶 AP 浼氳瘽锛氫笉鍐嶈嚜鍔 WEBSTART */
            s_ip[0] = 0;
            g_sys.wifi_ap_mode = 1;     /* 已请求 AP: 阻止自动恢复连接; 具体确认由 +XXXAP 行完成 */
        } else {
            s_state = ESPLINK_CONNECTING;
            s_state_tick = SystemTime_Millis();
            link_send("AT+WEBSTART");
        }
    }
}

/* ---------- 瀵瑰 ---------- */
void EspLink_Init(void)
{
    if (g_sys.wifi_enabled) {
        EspUart_Init();          /* 纭淇 USART1 宸插垵濮嬪寲锛圓pp 璺寰勪笉鍐嶈蛋 EspAt_Init锛 */
        esp_power(1);
        s_pow_ms = 30; s_pow_on = 0;                     /* ~3s 渚涚數绋冲畾 */
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
        if (s_pow_on) {   /* 断电 1s 稳定完成: 上电, 再进 3s 上电稳定倒计时 */
            esp_power(1);
            s_pow_on = 0;
            s_pow_ms = 30;
            s_state_tick = now;
            return;
        }
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
    /* BOOT 超时: 一直等不到 OK 说明 ESP01S 残留/掉线(上一会话失败), 强制断电复位避免 AP 永远打不开。
     * 时间戳方案: 进入 BOOT 首次计时, 8s 无前进则断电重上电重新 BOOT。无 static 边沿残留。 */
    if (s_state == ESPLINK_BOOT) {
        if (s_boot_t0 == 0) s_boot_t0 = now;                 /* 首次进入 BOOT: 开始计时 */
        else if ((uint32_t)(now - s_boot_t0) > 8000U) {      /* ~8s 无 OK */
            s_boot_t0 = 0;                                   /* 重新计时 (断电重上电后再进 BOOT 重新计) */
            s_pow_on = 1;
            s_pow_ms = 10;           /* 断电 1s → 上电 3s → 重新 BOOT */
            s_state_tick = now;
            s_closing = 0;
            s_li = 0;
        }
    } else {
        s_boot_t0 = 0;   /* 离开 BOOT: 清计时 */
    }
    if (s_state == ESPLINK_CONNECTING && (uint32_t)(now - s_state_tick) >= START_TMO_MS) {
        s_state_tick = now;
        link_send("AT+WEBSTART");
    }

    /* 音乐会话结束(成功/失败/超时/取消)后若有 WiFi 开关且非配网, 恢复自动连接
     * 否则 s_state 停在 ESPLINK_CONFIG, PushNow 的 ONLINE 判断让网页永不推送 */
    if (s_state == ESPLINK_CONFIG && !g_sys.music_ota_active && !s_pending_music
        && !g_sys.wifi_ap_mode && g_sys.wifi_enabled
        && (uint32_t)(now - s_state_tick) >= 1000U) {
        s_state = ESPLINK_CONNECTING;
        s_state_tick = now;
        link_send("AT+WEBSTART");
        s_sig_valid = 0;
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

    /* 鎺ユ敹婧㈠嚭鎭㈠嶏細鍐呴儴 Flash 鎿﹀啓鏈熼棿 CPU 琚 stall锛屽崟瀛楄妭 RX 浼氭孩鍑哄艰嚧
     * 琛岃鎴鏂銆傛娴嬪埌婧㈠嚭鍚庝涪寮冨綋鍓嶆畫琛屽苟澶嶄綅锛岄伩鍏嶅崱鍦ㄥ崐涓 JSON 涓娿 */
    if (EspUart_HasOverflow()) {
        /* 上传中溢出=必有字节丢失: 整包 CRC 后补 NAK 只会让两边状态机半失步,
         * 直接放弃本次传输并弹失败帧, 由用户重试(ESP 侧等不到 ACK 也会自行终止)。 */
        if (MusicOta_Active()) { MusicOta_Abort(); g_sys.music_popup = 5; }
        if (LangOta_Active())  { LangOta_Abort();  g_sys.lang_popup  = 5; }
        EspUart_ClearRx();
        s_li = 0;
    }

    while (EspUart_ReadByte(&b) != 0) {
        /* 璇█/闊充箰 FSM 杞緱鍠傝妭: 宸叉縺娲荤嫭鍗犲瓧鑺?绌洪棽鏃跺弻鍠傝瘑鍒 0x13/0x11 鎻℃墜,
         * 涓嶅共鎵拌涓崗璁 */
        if (LangOta_Active()) { LangOta_FeedByte(b); continue; }
        if (MusicOta_Active()) { MusicOta_FeedByte(b); continue; }
        LangOta_FeedByte(b);
        if (LangOta_Active()) continue;
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

    /* 闊充箰涓婁紶鎺ユ敹 FSM锛氶渶瑕佹椂鎺ㄨ繘钀界洏涓庤秴鏃讹紙鍚 ACK 琛ュ彂锛 */
    MusicOta_Poll();
    LangOta_Poll();   /* 闊充箰 FSM 甯搁┗锛氱┖闂叉壂甯/鏀舵祦鎺ㄨ繘锛屽潎鏃犲壇浣滅敤 */
}

void EspLink_OnToggle(uint8_t on)
{
    if (on) {
        if (s_state == ESPLINK_OFF) {
            EspUart_Init();      /* App 棣栧紑 WiFi 鏃惰ˉ榻 UART 鍒濆嬪寲 */
            esp_power(1);
            s_pow_ms = 30;
            s_state_tick = SystemTime_Millis();
            s_state = ESPLINK_BOOT;
        } else if (s_state == ESPLINK_BOOT) {
            /* 绛 OK 鍚庤嚜鍔ㄥ彂 */
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

/* 音乐上传 AP：强制冷启动 —— 先断电 1s 再上电, 3s 稳定后 BOOT→OK→MUSICAP。
 * 避免 ESP 挂死/停留在其它模式时直接发 AT+MUSICAP 无响应、热点起不来。 */
void EspLink_MusicOpenAp(void)
{
    MusicOta_Abort();   /* 清理上一次可能的残留接收态, 保证行协议恢复 */
    RGB_AllOff();       /* 上传前主动熄灯: 此刻 UART 无数据流(冷启动 ESP 已断电/在线等 AT 应答),
                         * 关中断的灯带帧安全; 之后 update_rgb 在会话期间本就冻结 */
    if (s_state == ESPLINK_ONLINE) {
        /* WiFi 已在线: 直接发 MUSICAP 切 APSTA(AP+STA 共存), 不断 WiFi */
        s_pending_music = 0;
        link_send("AT+MUSICAP");
        return;   /* popup 保持待开启: 等 ESP 回 +MUSICAP 确认后才切“连接热点”弹窗 */
    }
    EspUart_Init();          /* ensure UART initialized even if WiFi was never toggled on this power cycle */
    esp_power(0);
    s_closing = 0;
    s_pending_cfg = 0;
    s_pending_clr = 0;
    s_pending_music = 1;
    s_pow_on = 1;
    s_pow_ms = 10;           /* 断电 1s, 然后自动上电 +3s 稳定 */
    s_state_tick = SystemTime_Millis();
    s_state = ESPLINK_BOOT;
    s_ip[0] = 0;
    g_sys.music_ota_active = 0;
}

/* 语言字库上传 AP：与音乐同模式(在线直接发/离线冷启动) */
void EspLink_LangOpenAp(void)
{
    MusicOta_Abort();
    LangOta_Abort();
    RGB_AllOff();       /* 同音乐: 开 AP 前趁 UART 静默窗口熄灭灯带 */
    g_sys.lang_download_done = 0;
    if (s_state == ESPLINK_ONLINE) {
        s_pending_lang = 0;
        link_send("AT+LANGAP");
        return;   /* 等 ESP 回 +LANGAP 确认后才置 lang_ap_active + 弹窗 */
    }
    EspUart_Init();
    esp_power(0);
    s_closing = 0;
    s_pending_cfg = 0;
    s_pending_clr = 0;
    s_pending_lang = 1;
    s_pow_on = 1;
    s_pow_ms = 10;
    s_state_tick = SystemTime_Millis();
    s_state = ESPLINK_BOOT;
    s_ip[0] = 0;
    g_sys.music_ota_active = 0;
}

void EspLink_MusicCloseAp(void)
{
    link_send("AT+MUSICCLOSE");
    s_pending_music = 0;
    g_sys.music_ota_active = 0;
}

void EspLink_LangCloseAp(void)
{
    LangOta_Abort();
    link_send("AT+LANGCLOSE");
    s_pending_lang = 0;
    g_sys.lang_ap_active = 0;
    if (g_sys.lang_popup == 2) g_sys.lang_popup = 1;   /* 回到“待开启”, 原则同 +MUSICCLOSED */
    g_sys.wifi_ap_mode = 0;
}

/* 涓绘満涓婇勮惧炲垹鏀瑰悗锛屾妸鏈鏂伴勮捐〃鎺ㄧ粰缃戦〉锛堢綉椤 handleMsg 'PRESET_LIST' 瀹炴椂鍒锋柊锛 */
void EspLink_NotifyPresetsChanged(void)
{
    send_preset_list();
}

EspLinkState_t EspLink_State(void) { return s_state; }
const char *EspLink_IP(void) { return s_ip; }

#endif /* BOOTLOADER_BUILD */
