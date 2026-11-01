/**
  ******************************************************************************
  * @file    ukhas.c
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
  * The sentence balloon trackers decode (dl-fldigi, SondeHub):
  *
  *   $$NZ0P,12,01:23:45,39.09970,-94.57860,250,7,30,4790*F6E9\n
  *
  * callsign, sentence number, UTC time, latitude and longitude in degrees
  * (5 decimals, about 1 m), altitude in m, satellites (0 = no fix), MCU
  * temperature in C, supply in mV. The checksum is CRC-16/CCITT-FALSE
  * (crc16_ccitt(), as the settings block) over everything between "$$" and
  * "*", in upper-case hex. A tracker's payload document lists the fields in
  * this order for the decoders.
  *
  * No printf: the numbers are written by hand, which costs far less flash.
  ******************************************************************************
  */

#include "ukhas.h"
#include "settings.h"

/* writes v in decimal, at least min_digits (zero-padded); returns the length */
static uint8_t put_uint(char *p, uint32_t v, uint8_t min_digits) {
	char tmp[10];
	uint8_t n = 0;
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v || n < min_digits);
	for (uint8_t i = 0; i < n; i++)
		p[i] = tmp[n - 1 - i];
	return n;
}

static uint8_t put_int(char *p, int32_t v) {
	if (v < 0) {
		*p = '-';
		return 1 + put_uint(p + 1, (uint32_t)-(int64_t)v, 1);
	}
	return put_uint(p, (uint32_t)v, 1);
}

/* degrees * 1e7 as degrees with 5 decimals, rounded half away from zero */
static uint8_t put_degrees(char *p, int32_t e7) {
	uint8_t n = 0;
	uint32_t a = e7 < 0 ? (uint32_t)-(int64_t)e7 : (uint32_t)e7;
	uint32_t e5 = (a + 50) / 100;
	if (e7 < 0 && e5)
		p[n++] = '-';
	n += put_uint(p + n, e5 / 100000, 1);
	p[n++] = '.';
	n += put_uint(p + n, e5 % 100000, 5);
	return n;
}

/*
 * ukhas_sentence
 *
 * writes the sentence for d into buf (size bytes, at least UKHAS_MAX is
 * plenty), NUL-terminated, ending in "\n"
 *
 * returns its length without the NUL, 0 if it doesn't fit
 */
uint8_t ukhas_sentence(char *buf, uint8_t size, const char *call, const ukhas_data_t *d) {
	static const char hex[] = "0123456789ABCDEF";
	char s[UKHAS_MAX];
	uint8_t n = 0;

	s[n++] = '$';
	s[n++] = '$';
	for (uint8_t i = 0; call[i] && i < 9; i++)
		s[n++] = call[i];
	s[n++] = ',';
	n += put_uint(&s[n], d->seq, 1);
	s[n++] = ',';
	n += put_uint(&s[n], d->hour, 2);
	s[n++] = ':';
	n += put_uint(&s[n], d->min, 2);
	s[n++] = ':';
	n += put_uint(&s[n], d->sec, 2);
	s[n++] = ',';
	n += put_degrees(&s[n], d->lat_e7);
	s[n++] = ',';
	n += put_degrees(&s[n], d->lon_e7);
	s[n++] = ',';
	n += put_int(&s[n], d->alt_m);
	s[n++] = ',';
	n += put_uint(&s[n], d->sats, 1);
	s[n++] = ',';
	n += put_int(&s[n], d->temp_c);
	s[n++] = ',';
	n += put_uint(&s[n], d->vin_mv, 1);

	uint16_t crc = crc16_ccitt(&s[2], n - 2u);
	s[n++] = '*';
	for (int8_t shift = 12; shift >= 0; shift -= 4)
		s[n++] = hex[(crc >> shift) & 0xF];
	s[n++] = '\n';

	if (n + 1u > size)
		return 0;
	for (uint8_t i = 0; i < n; i++)
		buf[i] = s[i];
	buf[n] = 0;
	return n;
}
