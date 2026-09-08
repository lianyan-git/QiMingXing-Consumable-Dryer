#ifndef BSP_RGB_LED_H
#define BSP_RGB_LED_H

#include <stdint.h>

void RGB_Strip_Init(void);
void RGB_Strip2_SetPixels(uint8_t *data, uint16_t num);
void RGB_Strip3_SetPixels(uint8_t *data, uint16_t num);

void RGB_Status_Red(void);
void RGB_Status_Green(void);
void RGB_Status_Off(void);
void RGB_AllOff(void);

void RGB_Progress_Rainbow(void);
void RGB_Progress_ColorWheel(uint8_t pos);
void RGB_Progress_DryingBar(uint8_t percent);

#endif