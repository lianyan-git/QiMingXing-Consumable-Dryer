/*
 * upgrade_flag.h
 * 升级标志管理
 */

#ifndef __UPGRADE_FLAG_H
#define __UPGRADE_FLAG_H

#include "shared_defs.h"

int UpgradeFlag_Read(UpgradeFlag_t *flag);
int UpgradeFlag_Write(UpgradeFlag_t *flag);
int UpgradeFlag_ReadExt(UpgradeFlag_t *flag);
int UpgradeFlag_WriteExt(UpgradeFlag_t *flag);
void UpgradeFlag_Clear(void);

#endif /* __UPGRADE_FLAG_H */
