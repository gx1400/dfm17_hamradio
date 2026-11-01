/**
  ******************************************************************************
  * @file    rtty.c
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
  * Each character goes out as a start bit (space), the data bits LSB first
  * and the stop bits (mark), at the baud rate in rtty_params_t. Amateur RTTY
  * is ITA2 at 45.45 baud, 170 Hz shift, 1.5 stop bits; UKHAS balloon telemetry
  * is ASCII 7N2 or 8N2 at 50 baud, 425 Hz (#53).
  *
  * This file is the encoding and framing, host-tested; rtty_hw.c holds GPIO3
  * at each level for the right time.
  ******************************************************************************
  */

#include "rtty.h"

/* ITA2 letters and figures, by code. 0 = no character there (or a shift
 * code); figures only where ITA2 and US TTY agree */
static const char ita2_ltrs[32] = {0, 'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N',
		'F', 'C', 'K', 'T', 'Z', 'L', 'W', 'H', 'Y', 'P', 'Q', 'O', 'B', 'G', 0, 'M', 'X', 'V', 0};
static const char ita2_figs[32] = {0, '3', '\n', '-', ' ', 0, '8', '7', '\r', 0, '4', 0, ',', 0,
		':', '(', '5', 0, ')', '2', 0, '6', '0', '1', '9', '?', 0, 0, '.', '/', 0, 0};

/*
 * rtty_params_ok
 *
 * returns 1 if the parameters can be sent
 */
uint8_t rtty_params_ok(const rtty_params_t *p) {
	return p->baud_x100 >= 1000 && p->baud_x100 <= 30000 && p->shift_hz >= 50 && p->shift_hz <= 1000
			&& (p->bits == 5 || p->bits == 7 || p->bits == 8) && p->stop_halves >= 2
			&& p->stop_halves <= 4;
}

/*
 * rtty_ita2
 *
 * the ITA2 codes for one character into codes[]: the character, preceded by
 * LTRS or FIGS when the shift changes. *shift is the receiver's state, kept
 * between calls (RTTY_SHIFT_NONE to start). Receivers usually go back to
 * letters on a space (unshift on space), so after a space in figures the
 * state is unknown and the next character sends its shift again. Lower case
 * is sent as upper case.
 *
 * returns the number of codes, 0 if the character can't be sent
 */
uint8_t rtty_ita2(char c, uint8_t *shift, uint8_t codes[2]) {
	uint8_t code, need;

	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	if (c == 0)
		return 0;
	for (code = 0; code < 32; code++)
		if (ita2_ltrs[code] == c || ita2_figs[code] == c)
			break;
	if (code == 32)
		return 0;

	if (ita2_ltrs[code] == ita2_figs[code]) {
		/* space, CR, LF: the same in both shifts */
		codes[0] = code;
		if (c == ' ' && *shift == RTTY_SHIFT_FIGS)
			*shift = RTTY_SHIFT_NONE;
		return 1;
	}
	need = ita2_ltrs[code] == c ? RTTY_SHIFT_LTRS : RTTY_SHIFT_FIGS;
	uint8_t n = 0;
	if (*shift != need) {
		codes[n++] = need == RTTY_SHIFT_LTRS ? RTTY_ITA2_LTRS : RTTY_ITA2_FIGS;
		*shift = need;
	}
	codes[n++] = code;
	return n;
}

/*
 * rtty_halves
 *
 * returns the number of half bits closest to ms at baud_x100
 */
uint16_t rtty_halves(uint32_t ms, uint16_t baud_x100) {
	return (uint16_t)((ms * baud_x100 * 2 + 50000) / 100000);
}

/*
 * rtty_deviation_hz
 *
 * the deviation to ask si4060_set_deviation() for so the shift is the
 * nearest the radio can make: twice a whole number of deviation steps.
 * Asking for shift / 2 would round down: 170 Hz came out as 146 Hz on air.
 */
uint16_t rtty_deviation_hz(uint16_t shift_hz) {
	uint32_t steps = (shift_hz * 500UL + RTTY_DEV_STEP_MHZ / 2) / RTTY_DEV_STEP_MHZ;
	/* just above the step, so rounding down lands on it */
	return (uint16_t)((steps * RTTY_DEV_STEP_MHZ + 999) / 1000);
}

static void send_char(uint8_t code, const rtty_params_t *p) {
	rtty_hw_bits(0, 2);
	for (uint8_t b = 0; b < p->bits; b++)
		rtty_hw_bits((code >> b) & 1, 2);
	rtty_hw_bits(1, p->stop_halves);
}

/*
 * rtty_send
 *
 * keys up, sends RTTY_LEAD_MS of mark, the text, RTTY_TAIL_MS of mark, and
 * keys down. ITA2 starts with LTRS so the receiver's shift is known; ASCII
 * sends 7 or 8 bits of each character. Does nothing with bad parameters.
 */
void rtty_send(const char *text, const rtty_params_t *p) {
	uint8_t shift = RTTY_SHIFT_LTRS;

	if (!rtty_params_ok(p))
		return;
	rtty_hw_begin(p);
	rtty_hw_bits(1, rtty_halves(RTTY_LEAD_MS, p->baud_x100));
	if (p->bits == 5)
		send_char(RTTY_ITA2_LTRS, p);
	for (; *text; text++) {
		uint8_t codes[2], n = 1;
		if (p->bits == 5)
			n = rtty_ita2(*text, &shift, codes);
		else
			codes[0] = (uint8_t)*text & (p->bits == 7 ? 0x7F : 0xFF);
		for (uint8_t i = 0; i < n; i++)
			send_char(codes[i], p);
	}
	rtty_hw_bits(1, rtty_halves(RTTY_TAIL_MS, p->baud_x100));
	rtty_hw_end();
}
