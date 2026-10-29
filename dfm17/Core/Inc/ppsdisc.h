/**
  ******************************************************************************
  * @file    ppsdisc.h
  * @brief   This file contains all the function prototypes for
  *          the ppsdisc.c file
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
  */

#ifndef INC_PPSDISC_H_
#define INC_PPSDISC_H_

#include <inttypes.h>

/* CPU cycles per second when SYSCLK is exactly 24 MHz (TCXO 25.6 MHz / 2
 * = 12.8 MHz DIV_CLK, / 8 * 15 in the PLL) */
#define PPS_DISC_NOMINAL		24000000UL

/* samples further than this from nominal are rejected (the TCXO is about
 * -1.4 ppm; a missed or extra pulse is +-1e6 ppm) */
#define PPS_DISC_MAX_PPM		10

/* the time the HAL tick saw between the pulses must be 1 s within this */
#define PPS_DISC_TICK_TOL_MS	50

/* the estimate is a trimmed mean of the last PPS_DISC_WINDOW good samples:
 * the PPS_DISC_TRIM highest and lowest are dropped (a pulse whose interrupt
 * was held off by a higher-priority one) */
#define PPS_DISC_WINDOW			16
#define PPS_DISC_TRIM			4

/* the synthesizer is retuned when the estimate has moved this far */
#define PPS_DISC_RETUNE_PPB		10

/* SWD-visible state (docs/rf-measurements.md) */
extern volatile uint8_t pps_disc_enable;	/* 0: transmit on the nominal frequency */
extern volatile uint8_t pps_disc_valid;		/* a full window of samples */
extern volatile int32_t pps_disc_ppb;		/* TCXO error, + = fast */
extern volatile int32_t pps_disc_applied_ppb;	/* correction in the synthesizer */
extern volatile uint32_t pps_disc_samples;	/* accepted */
extern volatile uint32_t pps_disc_rejected;

void pps_disc_sample(uint32_t cycles, uint32_t ms);
void pps_disc_update(void);
uint32_t pps_disc_freq(uint32_t freq_hz);
void pps_disc_reset(void);

#endif /* INC_PPSDISC_H_ */
