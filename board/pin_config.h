/*
 * pin_config.h
 * STM32F103C8T6 引脚定义
 * 
 * 最终确认版 - 2026/07/29
 */

#ifndef __PIN_CONFIG_H
#define __PIN_CONFIG_H

#include "stm32f10x.h"
#include "resource_config.h"

/*═════════════════════════════════════════════════════════════════════════════
 *  PA口
 *═════════════════════════════════════════════════════════════════════════════*/

// PA0 - CS1237 DATA
#define PIN_CS1237_DATA_PORT    GPIOA
#define PIN_CS1237_DATA_PIN     GPIO_Pin_0

// PA1 - CS1237 SCLK
#define PIN_CS1237_CLK_PORT     GPIOA
#define PIN_CS1237_CLK_PIN      GPIO_Pin_1

// PA2 - NTC热敏电阻 (ADC输入, 限制PTC最大工作温度)
#define PIN_NTC_PORT            GPIOA
#define PIN_NTC_PIN             GPIO_Pin_2

// PA3 - TFT DC
#define PIN_TFT_DC_PORT         GPIOA
#define PIN_TFT_DC_PIN          GPIO_Pin_3

// PA4 - TFT CS
#define PIN_TFT_CS_PORT         GPIOA
#define PIN_TFT_CS_PIN          GPIO_Pin_4

// PA5 - SPI1 SCK (TFT + W25Q128共享)
#define PIN_SPI1_SCK_PORT       GPIOA
#define PIN_SPI1_SCK_PIN        GPIO_Pin_5

// PA6 - SPI1 MISO (TFT + W25Q128共享)
#define PIN_SPI1_MISO_PORT      GPIOA
#define PIN_SPI1_MISO_PIN       GPIO_Pin_6

// PA7 - SPI1 MOSI (TFT + W25Q128共享)
#define PIN_SPI1_MOSI_PORT      GPIOA
#define PIN_SPI1_MOSI_PIN       GPIO_Pin_7

// PA8 - 加热器 PWM (TIM1_CH1)
#define PIN_PTC_PWM_PORT        GPIOA
#define PIN_PTC_PWM_PIN         GPIO_Pin_8
#define PTC_PWM_TIM             TIM1
#define PTC_PWM_CHANNEL         TIM_Channel_1

// PA9 - ESP01S TX (USART1_TX)
#define PIN_ESP_TX_PORT         GPIOA
#define PIN_ESP_TX_PIN          GPIO_Pin_9

// PA10 - ESP01S RX (USART1_RX)
#define PIN_ESP_RX_PORT         GPIOA
#define PIN_ESP_RX_PIN          GPIO_Pin_10

// PA11 - 风扇 PWM (TIM1_CH4)
#define PIN_FAN_PWM_PORT        GPIOA
#define PIN_FAN_PWM_PIN         GPIO_Pin_11
#define FAN_PWM_TIM             TIM1
#define FAN_PWM_CHANNEL         TIM_Channel_4

// PA12 - ESP01S 使能
#define PIN_ESP_EN_PORT         GPIOA
#define PIN_ESP_EN_PIN          GPIO_Pin_12

// PA13 - SWDIO (保留)
// PA14 - SWCLK (保留)

// PA15 - W25Q128 CS
#define PIN_W25Q128_CS_PORT     GPIOA
#define PIN_W25Q128_CS_PIN      GPIO_Pin_15

/*═════════════════════════════════════════════════════════════════════════════
 *  PB口
 *═════════════════════════════════════════════════════════════════════════════*/

// PB0 - TFT背光 PWM (TIM3_CH3)
#define PIN_TFT_BL_PORT         GPIOB
#define PIN_TFT_BL_PIN          GPIO_Pin_0
#define TFT_BL_TIM              TIM3
#define TFT_BL_CHANNEL          TIM_Channel_3

// PB1 - 蜂鸣器 (TIM3_CH4)
#define PIN_BUZZER_PORT         GPIOB
#define PIN_BUZZER_PIN          GPIO_Pin_1
#define BUZZER_TIM              TIM3
#define BUZZER_CHANNEL          TIM_Channel_4

// PB2 - BOOT1 (接GND)

// PB3 - EC11 A相 (GPIO轮询)
#define PIN_ENC_A_PORT          GPIOB
#define PIN_ENC_A_PIN           GPIO_Pin_3

// PB4 - EC11 B相 (GPIO轮询)
#define PIN_ENC_B_PORT          GPIOB
#define PIN_ENC_B_PIN           GPIO_Pin_4

// PB5 - EC11 按键
#define PIN_ENC_BTN_PORT        GPIOB
#define PIN_ENC_BTN_PIN         GPIO_Pin_5

// PB6 - RGB灯条2 (TIM4_CH1 + DMA1_CH1)
#define PIN_RGB2_PORT           GPIOB
#define PIN_RGB2_PIN            GPIO_Pin_6
#define RGB2_TIM                TIM4
#define RGB2_CHANNEL            TIM_Channel_1
#define RGB2_DMA_CH             DMA1_Channel1

// PB7 - RGB灯条3 (TIM4_CH2 + DMA1_CH4)
#define PIN_RGB3_PORT           GPIOB
#define PIN_RGB3_PIN            GPIO_Pin_7
#define RGB3_TIM                TIM4
#define RGB3_CHANNEL            TIM_Channel_2
#define RGB3_DMA_CH             DMA1_Channel4

// PB8 - CAN1_RX
#define PIN_CAN_RX_PORT         GPIOB
#define PIN_CAN_RX_PIN          GPIO_Pin_8

// PB9 - CAN1_TX
#define PIN_CAN_TX_PORT         GPIOB
#define PIN_CAN_TX_PIN          GPIO_Pin_9

// PB10 - AHT20 SCL (I2C2)
#define PIN_AHT20_SCL_PORT      GPIOB
#define PIN_AHT20_SCL_PIN       GPIO_Pin_10

// PB11 - AHT20 SDA (I2C2)
#define PIN_AHT20_SDA_PORT      GPIOB
#define PIN_AHT20_SDA_PIN       GPIO_Pin_11

// PB12 - 步进电机 EN
#define PIN_STEP_EN_PORT        GPIOB
#define PIN_STEP_EN_PIN         GPIO_Pin_12

// PB13 - 步进电机 STEP
#define PIN_STEP_STEP_PORT      GPIOB
#define PIN_STEP_STEP_PIN       GPIO_Pin_13

// PB14 - 步进电机 DIR
#define PIN_STEP_DIR_PORT       GPIOB
#define PIN_STEP_DIR_PIN        GPIO_Pin_14

// PB15 - 步进电机 单线UART
#define PIN_STEP_UART_PORT      GPIOB
#define PIN_STEP_UART_PIN       GPIO_Pin_15

/*═════════════════════════════════════════════════════════════════════════════
 *  PC口
 *═════════════════════════════════════════════════════════════════════════════*/

// PC13 - TFT RES (推挽输出, 2MHz)
#define PIN_TFT_RES_PORT        GPIOC
#define PIN_TFT_RES_PIN         GPIO_Pin_13

// PC14 - 32.768kHz晶振 / 备用GPIO
// PC15 - 32.768kHz晶振 / 备用GPIO

/*═════════════════════════════════════════════════════════════════════════════
 *  引脚初始化函数
 *═════════════════════════════════════════════════════════════════════════════*/

void GPIO_AllInit(void);

#endif /* __PIN_CONFIG_H */
