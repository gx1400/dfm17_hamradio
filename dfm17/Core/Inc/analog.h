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

/* PA0 is the supply input through a divider: Vin = V(PA0) * ANALOG_VIN_DIVIDER.
 * 2.0 is a guess (2.33 V on PA0 from about 4.7 V); confirm against a meter,
 * see docs/hardware-investigation.md */
#define ANALOG_VIN_DIVIDER		2.0f

/* PB1 is the supply current monitor, about 1 V per A */
#define ANALOG_I_MA_PER_V		1000.0f

typedef struct {
	uint16_t vin_raw, iin_raw, vref_raw;	/* 12-bit averages, for calibration */
	uint16_t vdda_mv;		/* from the internal reference */
	uint16_t vin_mv;		/* supply input, PA0 */
	uint16_t iin_ma;		/* supply current, PB1 */
	int16_t temp_c;			/* STM32 internal sensor, +-several C */
} analog_reading;

/* refreshed every second by the main loop, for reading over SWD
 * (tools/hw_survey/read_analog.sh) */
extern analog_reading analog_last;

void analog_init(void);
void analog_read(analog_reading *r);

#endif /* INC_ANALOG_H_ */
