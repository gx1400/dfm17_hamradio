/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
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
#include "stm32f1xx_hal.h"

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
int __io_putchar(int ch);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define oSpiGPIO2_Pin GPIO_PIN_0
#define oSpiGPIO2_GPIO_Port GPIOD
#define oBattOn_Pin GPIO_PIN_0
#define oBattOn_GPIO_Port GPIOC
#define oSiSDN_Pin GPIO_PIN_3
#define oSiSDN_GPIO_Port GPIOC
#define gpsTx_Pin GPIO_PIN_2
#define gpsTx_GPIO_Port GPIOA
#define gpsRx_Pin GPIO_PIN_3
#define gpsRx_GPIO_Port GPIOA
#define oSpiGPIO3_Pin GPIO_PIN_4
#define oSpiGPIO3_GPIO_Port GPIOA
#define oSpiSCLK_Pin GPIO_PIN_5
#define oSpiSCLK_GPIO_Port GPIOA
#define oSpiMISO_Pin GPIO_PIN_6
#define oSpiMISO_GPIO_Port GPIOA
#define oSpiMOSI_Pin GPIO_PIN_7
#define oSpiMOSI_GPIO_Port GPIOA
#define oSpiCS_Pin GPIO_PIN_2
#define oSpiCS_GPIO_Port GPIOB
#define oLED_R_Pin GPIO_PIN_12
#define oLED_R_GPIO_Port GPIOB
#define oLED_G_Pin GPIO_PIN_6
#define oLED_G_GPIO_Port GPIOC
#define oLED_Y_Pin GPIO_PIN_7
#define oLED_Y_GPIO_Port GPIOC
#define iButton_Pin GPIO_PIN_8
#define iButton_GPIO_Port GPIOC
#define usbTX_Pin GPIO_PIN_9
#define usbTX_GPIO_Port GPIOA
#define usbRX_Pin GPIO_PIN_10
#define usbRX_GPIO_Port GPIOA
#define iGpsPPS_Pin GPIO_PIN_8
#define iGpsPPS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
