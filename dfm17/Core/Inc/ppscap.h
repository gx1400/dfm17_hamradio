/**
  ******************************************************************************
  * @file    ppscap.h
  * @brief   This file contains all the function prototypes for
  *          the ppscap.c file
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

#ifndef INC_PPSCAP_H_
#define INC_PPSCAP_H_


#include <stdint.h>

/* where pps_disc_sample() gets its PPS intervals: the TIM4 input capture
 * (exact, works while the CPU sleeps) or the DWT cycle counter read in the
 * PPS interrupt (late by the interrupt latency; stops in sleep) */
enum pps_source { PPS_SOURCE_DWT = 0, PPS_SOURCE_CAPTURE = 1 };
extern volatile uint8_t pps_source;

/* proof-out (#12): both measurements of the last PPS_LOG pulses, over SWD */
#define PPS_LOG 32
typedef struct {
	uint32_t dwt;	  /* DWT cycles since the previous pulse */
	uint32_t capture; /* TIM4 capture interval, 0 if none */
} pps_log_entry;
extern volatile pps_log_entry pps_log[PPS_LOG];
extern volatile uint32_t pps_log_head; /* pulses logged; entry = head % PPS_LOG */

extern volatile uint32_t pps_cap_overcaptures; /* an edge was missed (CC3OF) */
extern volatile uint32_t pps_cap_missing;	   /* no capture at a PPS interrupt */

/* test only: sleep (WFI) in the main loop, debug clocks off in sleep */
extern volatile uint8_t pps_sleep_test;

void pps_capture_init(void);
uint32_t pps_capture_interval(uint16_t prev, uint16_t now);
uint8_t pps_capture_read(uint32_t *cycles);
void pps_log_add(uint32_t dwt, uint32_t capture);

#endif /* INC_PPSCAP_H_ */
