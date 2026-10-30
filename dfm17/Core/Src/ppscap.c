/**
  ******************************************************************************
  * @file    ppscap.c
  * @brief   GPS PPS timestamps from the TIM4 input capture on PB8
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
  * TIM4 counts the 24 MHz timer clock (APB1 undivided) and latches its count
  * into CCR3 on the PPS rising edge on PB8 (TIM4_CH3, no remap). The PPS
  * interrupt (EXTI, diag_pps()) reads the latched value whenever it gets to
  * run, so the timestamp is exact to a cycle however late the interrupt is,
  * and the timer keeps counting while the CPU sleeps. Issue #12.
  *
  * No overflow interrupt and no 32-bit extension: a usable interval is
  * 24 000 000 +- 240 cycles (+-10 ppm), and the HAL-tick check in
  * pps_disc_sample() already rejects missed pulses, so the 16-bit difference
  * mod 65536 picks the full count out uniquely.
  *
  * Only CH3 is used; TIM4's CH1/CH2 pins PB6/PB7 stay the EEPROM's I2C. PB8
  * stays an EXTI input too (PPS LED, lock flag): one input feeds both.
  ******************************************************************************
  */

#include "ppscap.h"
#include "main.h"
#include "ppsdisc.h"

volatile uint8_t pps_source = PPS_SOURCE_CAPTURE;
volatile pps_log_entry pps_log[PPS_LOG];
volatile uint32_t pps_log_head;
volatile uint32_t pps_cap_overcaptures;
volatile uint32_t pps_cap_missing;
volatile uint8_t pps_sleep_test;

static uint16_t last_capture;
static uint8_t have_last;

void pps_capture_init(void) {
	SET_BIT(RCC->APB1ENR, RCC_APB1ENR_TIM4EN);
	TIM4->CR1 = 0;
	TIM4->PSC = 0;
	TIM4->ARR = 0xFFFF;
	/* CC3 input on TI3; digital filter: 8 samples at the timer clock, which
	 * ignores glitches and only adds a constant delay */
	TIM4->CCMR2 = TIM_CCMR2_CC3S_0 | TIM_CCMR2_IC3F_0 | TIM_CCMR2_IC3F_1;
	TIM4->CCER = TIM_CCER_CC3E; /* rising edge */
	TIM4->EGR = TIM_EGR_UG;
	TIM4->SR = 0;
	TIM4->CR1 = TIM_CR1_CEN;
	have_last = 0;
}

/*
 * pps_capture_interval
 *
 * the full number of timer cycles between two captures, from their 16-bit
 * values: PPS_DISC_NOMINAL plus the deviation, which is assumed to be within
 * +-32767 cycles (it has to be within +-240 to be used at all)
 */
uint32_t pps_capture_interval(uint16_t prev, uint16_t now) {
	uint16_t diff = (uint16_t)(now - prev);
	int16_t dev = (int16_t)(uint16_t)(diff - (uint16_t)PPS_DISC_NOMINAL);
	return (uint32_t)((int32_t)PPS_DISC_NOMINAL + dev);
}

/*
 * pps_capture_read
 *
 * call from the PPS interrupt. returns 1 with the cycles since the previous
 * pulse, or 0 if there's no usable interval (first pulse, no capture, or an
 * edge was missed)
 */
uint8_t pps_capture_read(uint32_t *cycles) {
	uint32_t sr = TIM4->SR;

	if (!(sr & TIM_SR_CC3IF)) {
		pps_cap_missing++;
		have_last = 0;
		return 0;
	}
	uint16_t now = (uint16_t)TIM4->CCR3; /* reading clears CC3IF */
	uint8_t ok = have_last;
	if (sr & TIM_SR_CC3OF) {
		/* a second edge came before this was read: the interval is unknown */
		TIM4->SR = ~TIM_SR_CC3OF;
		pps_cap_overcaptures++;
		ok = 0;
	}
	uint16_t prev = last_capture;
	last_capture = now;
	have_last = 1;
	if (!ok)
		return 0;
	*cycles = pps_capture_interval(prev, now);
	return 1;
}

void pps_log_add(uint32_t dwt, uint32_t capture) {
	uint32_t i = pps_log_head % PPS_LOG;
	pps_log[i].dwt = dwt;
	pps_log[i].capture = capture;
	pps_log_head++;
}
