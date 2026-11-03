/**
  ******************************************************************************
  * @file    hunt.h
  * @brief   fox hunt start: at boot, at a set UTC time, or on the button (#72)
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

#ifndef INC_HUNT_H_
#define INC_HUNT_H_

#include <inttypes.h>
#include "GNSS.h"
#include "settings.h"

/* time mode without GPS time: start this long after boot anyway, so a box
 * that never gets a fix doesn't stay silent forever */
#define HUNT_NO_TIME_MS (60UL * 60 * 1000)

/* the hunt's state, for the console status */
enum hunt_state {
	HUNT_STARTED = 0,
	HUNT_WAIT_TIME,	  /* time mode: waiting for hunt_start_utc */
	HUNT_WAIT_GPS,	  /* time mode: no GPS time yet */
	HUNT_WAIT_BUTTON, /* button mode: paused until a long press (or "fox start") */
};

void hunt_init(uint32_t now);
/* call every main-loop pass: starts the hunt when it's due. paused: the
 * fox/tracker is paused (diag_paused()). returns 1 once the hunt has started */
uint8_t hunt_poll(const settings_t *s, const GNSS_StateHandle *g, uint32_t now, uint8_t paused);
uint8_t hunt_started(void);
enum hunt_state hunt_state_now(void);
/* ms since the hunt started (0 before): the clock the shrinking dither runs on */
uint32_t hunt_elapsed_ms(uint32_t now);
/* button mode starts paused: main.c pauses the fox/tracker at boot */
uint8_t hunt_starts_paused(const settings_t *s);

/* UTC as Unix seconds from the last NAV-PVT, moved on by the HAL tick since;
 * returns 0 if the GPS hasn't given a valid date and time */
uint32_t hunt_gnss_unix(const GNSS_StateHandle *g, uint32_t now);
uint32_t hunt_unix_from_date(
		uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec);

#endif /* INC_HUNT_H_ */
