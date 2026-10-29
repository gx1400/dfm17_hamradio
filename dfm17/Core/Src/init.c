/**
  ******************************************************************************
  * @file    init.c
  * @brief   This file contains all functions for initializing hardware
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
  */

#include <stdio.h>
#include "init.h"
#include "GNSS.h"
#include "gpio.h"
#include "dma.h"
#include "usart.h"
#include "spi.h"
#include "tim.h"
#include "si4063.h"
#include "diag.h"
#include "analog.h"


extern UART_HandleTypeDef huart2;
extern TIM_HandleTypeDef htim6;
extern GNSS_StateHandle GNSS_Handle;
extern uint8_t txDone;
extern uint8_t rxDone;

/* uncomment to record Si4063 clock-output diagnostics at boot (radio_probe.c),
 * read back over SWD */
//#define RADIO_CLOCK_PROBE
void radio_clock_probe(void);

/* set by the clock security system when the Si4063 clock on OSC_IN stops */
static volatile uint8_t radioClockLost = 0;

void initHw(void) {
	// initialize STM hardware
	SystemClock_Config();
	MX_GPIO_Init();
	// all LEDs on for 0.5 s, then diag_tick() drives them
	diag_selftest();
	// supply voltage/current and temperature for APRS telemetry
	analog_init();
	MX_DMA_Init();
	MX_USART1_UART_Init();
	MX_USART2_UART_Init();
	MX_SPI1_Init();
	MX_TIM6_Init();
	MX_TIM17_Init();
	delay_us(50);

	//initialize radio
	initRadio();
#ifdef RADIO_CLOCK_PROBE
	radio_clock_probe();
#endif

	// run the MCU from the radio's TCXO instead of the +-1% HSI
	if (switchToRadioClock() != HAL_OK) {
		printf("ERROR: no Si4063 clock on OSC_IN, staying on HSI\r\n");
		diag_set_error(DIAG_ERR_RADIO_CLOCK);
	}

	// initialize GPS chip
	printf("Starting ublox...\r\n");
	GNSS_Init(&GNSS_Handle, &huart2, &txDone, &rxDone);
	HAL_Delay(1000);
	GNSS_LoadConfig(&GNSS_Handle);
	if (GNSS_timeouts) {
		printf("ERROR: GPS not responding\r\n");
		diag_set_error(DIAG_ERR_GPS);
	}

	//after GPS is initialized, then start GPS update tick timer
	startGpsTickTimer();
}

void initRadio() {
	SpiEnable();

	//restart radio
	printf("wake up radio...\r\n");
	si4060_wakeup();
	HAL_Delay(10);
	printf("reset radio...\r\n");
	si4060_reset();

	printf("check radio info...\r\n");
	uint16_t i;
	i = si4060_part_info();
	printf("Radio info: %04X\r\n",i);

	if(i != 0x4063) {
		printf("ERROR: Incorrect radio, not a 4063!\r\n");
		diag_set_error(DIAG_ERR_RADIO);
	}

	si4060_power_up(); 		//power up radio
	si4060_setup(MOD_TYPE_2FSK);
	si4060_config_dfm17();
	// idle with the crystal running: GPIO2 DIV_CLK clocks the STM32
	si4060_stop_tx();
}

/*
 * switchToRadioClock
 *
 * moves SYSCLK from the HSI PLL to the Si4063's 12.8 MHz DIV_CLK, which arrives on
 * PD0/OSC_IN (HSE bypass): 12.8 MHz / PREDIV1 8 * PLL 15 = 24 MHz. this is the
 * same 24 MHz the HSI PLL gives, so no peripheral needs reconfiguring, but timing
 * now follows the TCXO. the clock security system falls back to HSI if the radio
 * clock ever stops (see checkRadioClock()).
 *
 * the radio must already be configured (si4060_setup) so DIV_CLK is running.
 */
HAL_StatusTypeDef switchToRadioClock(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	// the PLL can't be reconfigured while it drives SYSCLK, so step down to HSI
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
		SystemClock_Config();
		return HAL_ERROR;
	}

	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
	RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV8;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL15;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		// no clock from the radio: back to the 24 MHz HSI PLL
		SystemClock_Config();
		return HAL_ERROR;
	}

	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
								|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
		SystemClock_Config();
		return HAL_ERROR;
	}

	HAL_RCC_EnableCSS();
	return HAL_OK;
}

/* called from the NMI by HAL_RCC_NMI_IRQHandler(); the hardware has already
 * switched SYSCLK to the bare 8 MHz HSI */
void HAL_RCC_CSSCallback(void) {
	radioClockLost = 1;
}

/*
 * checkRadioClock
 *
 * call from the main loop: after a radio clock failure, restore the 24 MHz HSI
 * PLL so timers and UARTs run at the right rate again, and light the red LED.
 */
void checkRadioClock(void) {
	if (radioClockLost) {
		radioClockLost = 0;
		SystemClock_Config();
		diag_set_error(DIAG_ERR_RADIO_CLOCK);
		printf("ERROR: Si4063 clock lost, running on HSI\r\n");
	}
}


