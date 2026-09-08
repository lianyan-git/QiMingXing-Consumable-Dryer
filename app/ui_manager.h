#ifndef __UI_MANAGER_H
#define __UI_MANAGER_H

#include <stdint.h>

void UI_ShowBootScreen(void);
void UI_Update(void);
void UI_DrawMainScreen(void);
void UI_DrawWeightScreen(void);
void UI_DrawTempAdjust(void);
void UI_DrawTempPid(void);
void UI_DrawTimeAdjust(void);
void UI_DrawPreset(void);
void UI_DrawPresetMenu(void);
void UI_DrawPresetEdit(void);
void UI_DrawPtcAdjust(void);
void UI_DrawPtcEdit(void);
void UI_DrawPtcCoolingEdit(void);
void UI_DrawPidAutotune(void);
void UI_DrawMenu(void);
void UI_RefreshMenuSel(uint8_t old_idx, uint8_t new_idx);
void UI_MenuScroll(int dir);
void UI_MotorScroll(int dir);
void UI_SettingsScroll(int dir);
void UI_DrawMotorAdjust(void);
void UI_DrawAbout(void);
void UI_DrawWiFiScreen(void);
void UI_DrawOTAScreen(void);
void UI_DrawSettingsScreen(void);
void theme_apply(void);
void UI_UpdateMainDynamic(void);
void UI_RefreshCard(uint8_t item);
void UI_ResetOTAScreen(void);

#endif