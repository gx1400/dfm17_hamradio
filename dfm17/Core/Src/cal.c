/**
  ******************************************************************************
  * @file    cal.c
  * @brief   Per-board analog calibration factors (#43)
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
  * Each analog channel is value = a * x + b, with x the pin voltage in mV
  * after VDDA compensation, a in Q16 fixed point and b in mV or mA. Boards
  * that haven't been calibrated use the defaults, board 1's bench fits
  * (#14, #43). A stored channel outside the limits falls back to its own
  * default, so a bad block can't produce nonsense readings.
  *
  * Loading from and saving to the EEPROM, and the SWD commands, are in
  * settings_store.c. No hardware access here, so the host tests can link it.
  ******************************************************************************
  */

#include "cal.h"
#include "settings.h"

/* loaded by settings_init(), before the main loop reads the ADC */
cal_t cal;
volatile uint8_t cal_source = CAL_FROM_DEFAULTS;
volatile uint8_t cal_error = CAL_OK;

/* board 1 (#14, #43): Vin = 2.0 x + 208 mV (the input diode), Iin = 1.0 x +
 * 4 mA (1 V/A, current drawn ahead of the sense point), VBUS = 2.02 x */
static const cal_channel_t defaults[CAL_CHANNELS] = {
		[CAL_VIN] = {131072, 208},
		[CAL_IIN] = {65536, 4},
		[CAL_USB] = {132383, 0},
};

/*
 * cal_defaults
 *
 * the built-in factors, none of them measured
 */
void cal_defaults(cal_t *c) {
	uint8_t *p = (uint8_t *)c;
	for (unsigned i = 0; i < sizeof(*c); i++)
		p[i] = 0xFF;
	for (int i = 0; i < CAL_CHANNELS; i++)
		c->ch[i] = defaults[i];
	c->measured = 0;
	cal_seal(c);
}

/* fill in magic, version, length and crc after editing the fields */
void cal_seal(cal_t *c) {
	c->magic[0] = 'C';
	c->magic[1] = 'A';
	c->version = CAL_VERSION;
	c->length = CAL_SIZE;
	c->crc = crc16_ccitt(c, offsetof(cal_t, crc));
}

/*
 * cal_bad_channels
 *
 * returns a bit for each channel outside the limits
 */
uint8_t cal_bad_channels(const cal_t *c) {
	uint8_t bad = 0;
	for (int i = 0; i < CAL_CHANNELS; i++) {
		int32_t a = c->ch[i].a_q16, a0 = defaults[i].a_q16, b = c->ch[i].b;
		int32_t da = a > a0 ? a - a0 : a0 - a;
		if (da > a0 / CAL_GAIN_TOLERANCE || b > CAL_MAX_OFFSET || b < -CAL_MAX_OFFSET)
			bad |= 1 << i;
	}
	return bad;
}

/*
 * cal_check
 *
 * returns CAL_OK if c is a calibration block with every channel in range
 */
enum cal_error cal_check(const cal_t *c) {
	if (c->magic[0] != 'C' || c->magic[1] != 'A')
		return CAL_ERR_MAGIC;
	if (c->version != CAL_VERSION || c->length != CAL_SIZE)
		return CAL_ERR_VERSION;
	if (c->crc != crc16_ccitt(c, offsetof(cal_t, crc)))
		return CAL_ERR_CRC;
	return cal_bad_channels(c) ? CAL_ERR_RANGE : CAL_OK;
}

/*
 * cal_load
 *
 * use a block read from the EEPROM. Not a valid block (magic, version, CRC):
 * nothing changes. Otherwise each channel outside the limits gets its
 * default (and isn't marked measured). returns the cal_check() result
 */
enum cal_error cal_load(const cal_t *c) {
	enum cal_error err = cal_check(c);
	if (err != CAL_OK && err != CAL_ERR_RANGE)
		return err;
	cal_t use = *c;
	uint8_t bad = cal_bad_channels(c);
	for (int i = 0; i < CAL_CHANNELS; i++) {
		if (bad & (1 << i)) {
			use.ch[i] = defaults[i];
			use.measured &= ~(1 << i);
		}
	}
	cal_seal(&use);
	cal = use;
	cal_source = CAL_FROM_EEPROM;
	cal_error = err;
	return err;
}

/*
 * cal_scale
 *
 * a channel's value from its pin voltage x in mV, rounded, held to 0-65535
 */
uint16_t cal_scale(enum cal_channel ch, uint32_t x_mv) {
	const cal_channel_t *k = &cal.ch[ch];
	/* a pin can't be above VDDA (3.6 V max); held to 8191 mV the product
	 * stays in 32 bits for any gain in range (at most 2.53 x 65536) */
	int32_t x = x_mv > 8191 ? 8191 : (int32_t)x_mv;
	int32_t v = ((k->a_q16 * x + 0x8000) >> 16) + k->b;
	if (v < 0)
		return 0;
	return v > 65535 ? 65535 : (uint16_t)v;
}
