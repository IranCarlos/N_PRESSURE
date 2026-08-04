/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PRESSURE_SENSOR_Pin GPIO_PIN_0
#define PRESSURE_SENSOR_GPIO_Port GPIOC
#define ENCODER_A_Pin GPIO_PIN_0
#define ENCODER_A_GPIO_Port GPIOA
#define ENCODER_B_Pin GPIO_PIN_1
#define ENCODER_B_GPIO_Port GPIOA
#define ENCODER_BT_Pin GPIO_PIN_2
#define ENCODER_BT_GPIO_Port GPIOA
#define BT_USER_Pin GPIO_PIN_3
#define BT_USER_GPIO_Port GPIOA
#define TFT_CLOCK_Pin GPIO_PIN_5
#define TFT_CLOCK_GPIO_Port GPIOA
#define TFT_CS_Pin GPIO_PIN_6
#define TFT_CS_GPIO_Port GPIOA
#define TFT_DATA_Pin GPIO_PIN_7
#define TFT_DATA_GPIO_Port GPIOA
#define TFT_DC_Pin GPIO_PIN_4
#define TFT_DC_GPIO_Port GPIOC
#define TFT_RES_Pin GPIO_PIN_5
#define TFT_RES_GPIO_Port GPIOC
#define TFT_BACKLIGHT_Pin GPIO_PIN_0
#define TFT_BACKLIGHT_GPIO_Port GPIOB
#define BUZZER_Pin GPIO_PIN_13
#define BUZZER_GPIO_Port GPIOD
#define LED_2_Pin GPIO_PIN_14
#define LED_2_GPIO_Port GPIOD
#define LED_1_Pin GPIO_PIN_15
#define LED_1_GPIO_Port GPIOD
#define VALVE_1_Pin GPIO_PIN_1
#define VALVE_1_GPIO_Port GPIOD
#define VALVE_2_Pin GPIO_PIN_2
#define VALVE_2_GPIO_Port GPIOD
#define VALVE_3_Pin GPIO_PIN_3
#define VALVE_3_GPIO_Port GPIOD
#define PEDAL_Pin GPIO_PIN_6
#define PEDAL_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
