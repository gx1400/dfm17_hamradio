/**
  ******************************************************************************
  * @file    ppsdisc.c
  * @brief   Corrects the carrier frequency for the TCXO error measured against
  *          the GPS PPS
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
  * The STM32 runs from the Si4063's TCXO (DIV_CLK, through the PLL), so the
  * number of CPU cycles between two GPS PPS pulses measures the TCXO against
  * GPS: 24 000 000 when it is exact, one cycle = 41.7 ppb. The carrier comes
  * from the same TCXO and is off by the same fraction, so programming
  * f * (1 - error) puts it on f.
  *
  * pps_disc_sample() runs in the PPS interrupt and only queues the count.
  * pps_disc_update() runs in the main loop, which only runs between
  * transmissions: the synthesizer can't be retuned while transmitting. It
  * filters the samples, keeps the estimate while the GPS sleeps, and retunes
  * when the estimate has moved by PPS_DISC_RETUNE_PPB.
  *
  * Samples are only taken while SYSCLK comes from the TCXO (on the HSI
  * fallback the count measures the wrong oscillator). The interrupt latency
  * shows up as the occasional sample a few cycles off when a higher-priority
  * interrupt (TIM15 during APRS, SysTick) held the PPS interrupt back; the
  * trimmed mean drops those.
  ******************************************************************************
  */

#include "ppsdisc.h"
#include "main.h"
#include "si4063.h"

#define QUEUE	32
#define MAX_DEV_CYCLES	(PPS_DISC_NOMINAL / 1000000UL * PPS_DISC_MAX_PPM)

volatile uint8_t pps_disc_enable = 1;
volatile uint8_t pps_disc_valid = 0;
volatile int32_t pps_disc_ppb = 0;
volatile int32_t pps_disc_applied_ppb = 0;
volatile uint32_t pps_disc_samples = 0;
volatile uint32_t pps_disc_rejected = 0;

/* written by the PPS interrupt, read by the main loop */
static volatile uint32_t queue[QUEUE];
static volatile uint32_t queue_head = 0;
static uint32_t queue_tail = 0;

static uint32_t window[PPS_DISC_WINDOW];
static uint8_t window_n = 0, window_next = 0;

/* a / b rounded to the nearest integer, b > 0 */
static int64_t div_round(int64_t a, int64_t b) {
	return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

/* SYSCLK is the PLL fed from HSE, i.e. the Si4063 TCXO */
static uint8_t on_tcxo(void) {
	return (RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL
			&& (RCC->CFGR & RCC_CFGR_PLLSRC);
}

/*
 * pps_disc_sample
 *
 * call from the PPS interrupt: cycles = DWT cycles since the previous pulse,
 * ms = HAL ticks since the previous pulse
 */
void pps_disc_sample(uint32_t cycles, uint32_t ms) {
	uint32_t dev = cycles > PPS_DISC_NOMINAL ? cycles - PPS_DISC_NOMINAL
			: PPS_DISC_NOMINAL - cycles;

	/* the first pulse after a GPS wake-up and missed pulses fail the tick
	 * check even when the 32-bit cycle count wrapped onto a plausible value */
	if (ms < 1000 - PPS_DISC_TICK_TOL_MS || ms > 1000 + PPS_DISC_TICK_TOL_MS
			|| dev > MAX_DEV_CYCLES || !on_tcxo()) {
		pps_disc_rejected++;
		return;
	}
	queue[queue_head % QUEUE] = cycles;
	queue_head++;
}

/* trimmed mean of the window, in ppb */
static int32_t estimate(void) {
	uint32_t s[PPS_DISC_WINDOW];
	int64_t sum = 0;

	for (int i = 0; i < PPS_DISC_WINDOW; i++)
		s[i] = window[i];
	for (int i = 1; i < PPS_DISC_WINDOW; i++)
		for (int j = i; j > 0 && s[j - 1] > s[j]; j--) {
			uint32_t t = s[j];
			s[j] = s[j - 1];
			s[j - 1] = t;
		}
	for (int i = PPS_DISC_TRIM; i < PPS_DISC_WINDOW - PPS_DISC_TRIM; i++)
		sum += (int64_t)s[i] - PPS_DISC_NOMINAL;
	/* mean cycles off nominal * 1e9 / 24e6 = * 125 / 3 */
	return (int32_t)div_round(sum * 125, 3 * (PPS_DISC_WINDOW - 2 * PPS_DISC_TRIM));
}

/*
 * pps_disc_freq
 *
 * the frequency to program so that the carrier lands on freq_hz, with the
 * correction currently applied
 */
uint32_t pps_disc_freq(uint32_t freq_hz) {
	return (uint32_t)((int64_t)freq_hz
			- div_round((int64_t)freq_hz * pps_disc_applied_ppb, 1000000000LL));
}

/*
 * pps_disc_update
 *
 * call from the main loop, between transmissions
 */
void pps_disc_update(void) {
	/* queue_head only grows in the interrupt; a wrapped queue loses samples */
	uint32_t head = queue_head;
	if (head - queue_tail > QUEUE)
		queue_tail = head - QUEUE;
	for (; queue_tail != head; queue_tail++) {
		window[window_next] = queue[queue_tail % QUEUE];
		window_next = (window_next + 1) % PPS_DISC_WINDOW;
		if (window_n < PPS_DISC_WINDOW)
			window_n++;
		pps_disc_samples++;
		if (window_n == PPS_DISC_WINDOW) {
			pps_disc_ppb = estimate();
			pps_disc_valid = 1;
		}
	}

	int32_t want = pps_disc_enable && pps_disc_valid ? pps_disc_ppb : 0;
	int32_t moved = want - pps_disc_applied_ppb;
	if (moved >= PPS_DISC_RETUNE_PPB || moved <= -PPS_DISC_RETUNE_PPB
			|| (want == 0 && pps_disc_applied_ppb != 0)) {
		pps_disc_applied_ppb = want;
		si4060_set_frequency(pps_disc_freq(RF_FREQ_HZ_DFM));
	}
}

/* forget all samples and the estimate (tests) */
void pps_disc_reset(void) {
	queue_tail = queue_head;
	window_n = window_next = 0;
	pps_disc_valid = 0;
	pps_disc_ppb = 0;
	pps_disc_applied_ppb = 0;
	pps_disc_samples = pps_disc_rejected = 0;
	pps_disc_enable = 1;
}
