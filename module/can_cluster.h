/*
 * can_cluster.h
 * CAN 集群协议：主机/从机、发现/加入/心跳、从机数据推送、离线剔除
 */
#ifndef CAN_CLUSTER_H
#define CAN_CLUSTER_H

#include <stdint.h>

#define CAN_SLAVE_MAX   16     /* 最多级联从机数 */

/* 从机实时数据（网页端所需字段，后续按网页协议微调） */
typedef struct {
    uint32_t device_id;
    uint32_t last_rx_ms;       /* 最近一次收到数据的时间戳(主机视角) */
    int16_t  air_temp_x10;     /* 空气温度 x10 */
    int16_t  humidity_x10;     /* 湿度 x10 */
    int16_t  weight_g;         /* 重量 克 */
    int16_t  ptc_temp_x10;     /* PTC 温度 x10 */
    uint32_t dry_time_sec;     /* 烘干时间 */
    uint32_t remaining_sec;    /* 剩余时间 */
    uint8_t  run_state;
    uint8_t  online;
} CanSlave_t;

void CAN_Cluster_Init(void);
void CAN_Cluster_Process(void);            /* 主循环频繁调用：收帧分发+周期广播/推送+超时剔除 */

uint8_t CAN_Cluster_GetCount(void);        /* 主机：当前在线从机数 */
const CanSlave_t *CAN_Cluster_GetSlave(uint8_t idx);
void CAN_Cluster_RequestSearch(void);      /* 主机：立即广播一次搜索 */
uint8_t CAN_Cluster_IsJoined(void);        /* 从机：是否已接入主机网络 */
uint32_t CAN_Cluster_GetMasterId(void);    /* 从机：当前主机序列号 */
/* 主机→从机控制帧：idx=0-15 指定槽位 / 0xFF 广播。cmd:1=run 2=温度 3=时间秒 4=PTC温度 */
uint8_t CAN_Cluster_SendCtrl(uint8_t idx, uint8_t cmd, int32_t v);

#define CAN_CTRL_RUN        1U
#define CAN_CTRL_SET_TEMP   2U
#define CAN_CTRL_SET_TIME   3U
#define CAN_CTRL_SET_PTC    4U

#endif /* CAN_CLUSTER_H */
