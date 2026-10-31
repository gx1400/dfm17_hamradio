/**
  ******************************************************************************
  * @file    cal.h
  * @brief   This file contains all the function prototypes for
  *          the cal.c file
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

#ifndef INC_CAL_H_
#define INC_CAL_H_

#include <stddef.h>
#include <stdint.h>

/* per-board analog calibration (#43) in the NFC EEPROM, user memory 0x160
 * (sector 2: writable over I2C, but not open to a phone like sector 3).
 * Separate from the settings block, so saving or resetting the settings
 * never touches it: it describes the hardware, not the station.
 * docs/settings.md has the map; tools/config/dfmcfg.py `cal` reads and
 * writes it over SWD. */
#define CAL_EE_ADDR 0x0160
#define CAL_VERSION 1
#define CAL_SIZE	32

/* each channel: value = a * x + b, x the pin voltage in mV after VDDA
 * compensation (analog_convert()) */
enum cal_channel {
	CAL_VIN = 0, /* supply input PA0 -> vin_mv */
	CAL_IIN,	 /* supply current PB1 -> iin_ma */
	CAL_USB,	 /* USB VBUS PC2 -> usb_mv */
	CAL_CHANNELS,
};

typedef struct __attribute__((packed)) {
	int32_t a_q16; /* gain, Q16 (65536 = 1.0) */
	int16_t b;	   /* offset, mV or mA */
} cal_channel_t;

typedef struct __attribute__((packed)) {
	char magic[2];					/* "CA" */
	uint8_t version;				/* CAL_VERSION */
	uint8_t length;					/* CAL_SIZE */
	cal_channel_t ch[CAL_CHANNELS]; /* enum cal_channel */
	uint8_t measured;				/* bit n: channel n measured on this board */
	uint8_t reserved1;				/* 0xFF */
	uint32_t date;					/* YYYYMMDD of the calibration, 0xFFFFFFFF none */
	uint8_t reserved[2];			/* 0xFF */
	uint16_t crc;					/* CRC-16/CCITT-FALSE of the bytes before it */
} cal_t;

/* the layout is shared with tools/config/dfmcfg.py: pin it */
_Static_assert(sizeof(cal_t) == CAL_SIZE, "cal_t size");
_Static_assert(sizeof(cal_channel_t) == 6, "cal_channel_t size");
_Static_assert(offsetof(cal_t, ch) == 4, "cal_t layout");
_Static_assert(offsetof(cal_t, measured) == 22, "cal_t layout");
_Static_assert(offsetof(cal_t, date) == 24, "cal_t layout");
_Static_assert(offsetof(cal_t, crc) == CAL_SIZE - 2, "cal_t layout");

/* limits a stored channel must meet, else its default is used: gain within
 * 1/CAL_GAIN_TOLERANCE (25%) of the default, offset within CAL_MAX_OFFSET */
#define CAL_GAIN_TOLERANCE 4
#define CAL_MAX_OFFSET	   500

/* why a calibration block was refused (cal_check) */
enum cal_error {
	CAL_OK = 0,
	CAL_ERR_MAGIC, /* not a calibration block (e.g. erased: FF) */
	CAL_ERR_VERSION,
	CAL_ERR_CRC,
	CAL_ERR_RANGE,	/* a channel outside the limits */
	CAL_ERR_EEPROM, /* the EEPROM didn't answer */
};

/* where the factors in use came from */
enum cal_source {
	CAL_FROM_DEFAULTS = 0, /* nothing valid in the EEPROM */
	CAL_FROM_EEPROM,	   /* (channels out of range: their defaults, cal_error RANGE) */
	CAL_FROM_SWD,		   /* applied over SWD, not saved yet */
};

/* the factors in use: always within the limits */
extern cal_t cal;
extern volatile uint8_t cal_source;
extern volatile uint8_t cal_error; /* enum cal_error of the last load or apply */

void cal_defaults(cal_t *c);
void cal_seal(cal_t *c);
uint8_t cal_bad_channels(const cal_t *c);
enum cal_error cal_check(const cal_t *c);
enum cal_error cal_load(const cal_t *c);
uint16_t cal_scale(enum cal_channel ch, uint32_t x_mv);

#endif /* INC_CAL_H_ */
