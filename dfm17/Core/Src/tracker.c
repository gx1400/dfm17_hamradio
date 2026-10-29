/**
  ******************************************************************************
  * @file    tracker.c
  * @brief   APRS tracker: GPS on only to get a fix, one packet every 5 min
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
  * Each cycle wakes the GPS, waits until a 3D fix has held for
  * TRACKER_SETTLE_MS (or TRACKER_FIX_TIMEOUT_MS has passed), sends one APRS
  * packet and puts the GPS back into software backup until the next cycle.
  * The packet is a position with the supply voltage, current and temperature
  * telemetry, or a status packet with the same values as text when there is no
  * fix. Every packet carries the callsign as its AX.25 source address.
  *
  * Until the GPS has had its first fix it stays on: a cold start has to
  * download the almanac and ephemeris, which backup mode would interrupt.
  ******************************************************************************
  */

#include "tracker.h"
#include "main.h"
#include "aprs.h"
#include "diag.h"
#include "gps.h"
#include <stdio.h>

enum tracker_state {TRK_ACQUIRE, TRK_WAIT};

static enum tracker_state state;
/* when the GPS was last woken, the start of the cycle */
static uint32_t cycle_start;
/* start of the current run of 3D fixes */
static uint32_t fix_start;
static uint8_t fixing;
static uint8_t had_fix;

/*
 * tracker_start
 *
 * starts the first cycle; the GPS is awake after initHw()
 */
void tracker_start(void) {
	state = TRK_ACQUIRE;
	cycle_start = HAL_GetTick();
	fixing = 0;
}

/* returns 1 if the packet carried a position */
static uint8_t send_aprs(GNSS_StateHandle *GNSS) {
	uint8_t position;

	diag_set_tx(1);
	position = aprs_prepare_buffer(GNSS, 0);
	tx_aprs();
	diag_set_tx(0);
	return position;
}

/*
 * tracker_poll
 *
 * runs the tracker; call it from the main loop
 */
void tracker_poll(GNSS_StateHandle *GNSS) {
	uint32_t now = HAL_GetTick();

	switch (state) {
	case TRK_ACQUIRE:
		if (ppsLockStatus && GNSS->fixType >= Fix3D) {
			if (!fixing) {
				fixing = 1;
				fix_start = now;
			}
		} else {
			fixing = 0;
		}
		if ((fixing && now - fix_start >= TRACKER_SETTLE_MS)
				|| now - cycle_start >= TRACKER_FIX_TIMEOUT_MS) {
			uint8_t position = send_aprs(GNSS);
			if (position)
				had_fix = 1;
			printf("tracker: sent %s\r\n", position ? "position" : "status, no fix");
			if (had_fix)
				gpsSleep();
			state = TRK_WAIT;
		}
		break;
	case TRK_WAIT:
		if (now - cycle_start >= TRACKER_PERIOD_MS) {
			gpsWake();
			cycle_start = now;
			fixing = 0;
			state = TRK_ACQUIRE;
		}
		break;
	}
}
