/**
  ******************************************************************************
  * @file    ukhas.h
  * @brief   UKHAS telemetry sentences, for RTTY (#53)
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

#ifndef INC_UKHAS_H_
#define INC_UKHAS_H_

#include <stdint.h>

/* one sentence's data; the caller fills it from the GPS (or the last fix)
 * and the analog readings */
typedef struct {
	uint16_t seq;			/* sentence counter */
	uint8_t hour, min, sec; /* UTC */
	int32_t lat_e7, lon_e7; /* degrees * 1e7, as u-blox NAV-PVT */
	int32_t alt_m;			/* above mean sea level */
	uint8_t sats;			/* 0: no fix (the position is the last one, or 0) */
	int16_t temp_c;
	uint16_t vin_mv;
} ukhas_data_t;

/* the longest sentence (a 9-character callsign, every field at its
 * extreme, test_ukhas.c) is 82 characters plus the NUL */
#define UKHAS_MAX 96

uint8_t ukhas_sentence(char *buf, uint8_t size, const char *call, const ukhas_data_t *d);

#endif /* INC_UKHAS_H_ */
