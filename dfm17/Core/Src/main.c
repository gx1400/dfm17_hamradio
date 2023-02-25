/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 Derek Rowland <gx1400@gmail.com>
  * All rights reserved.
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  ******************************************************************************
  * Leveraged Projects
  *
  * - SimpleMethod/STM32-GNSS
  *   - https://github.com/SimpleMethod/STM32-GNSS
  *   - Copyright 2020 SimpleMethod
  *   - Modified 2023 in GNSS.c and GNSS.h
  *
  * - thasti/utrak
  *   - https://github.com/thasti/utrak
  *   - Stefan Biereigel
  *   - Modified 2023 in si4063.c and si4063.h
  *
  ******************************************************************************
  *	TIMERS
  *	 -------------------------------------------
  *	| TIMER		| Purpose						|
  *	|-----------|-------------------------------|
  *	| TIM1 		| Reserved for tmux				|
  *	| TIM2  	| Reserved for tmux				|
  *	| TIM3  	| Reserved for tmux				|
  *	| TIM6		| Tick Timer for GPS Updates	|
  *	| TIM7      | GPS lock timer                |
  *	| TIM15		| Tick Timer for APRS Baud		|
  *	| TIM16		| Tick Timer for RTTY Baud		|
  *	| TIM17     | delay_us 1us timer            |
  *	 -------------------------------------------
  ******************************************************************************
  * INTERRUPTS
  *  -------------------------------------------------------
  * | INTERRUPT | Priority | Purpose                        |
  * |-----------|----------|--------------------------------|
  * | TIM15     |    1     | APRS Baud Clock                |
  * | TIM16     |    2     | RTTY Baud Clock                |
  * | DMA6      |    6     | GPS UART RX DMA                |
  * | DMA7      |    7     | GPS UART TX DMA                |
  * | TIM6      |   10     | GPS Update Tick Timer          |
  * | TIM7      |   14     | GPS Lock timer                 |
  * | EXTIO     |   15     | GPS 1pps input interrupt       |
  *  -------------------------------------------------------
  ******************************************************************************
  * LEDs
  * - Red: 		Error has occurred (not a hard fault)
  * - Green: 	Transmitter mode indicator
  *   - APRS - 1 Hz blink
  *   - RTTY - 2 Hz blink
  *   - Off  - Off
  * - Yellow:	GPS has a fix
  *
  * Special scenarios:
  * - Hard fault = all LEDs turn on and stay on
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "led.h"
#include "GNSS.h"
#include "init.h"
#include "aprs.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

volatile GNSS_StateHandle GNSS_Handle;
volatile uint8_t txDone;
volatile uint8_t rxDone;

volatile uint16_t aprs_bit;
volatile uint16_t aprs_tick;
volatile uint16_t aprs_baud_tick;


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint16_t calc_aprscrc (uint16_t crcStart, uint8_t *frame, uint8_t frame_len)
{

    uint8_t i, j;
    // Preload the CRC register with ones
    //uint16_t crc = 0xffff;
    uint16_t crc = crcStart;


    // Iterate over every octet in the frame
    for (i = 0; i < frame_len; i++)
    {
        // Iterate over every bit, LSb first
        for (j = 0; j < 8; j++)
        {
            uint8_t bit = (frame[i] >> j) & 0x01;

            // Divide by a bit - reversed 0x1021
            if ((crc & 0x0001) != bit)
            {
                crc = (crc >> 1) ^ 0x8408;
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }


    return crc;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_TIM15_Init();
  MX_TIM7_Init();
  /* USER CODE BEGIN 2 */

  initHw();
  si4060_stop_tx();



  aprs_prepare_buffer(&GNSS_Handle, 0);
  calculate_fcs();

  stopGpsLockTimer();
  stopGpsTickTimer();


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */


    /* USER CODE BEGIN 3 */
	  HAL_Delay(2000);
	  tx_aprs();

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

int __io_putchar(int ch) {
 // Write character to ITM ch.0
 ITM_SendChar(ch);
 return(ch);
}



/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
	  ledOnRed();
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
