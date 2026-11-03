/**
  ******************************************************************************
  * @file    rtty_hw.c
  * @brief   RTTY on the Si4063: FSK from GPIO3, timed by the cycle counter
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
  * The radio is in sync direct 2GFSK with GPIO3 as the data input (initRadio,
  * si4060_setup): it samples GPIO3 at SI_TX_RATE (13201 Hz) and smooths the
  * edges with its TX filter, which only rounds them at RTTY bit rates (#54).
  * GPIO3 high and low are the carrier +/- the deviation. With the deviation
  * set to half the shift, holding GPIO3 for a bit time sends that bit: true
  * FSK, received on SSB (USB), not FM.
  *
  * Bit edges are counted from the key-up in SYSCLK cycles (DWT CYCCNT), each
  * from the start rather than from the previous edge, so the baud rate is as
  * exact as the TCXO and never drifts. The main loop is busy for the whole
  * message, as for the fox's Morse; interrupts only add a few us of jitter.
  ******************************************************************************
  */

#include "rtty.h"
#include "main.h"
#include "diag.h"
#include "gpio.h"
#include "settings.h"
#include "si4063.h"
#include "tim.h"

/* SYSCLK from the Si4063's TCXO (switchToRadioClock in init.c) */
#define RTTY_SYSCLK_HZ 24000000ULL

/* GPIO3 level for mark (the higher frequency): 1 if GPIO3 high gives the
 * carrier + deviation. Checked on board 1 (docs/tx-test-modes.md) */
#ifndef RTTY_MARK_GPIO3
#define RTTY_MARK_GPIO3 1
#endif

_Static_assert(RTTY_DEV_STEP_MHZ == 2ULL * XO_FREQ * 1000 / (1UL << 19) / 8,
		"RTTY_DEV_STEP_MHZ doesn't match XO_FREQ");

static uint32_t start;
static uint32_t halves_sent;
static uint16_t baud;
static uint8_t mark_level;

static void level(uint8_t mark) {
	if ((mark ? mark_level : !mark_level))
		assertSiGPIO3();
	else
		deassertSiGPIO3();
}

void rtty_hw_begin(const rtty_params_t *p) {
	baud = p->baud_x100;
	mark_level = RTTY_MARK_GPIO3 ^ (p->reverse ? 1 : 0);

	/* nothing else may drive GPIO3: no tone, no AFSK tick */
	stopAprsTickTimer();
	tone_half_ticks = 0;
	si4060_set_deviation(rtty_deviation_hz(p->shift_hz));
	level(1);
	diag_set_tx(1);
	si4060_start_tx(0);
	halves_sent = 0;
	start = DWT->CYCCNT;
}

void rtty_hw_bits(uint8_t mark, uint16_t halves) {
	level(mark);
	halves_sent += halves;
	uint32_t end = start + (uint32_t)(halves_sent * RTTY_SYSCLK_HZ * 50 / baud);
	while ((int32_t)(DWT->CYCCNT - end) < 0)
		;
}

void rtty_hw_end(void) {
	si4060_stop_tx();
	deassertSiGPIO3();
	si4060_set_deviation(cfg.dev_hz);
	diag_set_tx(0);
}
