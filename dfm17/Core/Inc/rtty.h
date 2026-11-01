/**
  ******************************************************************************
  * @file    rtty.h
  * @brief   RTTY (FSK) transmit: ITA2 (Baudot) or ASCII
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

#ifndef INC_RTTY_H_
#define INC_RTTY_H_

#include <stdint.h>

typedef struct {
	uint16_t baud_x100;	 /* 4545 = 45.45 baud; 1000-30000 */
	uint16_t shift_hz;	 /* mark - space, 50-1000 (170, 425, 450 are usual) */
	uint8_t bits;		 /* 5 = ITA2 (Baudot), 7 or 8 = ASCII */
	uint8_t stop_halves; /* stop bit length in half bits: 2, 3 (1.5 bits) or 4 */
	uint8_t reverse;	 /* 0: mark is the higher frequency (the convention) */
	uint8_t reserved;
} rtty_params_t;

_Static_assert(sizeof(rtty_params_t) == 8, "rtty_params_t is read over SWD");

/* ITA2 shift codes */
#define RTTY_ITA2_LTRS 0x1F
#define RTTY_ITA2_FIGS 0x1B

/* the Si4063's deviation step at 420-450 MHz (VCO output divider 8):
 * 2 * XO_FREQ / 2^19 / 8 = 12.207 Hz, in mHz; si4060_set_deviation() rounds
 * down to it (checked against XO_FREQ in rtty_hw.c) */
#define RTTY_DEV_STEP_MHZ 12207UL

/* mark before the text, so a decoder can find the tones and lock, and after */
#define RTTY_LEAD_MS 1000
#define RTTY_TAIL_MS 300

/* rtty_ita2() shift state: nothing sent yet, or a space unshifted the receiver */
enum { RTTY_SHIFT_NONE = 0, RTTY_SHIFT_LTRS, RTTY_SHIFT_FIGS };

uint8_t rtty_params_ok(const rtty_params_t *p);
uint8_t rtty_ita2(char c, uint8_t *shift, uint8_t codes[2]);
uint16_t rtty_halves(uint32_t ms, uint16_t baud_x100);
uint16_t rtty_deviation_hz(uint16_t shift_hz);
void rtty_send(const char *text, const rtty_params_t *p);

/* the radio and bit timing (rtty_hw.c): key up on mark, hold each level for a
 * number of half bits, key down */
void rtty_hw_begin(const rtty_params_t *p);
void rtty_hw_bits(uint8_t mark, uint16_t halves);
void rtty_hw_end(void);

#endif /* INC_RTTY_H_ */
