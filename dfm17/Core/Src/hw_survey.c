/**
  ******************************************************************************
  * @file    hw_survey.c
  * @brief   One-shot hardware survey: analog inputs, power states, EEPROM
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
  * Built when HW_SURVEY is defined in main.c. Runs once after initHw(), then the
  * main loop idles (no fox). Results are left in the hw_survey struct in RAM and
  * read back over SWD; see docs/hardware-investigation.md.
  *
  * Uses registers directly (ADC1, I2C bit-banged on PB6/PB7, backup domain) so
  * no extra HAL modules or CubeMX changes are needed.
  ******************************************************************************
  */

#include "main.h"
#include "init.h"
#include "tim.h"
#include "usart.h"
#include "gps.h"
#include "GNSS.h"
#include "si4063.h"
#include "fox.h"
#include "diag.h"
#include "i2c_ee.h"
#include "settings.h"

extern volatile GNSS_StateHandle GNSS_Handle;

#define ADC_CH_PA0		0
#define ADC_CH_PB1		9
#define ADC_CH_TEMP		16
#define ADC_CH_VREFINT	17


enum {
	S_IDLE,			/* radio READY, GPS on */
	S_TX_00,		/* unmodulated carrier, PA_PWR_LVL 0x00 */
	S_TX_20,
	S_TX_40,
	S_TX_7F,
	S_RADIO_SLEEP,	/* MCU on HSI, radio SLEEP */
	S_RADIO_SDN,	/* MCU on HSI, radio in shutdown (SDN high) */
	S_RADIO_BACK,	/* radio re-initialised, MCU back on radio clock */
	S_GPS_BACKUP,	/* GPS in software backup (UBX-RXM-PMREQ) */
	S_GPS_AWAKE,	/* GPS woken by UART activity */
	S_STATES
};

typedef struct {
	uint16_t pa0, pb1, vrefint, temp;	/* raw 12-bit averages */
} adc_sample;

struct {
	uint32_t magic;				/* 0x53555256 "SURV" when finished */
	uint32_t step;				/* progress, for a survey that hangs */
	adc_sample s[S_STATES];
	uint16_t pd0_trans_ready;	/* DIV_CLK activity on PD0, radio READY */
	uint16_t pd0_trans_sleep;	/* ... radio SLEEP */
	uint16_t pd0_trans_sdn;		/* ... radio shutdown */
	uint8_t lse_ready;			/* 32.768 kHz crystal started */
	uint8_t pps_during_backup;	/* ppsLockStatus 3 s into GPS backup */
	uint8_t gps_wake_ok;		/* PVT poll answered after wake-up */
	uint8_t gps_reply_hdr[4];	/* B5 62 01 07 for a real NAV-PVT reply */
	uint8_t radio_back_ok;		/* switchToRadioClock() succeeded again */
	uint8_t i2c_idle;			/* bit0 SCL, bit1 SDA high when released */
	uint8_t pc13;				/* EEPROM RF WIP/BUSY level */
	uint8_t i2c_ack[16];		/* bitmap of 7-bit addresses that ACKed */
	uint8_t ee_user_ok, ee_sys_ok;
	uint8_t ee_user[512];
	uint8_t ee_sys_lo[16];		/* system area 0x0000.. (sector security) */
	uint8_t ee_sys_hi[32];		/* system area 0x0900.. (password, UID, ...) */
	/* write test on the last 4 bytes of the free sector 3 (0x1FC) */
	uint8_t ee_wr_ack;			/* write command bytes ACKed */
	uint8_t ee_wr_poll_ms;		/* ms until the device ACKed again */
	uint8_t ee_wr_before[4], ee_wr_after[4], ee_wr_restored[4];
	/* floor: LEDs off, radio shutdown, GPS backup; MCU at 24 MHz, then 8 MHz HSI */
	adc_sample floor24, floor8;
} volatile hw_survey;

/* ---- ADC1, registers ---------------------------------------------------- */

static void adc_init(void) {
	/* PCLK2 24 MHz / 2 = 12 MHz, the F100 maximum */
	MODIFY_REG(RCC->CFGR, RCC_CFGR_ADCPRE, RCC_CFGR_ADCPRE_DIV2);
	SET_BIT(RCC->APB2ENR, RCC_APB2ENR_ADC1EN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN);
	/* PA0 and PB1 analog (CNF/MODE 0000) */
	MODIFY_REG(GPIOA->CRL, 0xFUL << 0, 0);
	MODIFY_REG(GPIOB->CRL, 0xFUL << 4, 0);

	/* longest sample time (239.5 cycles) on every channel */
	ADC1->SMPR1 = 0x00FFFFFF;
	ADC1->SMPR2 = 0x3FFFFFFF;
	ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_TSVREFE | ADC_CR2_EXTTRIG | ADC_CR2_EXTSEL;
	HAL_Delay(2);
	SET_BIT(ADC1->CR2, ADC_CR2_RSTCAL);
	while (READ_BIT(ADC1->CR2, ADC_CR2_RSTCAL))
		;
	SET_BIT(ADC1->CR2, ADC_CR2_CAL);
	while (READ_BIT(ADC1->CR2, ADC_CR2_CAL))
		;
}

static uint16_t adc_read(uint8_t ch) {
	uint32_t sum = 0;
	ADC1->SQR1 = 0;
	ADC1->SQR3 = ch;
	for (int i = 0; i < 64; i++) {
		SET_BIT(ADC1->CR2, ADC_CR2_SWSTART);
		while (!READ_BIT(ADC1->SR, ADC_SR_EOC))
			;
		sum += ADC1->DR;
	}
	return (uint16_t)(sum / 64);
}

static adc_sample last;

/* state < 0: measure into 'last' only */
static void sample(int state) {
	HAL_Delay(300);
	last.pa0 = adc_read(ADC_CH_PA0);
	last.pb1 = adc_read(ADC_CH_PB1);
	last.vrefint = adc_read(ADC_CH_VREFINT);
	last.temp = adc_read(ADC_CH_TEMP);
	if (state >= 0)
		hw_survey.s[state] = last;
}

/* ---- clocks ------------------------------------------------------------- */

/* SYSCLK back to the 24 MHz HSI PLL and HSE off, so the radio can stop DIV_CLK */
static void clock_to_hsi(void) {
	CLEAR_BIT(RCC->CR, RCC_CR_CSSON);
	MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_HSI);
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI)
		;
	SystemClock_Config();
	CLEAR_BIT(RCC->CR, RCC_CR_HSEON);
	while (READ_BIT(RCC->CR, RCC_CR_HSERDY))
		;
	CLEAR_BIT(RCC->CR, RCC_CR_HSEBYP);
}

/* PD0 (Si4063 GPIO2) transitions over 10000 reads; needs HSE off */
static uint16_t pd0_transitions(void) {
	__HAL_RCC_AFIO_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_AFIO_REMAP_PD01_ENABLE();
	uint32_t prev = GPIOD->IDR & 1;
	uint16_t t = 0;
	for (int i = 0; i < 10000; i++) {
		uint32_t cur = GPIOD->IDR & 1;
		t += cur != prev;
		prev = cur;
	}
	__HAL_AFIO_REMAP_PD01_DISABLE();
	return t;
}

static uint8_t lse_test(void) {
	uint8_t ok = 0;
	SET_BIT(RCC->APB1ENR, RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN);
	SET_BIT(PWR->CR, PWR_CR_DBP);
	SET_BIT(RCC->BDCR, RCC_BDCR_LSEON);
	for (int i = 0; i < 3000; i++) {
		if (READ_BIT(RCC->BDCR, RCC_BDCR_LSERDY)) {
			ok = 1;
			break;
		}
		HAL_Delay(1);
	}
	CLEAR_BIT(RCC->BDCR, RCC_BDCR_LSEON);
	CLEAR_BIT(PWR->CR, PWR_CR_DBP);
	return ok;
}

/* ---- GPS power save ----------------------------------------------------- */

static void ubx_send(uint8_t cls, uint8_t id, const uint8_t *payload, uint16_t len) {
	uint8_t msg[32];
	uint8_t ck_a = 0, ck_b = 0;
	msg[0] = 0xB5;
	msg[1] = 0x62;
	msg[2] = cls;
	msg[3] = id;
	msg[4] = len & 0xFF;
	msg[5] = len >> 8;
	for (uint16_t i = 0; i < len; i++)
		msg[6 + i] = payload[i];
	for (uint16_t i = 2; i < 6 + len; i++) {
		ck_a += msg[i];
		ck_b += ck_a;
	}
	msg[6 + len] = ck_a;
	msg[7 + len] = ck_b;
	HAL_UART_Transmit(&huart2, msg, 8 + len, 100);
}

/* ---- survey ------------------------------------------------------------- */

void hw_survey_run(void) {
	hw_survey.step = 1;
	adc_init();
	sample(S_IDLE);

	/* transmit an unmodulated carrier at increasing PA levels */
	hw_survey.step = 2;
	si4060_set_cw(1);
	const uint8_t levels[4] = {0x00, 0x20, 0x40, 0x7F};
	for (int i = 0; i < 4; i++) {
		si4060_set_tx_power(levels[i]);
		si4060_start_tx(0);
		sample(S_TX_00 + i);
		si4060_stop_tx();
		HAL_Delay(200);
	}
	si4060_set_cw(0);
	si4060_set_tx_power(cfg.pa_level);

	/* radio low-power states; the MCU must leave the radio clock first */
	hw_survey.step = 3;
	clock_to_hsi();
	hw_survey.pd0_trans_ready = pd0_transitions();
	si4060_change_state(STATE_SLEEP);
	sample(S_RADIO_SLEEP);
	hw_survey.pd0_trans_sleep = pd0_transitions();
	si4060_shutdown();
	sample(S_RADIO_SDN);
	hw_survey.pd0_trans_sdn = pd0_transitions();

	hw_survey.step = 4;
	initRadio();
	hw_survey.radio_back_ok = switchToRadioClock() == HAL_OK;
	sample(S_RADIO_BACK);

	/* GPS software backup: UBX-RXM-PMREQ, infinite duration, backup flag */
	hw_survey.step = 5;
	const uint8_t pmreq[8] = {0, 0, 0, 0, 0x02, 0, 0, 0};
	ubx_send(0x02, 0x41, pmreq, sizeof(pmreq));
	HAL_Delay(3000);
	hw_survey.pps_during_backup = ppsLockStatus;
	sample(S_GPS_BACKUP);

	/* any UART activity wakes the receiver */
	hw_survey.step = 6;
	const uint8_t wake[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	HAL_UART_Transmit(&huart2, (uint8_t *)wake, sizeof(wake), 100);
	HAL_Delay(1500);
	uint32_t timeouts = GNSS_timeouts;
	GNSS_GetPVTData((GNSS_StateHandle *)&GNSS_Handle);
	hw_survey.gps_wake_ok = GNSS_timeouts == timeouts;
	for (int i = 0; i < 4; i++)
		hw_survey.gps_reply_hdr[i] = GNSS_Handle.uartWorkingBuffer[i];
	sample(S_GPS_AWAKE);

	/* 32.768 kHz crystal for an RTC wake-up? */
	hw_survey.step = 7;
	hw_survey.lse_ready = lse_test();

	/* EEPROM: scan and read-only dump */
	hw_survey.step = 8;
	hw_survey.pc13 = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET;
	i2c_init();
	hw_survey.i2c_idle = i2c_bus_idle();
	for (uint8_t a = 0x08; a < 0x78; a++) {
		i2c_start();
		if (i2c_write(a << 1))
			hw_survey.i2c_ack[a >> 3] |= 1 << (a & 7);
		i2c_stop();
	}
	hw_survey.ee_user_ok = ee_read(EE_USER_ADDR, 0x0000, hw_survey.ee_user, sizeof(hw_survey.ee_user));
	hw_survey.ee_sys_ok = ee_read(EE_SYS_ADDR, 0x0000, hw_survey.ee_sys_lo, sizeof(hw_survey.ee_sys_lo))
			&& ee_read(EE_SYS_ADDR, 0x0900, hw_survey.ee_sys_hi, sizeof(hw_survey.ee_sys_hi));

	/* write test, only if the test bytes are erased (0xFF) */
	ee_read(EE_USER_ADDR, 0x01FC, hw_survey.ee_wr_before, 4);
	if (hw_survey.ee_wr_before[0] == 0xFF && hw_survey.ee_wr_before[1] == 0xFF
			&& hw_survey.ee_wr_before[2] == 0xFF && hw_survey.ee_wr_before[3] == 0xFF) {
		const uint8_t pattern[4] = {0xA5, 0x5A, 0xC3, 0x3C};
		const uint8_t erased[4] = {0xFF, 0xFF, 0xFF, 0xFF};
		hw_survey.ee_wr_ack = ee_write(EE_USER_ADDR, 0x01FC, pattern, 4);
		hw_survey.ee_wr_poll_ms = ee_poll(EE_USER_ADDR);
		ee_read(EE_USER_ADDR, 0x01FC, hw_survey.ee_wr_after, 4);
		ee_write(EE_USER_ADDR, 0x01FC, erased, 4);
		ee_poll(EE_USER_ADDR);
		ee_read(EE_USER_ADDR, 0x01FC, hw_survey.ee_wr_restored, 4);
	}

	/* current floor with everything we can switch off */
	hw_survey.step = 10;
	diag_leds_off();
	clock_to_hsi();
	si4060_shutdown();
	ubx_send(0x02, 0x41, pmreq, sizeof(pmreq));
	HAL_Delay(2000);
	sample(-1);
	hw_survey.floor24 = last;
	{
		RCC_ClkInitTypeDef clk = {0};
		clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
		clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
		clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
		clk.APB1CLKDivider = RCC_HCLK_DIV1;
		clk.APB2CLKDivider = RCC_HCLK_DIV1;
		HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0);
		CLEAR_BIT(RCC->CR, RCC_CR_PLLON);
	}
	sample(-1);
	hw_survey.floor8 = last;

	/* restore: 24 MHz, GPS awake, radio up, MCU on the radio clock, LEDs */
	hw_survey.step = 11;
	SystemClock_Config();
	HAL_UART_Transmit(&huart2, (uint8_t *)wake, sizeof(wake), 100);
	initRadio();
	switchToRadioClock();
	diag_selftest();

	/* identify after the test carriers */
	hw_survey.step = 9;
	fox_transmit((GNSS_StateHandle *)&GNSS_Handle);

	hw_survey.magic = 0x53555256;
}
