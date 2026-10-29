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
  * packet and puts the GPS back into software backup until the next cycle. With
  * a poor sky view (a parked car) the fix can come and go; at the timeout the
  * last 3D fix seen during the cycle is sent, if there was one.
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
/* the last 3D fix of this cycle */
static GNSS_StateHandle last_fix;
static uint8_t have_last_fix;

/*
 * tracker_start
 *
 * starts the first cycle; the GPS is awake after initHw()
 */
void tracker_start(void) {
	state = TRK_ACQUIRE;
	cycle_start = HAL_GetTick();
	fixing = 0;
	had_fix = 0;
	have_last_fix = 0;
}

/* returns 1 if the packet carried a position */
static uint8_t send_aprs(GNSS_StateHandle *GNSS) {
	uint8_t position;

	diag_set_tx(1);
	if (fixing || !have_last_fix)
		position = aprs_prepare_buffer(GNSS, 0);
	else
		position = aprs_prepare_fix(&last_fix, 1, 0);
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
			/* GNSS is only updated by gpsUpdate() in the main loop, so this
			 * copy can't catch a half-parsed NAV-PVT */
			last_fix.fixType = GNSS->fixType;
			last_fix.numSV = GNSS->numSV;
			last_fix.lat = GNSS->lat;
			last_fix.lon = GNSS->lon;
			last_fix.hMSL = GNSS->hMSL;
			last_fix.day = GNSS->day;
			last_fix.hour = GNSS->hour;
			last_fix.min = GNSS->min;
			have_last_fix = 1;
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
			have_last_fix = 0;
			state = TRK_ACQUIRE;
		}
		break;
	}
}
