/**
  ******************************************************************************
  * @file    aprs.c
  * @brief   This file contains all functions for integrating APRS
  ******************************************************************************
  * @attention
  *
  *	Copyright (c) Stefan Biereigel
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
  * Based on code from https://github.com/bristol-seds/pico-tracker
  ******************************************************************************
  */

#include "string.h"


/* i16toa
 * 16 bit number to fixed-length output char
 */
void i16toa(uint16_t in, uint8_t len, volatile char *out) {
	uint8_t i;
	uint16_t mult = 1;
	for (i = len; i > 0; i--) {
		*(out + i - 1) = ((in % (mult*10)) / mult) + '0';
		mult *= 10;
	}
}

/* i16toa
 * 16 bit number to variable-length output char
 *
 * returns:	length of string
 */
uint8_t i16toav(uint16_t in, volatile char *out) {
	uint16_t mult = 10000;
	uint8_t cnt = 0;
	uint8_t start = 0;
	uint8_t len = 0;
	if (in == 0) {
		*out = '0';
		return 1;
	}

	while(mult > 0) {
		if (in >= mult) {
			in = in - mult;
			cnt++;
			start = 1;
		} else {
			*out = cnt + '0';
			cnt = 0;
			mult /= 10;
			if (start) {
				out++;
				len++;
			}
		}
	}

	return len;
}

