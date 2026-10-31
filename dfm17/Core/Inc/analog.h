/**
  ******************************************************************************
  * @file    analog.h
  * @brief   This file contains all the function prototypes for
  *          the analog.c file
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

#ifndef INC_ANALOG_H_
#define INC_ANALOG_H_

#include <inttypes.h>

/* PA0 (supply input through a divider and a diode), PB1 (supply current
 * monitor, 1 V/A) and PC2 (mini-USB VBUS through a divider) are scaled with
 * each board's calibration factors, a * V(pin) + b (cal.h, #43) */

/* USB power is present when PC2 itself reads above this; on battery it reads
 * near 0, so this works whatever the divider turns out to be */
#define ANALOG_USB_PRESENT_MV	500

typedef struct {
	uint16_t vin_raw, iin_raw, vref_raw;	/* 12-bit averages, for calibration */
	uint16_t vdda_mv;		/* from the internal reference */
	uint16_t vin_mv;		/* supply input, PA0 */
	uint16_t iin_ma;		/* supply current, PB1 */
	int16_t temp_c;			/* STM32 internal sensor, +-several C */
	uint16_t usb_raw;		/* 12-bit average, PC2 */
	uint16_t usb_mv;		/* USB VBUS */
	uint8_t on_usb;			/* USB power present */
} analog_reading;

/* refreshed every second by the main loop, for reading over SWD
 * (tools/hw_survey/read_analog.sh) */
extern analog_reading analog_last;

void analog_init(void);
void analog_read(analog_reading *r);
void analog_convert(analog_reading *r, uint32_t vref, uint32_t vin_raw,
		uint32_t iin_raw, uint32_t temp_raw, uint32_t usb_raw);

#endif /* INC_ANALOG_H_ */
