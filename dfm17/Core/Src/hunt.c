/**
  ******************************************************************************
  * @file    hunt.c
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
  * The box can be placed and powered up ahead of the hunt. Until the hunt
  * starts it sends nothing on its schedule (the button tests and send-now
  * still work), and the shrinking position dither (posdither.c) runs on the
  * hunt's clock, not on the uptime:
  *   boot   - at power-up (the default, and an erased setting)
  *   time   - at hunt_start_utc, from GPS time. Booted after it, the hunt is
  *            already running: the clock counts from hunt_start_utc. With no
  *            GPS time HUNT_NO_TIME_MS after boot, it starts anyway
  *   button - the box boots paused; the first resume (a 3-10 s press, or
  *            "fox start" on the console) starts it
  * Once started, the hunt runs until the next boot.
  ******************************************************************************
  */

#include "hunt.h"

static uint8_t started;
static uint32_t start_tick; /* HAL tick the hunt clock counts from */
static uint32_t boot_tick;
static enum hunt_state state = HUNT_STARTED;

void hunt_init(uint32_t now) {
	started = 0;
	boot_tick = now;
	start_tick = now;
	state = HUNT_WAIT_TIME;
}

uint8_t hunt_starts_paused(const settings_t *s) {
	return s->hunt_start_mode == HUNT_START_BUTTON;
}

/* days since 1970-01-01 of a Gregorian date (Howard Hinnant's days_from_civil) */
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d) {
	y -= m <= 2;
	int32_t era = (y >= 0 ? y : y - 399) / 400;
	uint32_t yoe = (uint32_t)(y - era * 400);
	uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int32_t)doe - 719468;
}

uint32_t hunt_unix_from_date(
		uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec) {
	if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31)
		return 0;
	return (uint32_t)days_from_civil(year, month, day) * 86400UL + hour * 3600UL + min * 60UL + sec;
}

uint32_t hunt_gnss_unix(const GNSS_StateHandle *g, uint32_t now) {
	if ((g->valid & (GNSS_VALID_DATE | GNSS_VALID_TIME)) != (GNSS_VALID_DATE | GNSS_VALID_TIME))
		return 0;
	uint32_t t = hunt_unix_from_date(g->year, g->month, g->day, g->hour, g->min, g->sec);
	return t ? t + (now - g->pvtTick) / 1000 : 0;
}

static void start(uint32_t tick) {
	started = 1;
	start_tick = tick;
	state = HUNT_STARTED;
}

uint8_t hunt_poll(const settings_t *s, const GNSS_StateHandle *g, uint32_t now, uint8_t paused) {
	if (started)
		return 1;
	switch (s->hunt_start_mode) {
	case HUNT_START_BUTTON:
		state = HUNT_WAIT_BUTTON;
		if (!paused)
			start(now);
		break;
	case HUNT_START_TIME: {
		uint32_t utc = hunt_gnss_unix(g, now);
		if (utc) {
			state = HUNT_WAIT_TIME;
			/* booted late: the clock counts from the set time, as if on time
			 * (held to 10 days late, well past any shrink, so ms fit in 32 bits) */
			if (utc >= s->hunt_start_utc) {
				uint32_t late = utc - s->hunt_start_utc;
				start(now - (late < 864000UL ? late : 864000UL) * 1000);
			}
		} else {
			state = HUNT_WAIT_GPS;
			if (now - boot_tick >= HUNT_NO_TIME_MS)
				start(now);
		}
		break;
	}
	default: /* HUNT_START_BOOT, or erased */
		start(boot_tick);
		break;
	}
	return started;
}

uint8_t hunt_started(void) {
	return started;
}

enum hunt_state hunt_state_now(void) {
	return started ? HUNT_STARTED : state;
}

uint32_t hunt_elapsed_ms(uint32_t now) {
	return started ? now - start_tick : 0;
}
