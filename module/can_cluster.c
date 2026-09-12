/*
 * can_cluster.c
 * CAN 集群协议：
 *   ID 0x100 主机搜索广播: [0]=0xA5 [1..4]=主机序列号(LE)
 *   ID 0x101 从机应答加入: [0]=0x5A [1..4]=从机序列号(LE)
 *   ID 0x102 主机加入确认: [0]=0xA6 [1..4]=从机序列号(LE) [5]=槽位(0-15)
 *   ID 0x180|槽位 从机数据帧: 类型字节 + 字段
 *     type 0x01: 空气温度x10(int16) 湿度x10(int16) 重量g(int16)
 *     type 0x02: PTC温度x10(int16) 烘干时间(uint32)
 *     type 0x03: 剩余时间(uint32) 运行状态(uint8)
 * 主机每 2s 广播搜索维持网络；从机入网即刻关 WiFi，入网后每 500ms 推送数据；
 * 主机 3s 收不到某从机数据即剔除。
 */
#ifndef BOOTLOADER_BUILD

#include "system_config.h"
#include "system_time.h"
#include "bsp_can.h"
#include "mod_wifi_manager.h"
#include "can_cluster.h"
#include <string.h>

extern void StartDrying(void);
extern void StopDrying(void);

#define CAN_ID_DISCOVERY   0x100U
#define CAN_ID_JOIN        0x101U
#define CAN_ID_ACK         0x102U
#define CAN_DATA_ID_BASE   0x180U
#define CAN_BAUD           500U

#define CAN_MAGIC_DISCOVERY 0xA5U
#define CAN_MAGIC_JOIN      0x5AU
#define CAN_MAGIC_ACK       0xA6U

#define CAN_DATA_AIR        0x01U
#define CAN_DATA_PTC        0x02U
#define CAN_DATA_TIME       0x03U
#define CAN_DATA_PRESET     0x04U   /* 入网后首帧：预设列表摘要（count/current） */

/* 主机→从机 控制帧：ID=0x400|槽位，data[0]=命令，data[1..4]=int32 参数(LE)
 * slot=0xFF ⇒ 广播给全部从机(ID 0x4FF)。 */
#define CAN_ID_CTRL_BASE    0x400U
#define CAN_ID_CTRL_ALL     0x4FFU
#define CAN_CTRL_RUN        1U      /* v=1 开始烘干 / 0 停止 */
#define CAN_CTRL_SET_TEMP   2U      /* v=整数℃ */
#define CAN_CTRL_SET_TIME   3U      /* v=秒 */
#define CAN_CTRL_SET_PTC    4U      /* v=整数℃ */

static void put_i32(uint8_t *d, int32_t v)
{
    uint32_t u = (uint32_t)v;
    d[0] = (uint8_t)(u & 0xFF);
    d[1] = (uint8_t)((u >> 8) & 0xFF);
    d[2] = (uint8_t)((u >> 16) & 0xFF);
    d[3] = (uint8_t)((u >> 24) & 0xFF);
}

#define CAN_HEARTBEAT_MS    2000U    /* 主机搜索广播周期 */
#define CAN_DATA_PUSH_MS    500U     /* 从机数据推送周期 */
#define CAN_OFFLINE_MS      3000U    /* 主机剔除离线从机 / 从机失去主机 */
#define CAN_PROCESS_BATCH   8U       /* 单次最多收帧数 */

static CanSlave_t s_slaves[CAN_SLAVE_MAX];
static uint32_t s_last_search_ms = 0;   /* 主机：上次广播时间 */
static uint32_t s_last_push_ms = 0;     /* 从机：上次推送时间 */
static uint32_t s_last_disc_ms = 0;     /* 从机：上次收到主机广播时间 */
static uint8_t  s_slot = 0;             /* 从机：主机分配的槽位 */
static uint8_t  s_wifi_off_done = 0;    /* 从机：WiFi 已关（只关一次） */
static uint8_t  s_hw_ready = 0;         /* 硬件已初始化 */
static uint8_t  s_last_role = 0xFF;     /* 角色变化检测 */
static uint32_t s_master_id = 0;        /* 从机：当前主机序列号 */

static void cluster_start_hw(void)
{
    BspCan_Init(CAN_BAUD);
    s_hw_ready = (uint8_t)BspCan_IsReady();
    if (!s_hw_ready) return;
    s_last_search_ms = SystemTime_Millis();
    s_last_push_ms = SystemTime_Millis();
}

static void cluster_stop_hw(void)
{
    s_hw_ready = 0;
    g_sys.can_joined = 0;
    s_slot = 0;
    s_wifi_off_done = 0;
    s_master_id = 0;
    s_last_disc_ms = 0;
    BspCan_DeInit();
}

static void put_u32(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t)(v & 0xFF);
    d[1] = (uint8_t)((v >> 8) & 0xFF);
    d[2] = (uint8_t)((v >> 16) & 0xFF);
    d[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t get_u32(const uint8_t *d)
{
    return (uint32_t)d[0] | ((uint32_t)d[1] << 8) |
           ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}

static void put_i16(uint8_t *d, int16_t v)
{
    d[0] = (uint8_t)(v & 0xFF);
    d[1] = (uint8_t)((v >> 8) & 0xFF);
}

static int16_t get_i16(const uint8_t *d)
{
    return (int16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));
}

static CanSlave_t *find_slave(uint32_t id)
{
    uint8_t i;
    for (i = 0; i < CAN_SLAVE_MAX; i++) {
        if (s_slaves[i].device_id == id) return &s_slaves[i];
    }
    return 0;
}

/* 主机：收到从机加入应答 → 登记/刷新 + 回 ACK */
static void on_join(const uint8_t *data)
{
    uint32_t dev = get_u32(&data[1]);
    CanSlave_t *sl = find_slave(dev);
    uint8_t slot;
    uint8_t out[8];

    if (dev == 0) return;
    if (!sl) {
        for (slot = 0; slot < CAN_SLAVE_MAX; slot++) {
            if (s_slaves[slot].device_id == 0) break;
        }
        if (slot >= CAN_SLAVE_MAX) return;   /* 表满 */
        sl = &s_slaves[slot];
        sl->device_id = dev;
    }
    sl->online = 1;
    sl->last_rx_ms = SystemTime_Millis();

    /* ACK：告知槽位 */
    out[0] = CAN_MAGIC_ACK;
    put_u32(&out[1], dev);
    out[5] = (uint8_t)(sl - s_slaves);
    out[6] = 0;
    out[7] = 0;
    BspCan_Send(CAN_ID_ACK, out, 8);
}

/* 主机：收到从机数据帧 → 刷新在线与数值 */
static void on_data(uint32_t id, const uint8_t *data)
{
    uint8_t slot = (uint8_t)(id - CAN_DATA_ID_BASE);
    CanSlave_t *sl;

    if (slot >= CAN_SLAVE_MAX) return;
    sl = &s_slaves[slot];
    if (sl->device_id == 0) return;

    sl->online = 1;
    sl->last_rx_ms = SystemTime_Millis();
    switch (data[0]) {
    case CAN_DATA_AIR:
        sl->air_temp_x10 = get_i16(&data[1]);
        sl->humidity_x10 = get_i16(&data[3]);
        sl->weight_g = get_i16(&data[5]);
        break;
    case CAN_DATA_PTC:
        sl->ptc_temp_x10 = get_i16(&data[1]);
        sl->dry_time_sec = get_u32(&data[3]);
        break;
    case CAN_DATA_TIME:
        sl->remaining_sec = get_u32(&data[1]);
        sl->run_state = data[5];
        break;
    default:
        break;
    }
}

/* 从机：收到主机搜索 → 记录主机 + 应答加入（未入网时） */
static void on_discovery(const uint8_t *data)
{
    uint8_t out[8];
    uint32_t dev = System_GetDeviceId();

    if (g_sys.params.can_role != 1) return;   /* 仅从机应答 */
    s_master_id = get_u32(&data[1]);
    s_last_disc_ms = SystemTime_Millis();
    if (g_sys.can_joined) return;
    out[0] = CAN_MAGIC_JOIN;
    put_u32(&out[1], dev);
    out[5] = 0; out[6] = 0; out[7] = 0;
    BspCan_Send(CAN_ID_JOIN, out, 8);
}

/* 从机：收到主机 ACK（确认自己） → 入网 + 关 WiFi（只一次） */
static void on_ack(const uint8_t *data)
{
    uint32_t dev = get_u32(&data[1]);
    if (dev != System_GetDeviceId()) return;
    g_sys.can_joined = 1;
    s_slot = data[5];
    if (!s_wifi_off_done) {
        s_wifi_off_done = 1;
        WiFiManager_Stop();          /* 被连接进主设备网络那一刻立即关闭 WiFi */
    }
}

/* 从机：执行主机控制帧（控制权已交网页端） */
static void on_ctrl(const uint8_t *data, uint8_t len)
{
    int32_t v;
    if (len < 5) return;
    v = (int32_t)get_u32(&data[1]);
    switch (data[0]) {
    case CAN_CTRL_RUN:
        if (v) StartDrying(); else StopDrying();
        break;
    case CAN_CTRL_SET_TEMP:
        if (v >= TEMP_MIN && v <= TEMP_MAX) { g_sys.params.target_temp = (uint8_t)v; }
        break;
    case CAN_CTRL_SET_TIME:
        if (v >= 0 && v <= (int32_t)86400L) { g_sys.params.dry_time_sec = (uint32_t)v; }
        break;
    case CAN_CTRL_SET_PTC:
        if (v >= PTC_TEMP_MIN && v <= PTC_TEMP_MAX) { g_sys.params.ptc_max_temp = (uint8_t)v; }
        break;
    default: break;
    }
    System_RequestSave();
}

static void rx_dispatch(void)
{
    uint8_t batch;
    for (batch = 0; batch < CAN_PROCESS_BATCH; batch++) {
        uint32_t id = 0;
        uint8_t data[8] = {0};
        uint8_t len = 0;
        if (!BspCan_Recv(&id, data, &len)) break;
        if (id == CAN_ID_DISCOVERY) on_discovery(data);
        else if (id == CAN_ID_JOIN && g_sys.params.can_role == 0) on_join(data);
        else if (id == CAN_ID_ACK) on_ack(data);
        else if (id >= CAN_DATA_ID_BASE && id <= (uint32_t)(CAN_DATA_ID_BASE + CAN_SLAVE_MAX - 1U)
                 && g_sys.params.can_role == 0) on_data(id, data);
        else if (g_sys.params.can_role == 1 && id >= CAN_ID_CTRL_BASE && id <= (uint32_t)(CAN_ID_CTRL_BASE + CAN_SLAVE_MAX - 1U)
                 && (uint8_t)(id - CAN_ID_CTRL_BASE) == s_slot) on_ctrl(data, len);
        else if (g_sys.params.can_role == 1 && id == CAN_ID_CTRL_ALL) on_ctrl(data, len);
    }
}

/* 主机：周期广播搜索 + 超时剔除 */
static void master_task(void)
{
    uint32_t now = SystemTime_Millis();
    uint8_t i;
    uint8_t out[8];

    if ((int32_t)(now - s_last_search_ms) >= (int32_t)CAN_HEARTBEAT_MS) {
        s_last_search_ms = now;
        out[0] = CAN_MAGIC_DISCOVERY;
        put_u32(&out[1], System_GetDeviceId());
        out[5] = 0; out[6] = 0; out[7] = 0;
        BspCan_Send(CAN_ID_DISCOVERY, out, 8);
    }
    for (i = 0; i < CAN_SLAVE_MAX; i++) {
        if (s_slaves[i].device_id != 0 && s_slaves[i].online &&
            (int32_t)(now - s_slaves[i].last_rx_ms) > (int32_t)CAN_OFFLINE_MS) {
            s_slaves[i].online = 0;
        }
    }
    /* 已连接数同步到 UI */
    {
        uint8_t cnt = 0;
        for (i = 0; i < CAN_SLAVE_MAX; i++) {
            if (s_slaves[i].online) cnt++;
        }
        g_sys.can_connected = cnt;
    }
}

/* 从机：周期推送网页所需数据 */
static void slave_task(void)
{
    uint32_t now = SystemTime_Millis();
    uint8_t out[8];

    if (!g_sys.can_joined) return;
    /* 主机失联超时：停止推送，等待下次重新入网 */
    if ((int32_t)(now - s_last_disc_ms) > (int32_t)CAN_OFFLINE_MS) {
        g_sys.can_joined = 0;
        return;
    }
    if ((int32_t)(now - s_last_push_ms) < (int32_t)CAN_DATA_PUSH_MS) return;
    s_last_push_ms = now;

    out[0] = CAN_DATA_AIR;
    put_i16(&out[1], (int16_t)(g_sys.current_temp * 10.0f));
    put_i16(&out[3], (int16_t)(g_sys.current_humidity * 10.0f));
    put_i16(&out[5], (int16_t)g_sys.weight_g);
    out[7] = 0;
    BspCan_Send((uint32_t)(CAN_DATA_ID_BASE + s_slot), out, 8);

    out[0] = CAN_DATA_PTC;
    put_i16(&out[1], (int16_t)(g_sys.ptc_temp * 10.0f));
    put_u32(&out[3], g_sys.params.dry_time_sec);
    BspCan_Send((uint32_t)(CAN_DATA_ID_BASE + s_slot), out, 8);

    out[0] = CAN_DATA_TIME;
    put_u32(&out[1], g_sys.remaining_sec);
    out[5] = (uint8_t)g_sys.run_state;
    out[6] = 0; out[7] = 0;
    BspCan_Send((uint32_t)(CAN_DATA_ID_BASE + s_slot), out, 8);
}

void CAN_Cluster_Init(void)
{
    memset(s_slaves, 0, sizeof(s_slaves));
    g_sys.can_joined = 0;
    s_wifi_off_done = 0;
    s_hw_ready = 0;
    s_last_role = 0xFF;
    g_sys.can_connected = 0;
    if (g_sys.params.can_enabled) {
        cluster_start_hw();
        s_last_role = g_sys.params.can_role;
    }
}

void CAN_Cluster_Process(void)
{
    uint8_t role;

    if (!g_sys.params.can_enabled) {
        if (s_hw_ready) {
            cluster_stop_hw();
            s_last_role = 0xFF;
            g_sys.can_connected = 0;
        }
        return;
    }

    role = g_sys.params.can_role;
    if (!s_hw_ready) {
        cluster_start_hw();
        if (!s_hw_ready) return;
    }
    if (role != s_last_role) {
        s_last_role = role;
        g_sys.can_joined = 0;
        s_slot = 0;
        s_wifi_off_done = 0;
        s_master_id = 0;
        memset(s_slaves, 0, sizeof(s_slaves));
        g_sys.can_connected = 0;
    }

    rx_dispatch();
    if (role == 0) master_task();
    else slave_task();
}

uint8_t CAN_Cluster_GetCount(void)
{
    return g_sys.can_connected;
}

const CanSlave_t *CAN_Cluster_GetSlave(uint8_t idx)
{
    if (idx >= CAN_SLAVE_MAX) return 0;
    return &s_slaves[idx];
}

void CAN_Cluster_RequestSearch(void)
{
    uint8_t out[8];
    if (!g_sys.params.can_enabled || g_sys.params.can_role != 0) return;
    if (!s_hw_ready) cluster_start_hw();
    out[0] = CAN_MAGIC_DISCOVERY;
    put_u32(&out[1], System_GetDeviceId());
    out[5] = 0; out[6] = 0; out[7] = 0;
    BspCan_Send(CAN_ID_DISCOVERY, out, 8);
}

uint8_t CAN_Cluster_IsJoined(void)
{
    return g_sys.can_joined;
}

uint32_t CAN_Cluster_GetMasterId(void)
{
    return s_master_id;
}

/* 主机：发送控制帧到指定从机；idx=0xFF ⇒ 广播全部从机 */
uint8_t CAN_Cluster_SendCtrl(uint8_t idx, uint8_t cmd, int32_t v)
{
    uint8_t out[8];
    uint32_t id;
    if (!g_sys.params.can_enabled || g_sys.params.can_role != 0) return 0;
    id = (idx == 0xFFU) ? CAN_ID_CTRL_ALL : (uint32_t)(CAN_ID_CTRL_BASE + idx);
    out[0] = cmd;
    put_i32(&out[1], v);
    out[5] = 0; out[6] = 0; out[7] = 0;
    return (BspCan_Send(id, out, 8) == 0) ? 1 : 0;
}

#endif /* BOOTLOADER_BUILD */
