/**
  ******************************************************************************
  * @file    foxgps.c
  * @brief   the fox's GPS power policy: a fix, then backup, an hourly resync
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
  * A fox box doesn't move: it needs its position (for APRS) and the time,
  * not a continuous fix. The GPS is about 22 mA of the board's ~54 mA idle
  * (#74), so the fox keeps it in software backup except to refresh:
  *
  * - awake until a 3D fix has held for tracker_settle_s, then backup. Until
  *   the first fix since boot it stays on, however long that takes: a cold
  *   start needs uninterrupted signal to download ephemeris (see tracker.c);
  * - every FOX_GPS_RESYNC_S it wakes again, and goes back to backup at the
  *   settled fix, or after tracker_fix_timeout_s keeping the previous fix;
  * - a sleep command the GPS didn't take shows as a PPS that keeps coming
  *   (ppsLockStatus); it's sent again, up to FOX_GPS_SLEEP_TRIES times.
  *
  * gps_power=always_on keeps the GPS awake throughout, as in the tracker.
  * While it sleeps, the fox's APRS packets use the last fix.
  ******************************************************************************
  */

#include "foxgps.h"
#include "main.h"
#include "gps.h"
#include "settings.h"
#include <stdio.h>
#include <stddef.h>

enum fox_gps_state { FG_AWAKE, FG_ASLEEP };

static enum fox_gps_state state;
static uint32_t since;	   /* woke, or went to sleep */
static uint32_t fix_start; /* the current 3D fix began */
static uint8_t fixing;
static uint32_t slept_at; /* the last sleep command */
static uint8_t sleep_tries;
static GNSS_StateHandle last_fix;
static uint8_t have_last_fix;

/*
 * fox_gps_start
 *
 * from the fox's start: the GPS is awake after power-up
 */
void fox_gps_start(void) {
	state = FG_AWAKE;
	since = HAL_GetTick();
	fixing = 0;
	have_last_fix = 0;
}

static void go_to_sleep(uint32_t now) {
	gpsSleep();
	slept_at = now;
	sleep_tries = 1;
	since = now;
	state = FG_ASLEEP;
	printf("fox: GPS to backup\r\n");
}

static void wake(uint32_t now) {
	gpsWake();
	since = now;
	fixing = 0;
	state = FG_AWAKE;
}

/*
 * fox_gps_poll
 *
 * the policy; call it from the main loop while the fox runs
 */
void fox_gps_poll(GNSS_StateHandle *GNSS, uint32_t now) {
	switch (state) {
	case FG_AWAKE:
		if (ppsLockStatus && GNSS->fixType >= Fix3D) {
			if (!fixing) {
				fixing = 1;
				fix_start = now;
			}
			/* only gpsUpdate() in the main loop writes GNSS: no half-parsed copy */
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
		if (settings_gps_always_on())
			break;
		if ((fixing && now - fix_start >= cfg.tracker_settle_s * 1000UL)
				|| (have_last_fix && now - since >= cfg.tracker_fix_timeout_s * 1000UL))
			go_to_sleep(now);
		break;
	case FG_ASLEEP:
		if (settings_gps_always_on() || now - since >= FOX_GPS_RESYNC_S * 1000UL) {
			wake(now);
		} else if (ppsLockStatus && sleep_tries < FOX_GPS_SLEEP_TRIES
				&& now - slept_at >= FOX_GPS_SLEEP_CHECK_MS) {
			/* still pulsing: the command was lost */
			gpsSleep();
			slept_at = now;
			sleep_tries++;
			printf("fox: GPS still awake, backup again (%u)\r\n", sleep_tries);
		}
		break;
	}
}

GNSS_StateHandle *fox_gps_last_fix(void) {
	return have_last_fix ? &last_fix : NULL;
}

uint8_t fox_gps_asleep(void) {
	return state == FG_ASLEEP;
}
