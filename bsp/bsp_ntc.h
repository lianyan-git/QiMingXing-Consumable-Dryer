#ifndef BSP_NTC_H
#define BSP_NTC_H

#include <stdint.h>

/* Measurement/accepted domain for the NTC *sensor only* (0.1 °C units): -40.0..200.0 °C.
 * NTC_READ_HI_10C=2000 is NOT a heater safety limit — it only bounds plausible readings
 * so open/short sentinels (-9999/+9999) can't masquerade as real temperature.
 * Heater absolute limit = PTC_TEMP_MAX (system_config.h, 160 °C), enforced in control layer.
 * 三层职责: [-40,200]℃=测量域 | 160℃=加热绝对上限 | ±9999=硬件异常哨兵。 */
#define NTC_READ_LO_10C   (-400)
#define NTC_READ_HI_10C   (2000)
#define NTC_ERR_OPEN      (-9999)   // 开路/线松
#define NTC_ERR_SHORT     ( 9999)   // 短路/ADC 拉底

#define NTC_OVERTEMP_THRESHOLD  85  // 遗留阈值；当前除 BL 外无消费者，勿用于 App 保护逻辑

void NTC_Init(void);
uint16_t NTC_ReadADC(void);
int16_t NTC_GetTemperature(void);       // 返回温度值 (单位 °C, 含小数部分 *10)
uint8_t NTC_IsOverTemp(void);           // 是否超过过热保护阈值（遗留，App 未用）
uint8_t NTC_IsValid(void);               // NTC 读数有效(落在测量域内, 非开路/短路哨兵)

#endif /* BSP_NTC_H */