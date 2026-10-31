/**
  ******************************************************************************
  * @file    fox.h
  * @brief   This file contains all the function prototypes for
  *          the fox.c file
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

#ifndef INC_FOX_H_
#define INC_FOX_H_

#include "GNSS.h"

/* the callsign (Morse ID at the start of every cycle), CW speed, hunt tone
 * length, period and how often to send APRS come from the settings (cfg,
 * settings.h). APRS gives the fox's location away to anyone decoding it: set
 * fox_aprs_every to 0 for an actual hunt */

/* audio tone for both the Morse ID and the hunt tone:
 * TIM15 tick 26402.6 Hz / (2 * 16) = 825 Hz */
#define FOX_TONE_HALF_TICKS	16

/* button test transmissions: unmodulated carrier for measuring the frequency,
 * and a 1200 Hz tone (26402.6 Hz / (2 * 11)) for checking deviation */
#define FOX_TEST_MS			5000
#define FOX_TEST_TONE_HALF_TICKS	11

/* boot with the fox paused (no transmissions until a long button press) */
//#define FOX_START_PAUSED

void fox_transmit(GNSS_StateHandle *GNSS);
void fox_test_carrier(void);
void fox_test_tone_aprs(GNSS_StateHandle *GNSS);
void fox_send_now(GNSS_StateHandle *GNSS);

#endif /* INC_FOX_H_ */
