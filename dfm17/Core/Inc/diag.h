/**
  ******************************************************************************
  * @file    diag.h
  * @brief   This file contains all the function prototypes for
  *          the diag.c file
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

#ifndef INC_DIAG_H_
#define INC_DIAG_H_

#include <inttypes.h>

/* red LED blink codes: the lowest active code is shown as that many blinks */
typedef enum {
	DIAG_ERR_RADIO = 1,			/* Si4063 not detected (PART_INFO != 0x4063) */
	DIAG_ERR_RADIO_CLOCK = 2,	/* not running from the Si4063 clock, on HSI */
	DIAG_ERR_GPS = 3,			/* GPS did not answer a request */
} diag_error;

/* button presses, classified when the button is released */
typedef enum {
	DIAG_BUTTON_NONE = 0,
	DIAG_BUTTON_SHORT,			/* under DIAG_BUTTON_MEDIUM_MS */
	DIAG_BUTTON_MEDIUM,			/* under DIAG_BUTTON_LONG_MS */
	DIAG_BUTTON_LONG,
} diag_button;

#define DIAG_BUTTON_MEDIUM_MS	1000
#define DIAG_BUTTON_LONG_MS		3000
/* held this long, a release powers the board off instead of a long press
 * (#44): handled inside diag.c, never returned by diag_get_button() */
#define DIAG_BUTTON_OFF_MS		10000

extern volatile uint32_t pps_cycles;
/* pulses kept from the discipline: GPS time not locked (#35) */
extern volatile uint32_t pps_unlocked;

void diag_selftest(void);
void diag_leds_off(void);
void diag_tick(void);
void diag_pps(void);
void diag_set_error(diag_error err);
void diag_clear_error(diag_error err);
void diag_set_tx(uint8_t on);
void diag_set_gps_fix(uint8_t fix3d);
void diag_set_paused(uint8_t paused);
diag_button diag_get_button(void);
uint8_t diag_powered_off(void);

#endif /* INC_DIAG_H_ */
