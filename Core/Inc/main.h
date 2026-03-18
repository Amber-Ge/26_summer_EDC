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
#include "stm32f4xx_hal.h"

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

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define Laser_Pin GPIO_PIN_2
#define Laser_GPIO_Port GPIOE
#define Buzzer_Pin GPIO_PIN_3
#define Buzzer_GPIO_Port GPIOE
#define LED_RED_Pin GPIO_PIN_10
#define LED_RED_GPIO_Port GPIOF
#define LED_GREEN_Pin GPIO_PIN_11
#define LED_GREEN_GPIO_Port GPIOF
#define LED_YELLOW_Pin GPIO_PIN_12
#define LED_YELLOW_GPIO_Port GPIOF
#define Sensor_1_Pin GPIO_PIN_0
#define Sensor_1_GPIO_Port GPIOG
#define Sensor_2_Pin GPIO_PIN_1
#define Sensor_2_GPIO_Port GPIOG
#define AIN1_Pin GPIO_PIN_7
#define AIN1_GPIO_Port GPIOE
#define AIN2_Pin GPIO_PIN_8
#define AIN2_GPIO_Port GPIOE
#define BIN1_Pin GPIO_PIN_9
#define BIN1_GPIO_Port GPIOE
#define BIN2_Pin GPIO_PIN_10
#define BIN2_GPIO_Port GPIOE
#define KEY_1_Pin GPIO_PIN_2
#define KEY_1_GPIO_Port GPIOG
#define KEY_2_Pin GPIO_PIN_3
#define KEY_2_GPIO_Port GPIOG
#define KEY_3_Pin GPIO_PIN_4
#define KEY_3_GPIO_Port GPIOG
#define Sensor_3_Pin GPIO_PIN_5
#define Sensor_3_GPIO_Port GPIOG
#define Sensor_4_Pin GPIO_PIN_6
#define Sensor_4_GPIO_Port GPIOG
#define Sensor_5_Pin GPIO_PIN_7
#define Sensor_5_GPIO_Port GPIOG
#define Sensor_6_Pin GPIO_PIN_8
#define Sensor_6_GPIO_Port GPIOG
#define Sensor_7_Pin GPIO_PIN_9
#define Sensor_7_GPIO_Port GPIOG
#define Sensor_8_Pin GPIO_PIN_10
#define Sensor_8_GPIO_Port GPIOG
#define Sensor_9_Pin GPIO_PIN_11
#define Sensor_9_GPIO_Port GPIOG
#define Sensor_10_Pin GPIO_PIN_12
#define Sensor_10_GPIO_Port GPIOG
#define Sensor_11_Pin GPIO_PIN_13
#define Sensor_11_GPIO_Port GPIOG
#define Sensor_12_Pin GPIO_PIN_14
#define Sensor_12_GPIO_Port GPIOG

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
