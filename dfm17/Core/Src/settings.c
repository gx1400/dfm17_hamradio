/**
  ******************************************************************************
  * @file    settings.c
  * @brief   Station settings: defaults, validation, and the transmit rule
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
  * The settings live in the NFC EEPROM (settings_store.c loads them). Anything
  * that fails settings_check() is ignored and the built-in defaults are used.
  * The defaults carry the callsign N0CALL, and nothing transmits until a real
  * callsign is set (settings_tx_allowed(), checked in si4060_start_tx()), so
  * a fresh build never goes on the air under someone else's callsign.
  *
  * No hardware access here, so the host tests can link it.
  ******************************************************************************
  */

#include "settings.h"

settings_t cfg;
volatile uint8_t settings_source = SETTINGS_FROM_DEFAULTS;
volatile uint8_t settings_error = SETTINGS_OK;

/*
 * settings_defaults
 *
 * the built-in settings: today's tracker (or fox, built with -DFOX) on
 * 446.175 MHz, but with the callsign N0CALL, so it doesn't transmit
 */
void settings_defaults(settings_t *s) {
	uint8_t *p = (uint8_t *)s;
	for (unsigned i = 0; i < sizeof(*s); i++)
		p[i] = 0;
	s->magic[0] = 'D';
	s->magic[1] = 'F';
	s->version = SETTINGS_VERSION;
	s->length = SETTINGS_SIZE;
	const char call[6] = {'N', '0', 'C', 'A', 'L', 'L'};
	for (int i = 0; i < 6; i++)
		s->callsign[i] = call[i];
	s->ssid = 12;
	s->symbol_table = '/';
	s->symbol_code = 'O';
	s->path = 0x11;
#ifdef FOX
	s->mode = MODE_FOX;
#else
	s->mode = MODE_TRACKER;
#endif
	s->flags = 0;
	s->freq_hz = 446175000UL;
	s->pa_level = 0x20;
	s->cw_wpm = 15;
	s->dev_hz = 2600;
	s->tracker_period_s = 300;
	s->tracker_fix_timeout_s = 120;
	s->tracker_settle_s = 10;
	s->fox_period_s = 30;
	s->fox_tone_ms = 10000;
	s->fox_aprs_every = 10;
	s->gps_power = GPS_POWER_DEFAULT;
	for (unsigned i = 0; i < sizeof(s->reserved); i++)
		s->reserved[i] = 0xFF;
	s->crc = settings_crc(s);
}

/*
 * settings_crc
 *
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) of everything before the crc
 * field; Python: binascii.crc_hqx(data, 0xFFFF)
 */
uint16_t settings_crc(const settings_t *s) {
	const uint8_t *p = (const uint8_t *)s;
	uint16_t crc = 0xFFFF;
	for (unsigned i = 0; i < offsetof(settings_t, crc); i++) {
		crc ^= (uint16_t)p[i] << 8;
		for (int b = 0; b < 8; b++)
			crc = crc & 0x8000 ? (uint16_t)(crc << 1) ^ 0x1021 : (uint16_t)(crc << 1);
	}
	return crc;
}

/* fill in magic, version, length and crc after editing the fields */
void settings_seal(settings_t *s) {
	s->magic[0] = 'D';
	s->magic[1] = 'F';
	s->version = SETTINGS_VERSION;
	s->length = SETTINGS_SIZE;
	s->crc = settings_crc(s);
}

static uint8_t is_upper(char c) {
	return c >= 'A' && c <= 'Z';
}

static uint8_t is_digit(char c) {
	return c >= '0' && c <= '9';
}

/* 3-6 of A-Z 0-9 with at least one letter and one digit, then NUL padding */
static uint8_t callsign_ok(const char *c) {
	int n = 0, letters = 0, digits = 0;
	while (n < 6 && c[n]) {
		if (is_upper(c[n]))
			letters++;
		else if (is_digit(c[n]))
			digits++;
		else
			return 0;
		n++;
	}
	for (int i = n; i < 6; i++)
		if (c[i])
			return 0;
	return n >= 3 && letters && digits;
}

/*
 * settings_check
 *
 * returns SETTINGS_OK, or why the block can't be used. The limits keep a bad
 * or half-written block from putting the transmitter out of band, keying it
 * continuously, or identifying with garbage.
 */
enum settings_error settings_check(const settings_t *s) {
	if (s->magic[0] != 'D' || s->magic[1] != 'F')
		return SETTINGS_ERR_MAGIC;
	if (s->version != SETTINGS_VERSION || s->length != SETTINGS_SIZE)
		return SETTINGS_ERR_VERSION;
	if (s->crc != settings_crc(s))
		return SETTINGS_ERR_CRC;
	if (!callsign_ok(s->callsign))
		return SETTINGS_ERR_CALLSIGN;
	if (s->ssid > 15)
		return SETTINGS_ERR_SSID;
	if (!(s->symbol_table == '/' || s->symbol_table == '\\' || is_upper(s->symbol_table))
			|| s->symbol_code < '!' || s->symbol_code > '~')
		return SETTINGS_ERR_SYMBOL;
	uint8_t n = s->path >> 4, hops = s->path & 0x0F;
	if (n < 1 || n > 2 || hops < 1 || hops > n)
		return SETTINGS_ERR_PATH;
	if (s->mode > MODE_FOX)
		return SETTINGS_ERR_MODE;
	if (s->freq_hz < 420000000UL || s->freq_hz > 450000000UL)
		return SETTINGS_ERR_FREQ;
	if (s->pa_level > 0x7F)
		return SETTINGS_ERR_POWER;
	if (s->cw_wpm < 5 || s->cw_wpm > 40)
		return SETTINGS_ERR_CW;
	if (s->dev_hz < 1000 || s->dev_hz > 5000)
		return SETTINGS_ERR_DEVIATION;
	if (s->tracker_period_s < 30 || s->tracker_fix_timeout_s < 10
			|| s->tracker_fix_timeout_s >= s->tracker_period_s
			|| s->tracker_settle_s >= s->tracker_fix_timeout_s)
		return SETTINGS_ERR_TRACKER_TIMING;
	/* the tone plus a few seconds of ID and margin must fit in the period */
	if (s->fox_period_s < 10 || s->fox_tone_ms > 60000
			|| s->fox_tone_ms + 8000UL > s->fox_period_s * 1000UL)
		return SETTINGS_ERR_FOX_TIMING;
	if (s->gps_power != GPS_POWER_DUTY_CYCLE && s->gps_power != GPS_POWER_ALWAYS_ON
			&& s->gps_power != GPS_POWER_DEFAULT)
		return SETTINGS_ERR_GPS_POWER;
	return SETTINGS_OK;
}

/* a real callsign, not the N0CALL placeholder */
uint8_t settings_callsign_set(const settings_t *s) {
	static const char placeholder[6] = {'N', '0', 'C', 'A', 'L', 'L'};
	for (int i = 0; i < 6; i++)
		if (s->callsign[i] != placeholder[i])
			return callsign_ok(s->callsign);
	return 0;
}

/* the live callsign as a C string (without the SSID) */
const char *settings_callsign(void) {
	static char call[7];
	for (int i = 0; i < 6; i++)
		call[i] = cfg.callsign[i];
	call[6] = 0;
	return call;
}

/*
 * settings_tx_allowed
 *
 * whether the transmitter may be keyed: only with a real callsign and
 * settings that passed settings_check()
 */
uint8_t settings_tx_allowed(void) {
	return settings_check(&cfg) == SETTINGS_OK && settings_callsign_set(&cfg);
}

/* keep the GPS on between tracker fixes */
uint8_t settings_gps_always_on(void) {
	return cfg.gps_power == GPS_POWER_ALWAYS_ON;
}
