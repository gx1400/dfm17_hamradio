/**
  ******************************************************************************
  * @file    console_hw.c
  * @brief   The serial console's hardware: USART1 on the mini-USB connector
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Derek Rowland <gx1400@gmail.com>
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
  * USART1 is set up by CubeMX (MX_USART1_UART_Init, 115200 8N1); the receive
  * interrupt is added here at register level, so the .ioc doesn't change.
  * Pins (checked on board 1, #50): PA9 TX on the connector's D+, PA10 RX on
  * D-. Off (battery), both are analog inputs: nothing driven into a cable,
  * and a floating RX can't produce characters.
  ******************************************************************************
  */

#include "console.h"
#include "main.h"
#include "analog.h"
#include "cal.h"
#include "diag.h"
#include "GNSS.h"
#include "gps.h"
#include "ppsdisc.h"
#include "settings.h"
#include "si4063.h"
#include "aprs.h"
#include "version.h"
#include <stdio.h>

extern UART_HandleTypeDef huart1;
extern GNSS_StateHandle GNSS_Handle;

/* PA9 and PA10 in GPIOA->CRH (4 bits each: CNF, MODE) */
#define PA9_SHIFT		4
#define PA10_SHIFT		8
#define PIN_AF_PP_50MHZ 0xBUL /* CNF 10, MODE 11 */
#define PIN_INPUT_PULL	0x8UL /* CNF 10, MODE 00; pull-up with ODR = 1 */
#define PIN_ANALOG		0x0UL

void console_hw_enable(uint8_t on) {
	if (on) {
		/* RX pulled up: no adapter attached reads as an idle line */
		SET_BIT(GPIOA->ODR, GPIO_PIN_10);
		MODIFY_REG(GPIOA->CRH, 0xFFUL << PA9_SHIFT,
				PIN_AF_PP_50MHZ << PA9_SHIFT | PIN_INPUT_PULL << PA10_SHIFT);
		(void)USART1->SR;
		(void)USART1->DR;
		SET_BIT(USART1->CR1, USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE);
		/* below the APRS timer, above the PPS edge */
		HAL_NVIC_SetPriority(USART1_IRQn, 14, 0);
		HAL_NVIC_EnableIRQ(USART1_IRQn);
	} else {
		HAL_NVIC_DisableIRQ(USART1_IRQn);
		CLEAR_BIT(USART1->CR1, USART_CR1_UE | USART_CR1_RXNEIE);
		MODIFY_REG(GPIOA->CRH, 0xFFUL << PA9_SHIFT,
				PIN_ANALOG << PA9_SHIFT | PIN_ANALOG << PA10_SHIFT);
		CLEAR_BIT(GPIOA->ODR, GPIO_PIN_10);
	}
}

void console_hw_send(const char *s, uint16_t n) {
	HAL_UART_Transmit(&huart1, (const uint8_t *)s, n, 100);
}

/* a reply through printf (_write), which is linked anyway: vsnprintf would
 * add the C library's string formatter, about 1.5 KB */
void console_hw_vprintf(const char *fmt, va_list ap) {
	vprintf(fmt, ap);
	fflush(stdout);
}

/* received bytes to console_rx(); a byte with a framing or noise error is
 * dropped. reading DR clears RXNE and the error flags */
void USART1_IRQHandler(void) {
	uint32_t sr = USART1->SR;
	uint8_t c = (uint8_t)USART1->DR;
	if ((sr & USART_SR_RXNE) && !(sr & (USART_SR_FE | USART_SR_NE)))
		console_rx(c);
}

/* ver: what a host tool checks before reading or writing the blocks */
void console_hw_ver(void) {
	const uint32_t *uid = (const uint32_t *)UID_BASE;
	console_printf("ok %s proto %u settings %u %u cal %u %u uid %08lX%08lX%08lX flash %u\r\n",
			fw_version, CONSOLE_PROTO, SETTINGS_VERSION, SETTINGS_SIZE, CAL_VERSION, CAL_SIZE,
			(unsigned long)uid[0], (unsigned long)uid[1], (unsigned long)uid[2],
			*(const uint16_t *)FLASHSIZE_BASE);
}

void console_hw_status(void) {
	static const char *const from[] = {"defaults", "EEPROM", "SWD"};
	const analog_reading *r = &analog_last;

	console_printf("mode %s %s%s log %u\r\n", cfg.mode == MODE_FOX ? "fox" : "tracker",
			console_config_mode()	? "config"
					: diag_paused() ? "paused"
									: "running",
			diag_powered_off() ? " off" : "", console_log_on());
	console_printf("settings %s %s-%u %lu tx %u\r\n", from[settings_source], settings_callsign(),
			cfg.ssid, (unsigned long)cfg.freq_hz, settings_tx_allowed());
	console_printf("gps fix %u sats %u tacc %lu pps %u\r\n", GNSS_Handle.fixType, GNSS_Handle.numSV,
			GNSS_Handle.tAcc, ppsLockStatus);
	console_printf("tcxo valid %u ppb %ld applied %ld samples %lu rejected %lu\r\n", pps_disc_valid,
			(long)pps_disc_ppb, (long)pps_disc_applied_ppb, (unsigned long)pps_disc_samples,
			(unsigned long)pps_disc_rejected);
	console_printf("supply %u mV %u mA usb %u mV %d C cal %s\r\n", r->vin_mv, r->iin_ma, r->usb_mv,
			r->temp_c, from[cal_source]);
	console_printf("ok sent %lu refused %lu repairs %lu\r\n", (unsigned long)aprs_packets_sent,
			(unsigned long)si4060_tx_refused, (unsigned long)settings_repairs);
}

void console_hw_reset(void) {
	HAL_Delay(20); /* the reply is out already: the transmit blocks */
	NVIC_SystemReset();
}
