/**
  ******************************************************************************
  * @file    fox.c
  * @brief   Fox hunt transmitter: Morse ID, hunt tone and optional APRS position
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
  * Everything is FM: the carrier stays up for the whole cycle and the tone on
  * Si4063 GPIO3 is keyed on and off, so any FM handheld hears the Morse ID and
  * the hunt tone (MCW). The tone comes from the TIM15 tick ISR (tone_half_ticks).
  ******************************************************************************
  */

#include "fox.h"
#include "main.h"
#include "aprs.h"
#include "diag.h"
#include "gpio.h"
#include "si4063.h"
#include "tim.h"

static void tone_on(void) {
	tone_half_ticks = FOX_TONE_HALF_TICKS;
}

/* key up with the tone off; the carrier sits at one end of the deviation */
static void tx_start(void) {
	diag_set_tx(1);
	tone_half_ticks = 0;
	deassertSiGPIO3();
	startAprsTickTimer();
	si4060_start_tx(0);
}

static void tx_stop(void) {
	si4060_stop_tx();
	stopAprsTickTimer();
	diag_set_tx(0);
}

static void send_aprs(GNSS_StateHandle *GNSS) {
	diag_set_tx(1);
	aprs_prepare_buffer(GNSS, 0);
	calculate_fcs();
	tx_aprs();
	diag_set_tx(0);
}

static void tone_off(void) {
	tone_half_ticks = 0;
	/* park the carrier at one end of the deviation, no audio */
	deassertSiGPIO3();
}

/*
 * morse
 *
 * returns the dot/dash pattern for a character, or NULL if there is none
 */
static const char *morse(char c) {
	static const char *const letters[26] = {
		".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..",
		".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.",
		"...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."};
	static const char *const digits[10] = {
		"-----", ".----", "..---", "...--", "....-",
		".....", "-....", "--...", "---..", "----."};

	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	if (c >= 'A' && c <= 'Z')
		return letters[c - 'A'];
	if (c >= '0' && c <= '9')
		return digits[c - '0'];
	if (c == '/')
		return "-..-.";
	return NULL;
}

/*
 * send_cw
 *
 * keys the tone in Morse at FOX_CW_WPM (PARIS timing: dit = 1200 / WPM ms).
 * the transmitter must already be on.
 */
static void send_cw(const char *text) {
	uint32_t dit = 1200 / FOX_CW_WPM;

	for (; *text; text++) {
		if (*text == ' ') {
			/* word gap is 7 dits, 3 were already sent after the last letter */
			HAL_Delay(4 * dit);
			continue;
		}
		const char *code = morse(*text);
		if (code == NULL)
			continue;
		for (; *code; code++) {
			tone_on();
			HAL_Delay(*code == '-' ? 3 * dit : dit);
			tone_off();
			HAL_Delay(dit);
		}
		/* letter gap is 3 dits, 1 was already sent after the last element */
		HAL_Delay(2 * dit);
	}
}

/*
 * fox_transmit
 *
 * one fox transmission: Morse ID, FOX_TONE_MS of hunt tone, then (with
 * FOX_SEND_APRS, every FOX_APRS_EVERY-th time) an APRS position packet. the
 * caller repeats it every FOX_PERIOD_MS.
 */
void fox_transmit(GNSS_StateHandle *GNSS) {
	tx_start();
	/* let receiver squelch open before the ID */
	HAL_Delay(500);

	send_cw(FOX_CALLSIGN);
	HAL_Delay(500);

	tone_on();
	HAL_Delay(FOX_TONE_MS);
	tone_off();
	HAL_Delay(250);
	tx_stop();

#ifdef FOX_SEND_APRS
	/* the first cycle after boot sends one, then every FOX_APRS_EVERY-th */
	static uint16_t cycle = 0;
	if (cycle++ % FOX_APRS_EVERY == 0) {
		HAL_Delay(1000);
		send_aprs(GNSS);
	}
#else
	(void)GNSS;
#endif
}

/*
 * fox_test_carrier
 *
 * button short press: FOX_TEST_MS of unmodulated carrier exactly on
 * RF_FREQ_HZ_DFM, a single clean line on an SDR for reading the frequency
 */
void fox_test_carrier(void) {
	si4060_set_cw(1);
	tx_start();
	HAL_Delay(FOX_TEST_MS);
	tx_stop();
	si4060_set_cw(0);
}

/*
 * fox_test_tone_aprs
 *
 * button medium press: FOX_TEST_MS of a steady 1200 Hz tone (the APRS mark
 * tone, for checking deviation and audio level), then one APRS packet
 */
void fox_test_tone_aprs(GNSS_StateHandle *GNSS) {
	tx_start();
	tone_half_ticks = FOX_TEST_TONE_HALF_TICKS;
	HAL_Delay(FOX_TEST_MS);
	tone_off();
	tx_stop();

	HAL_Delay(500);
	send_aprs(GNSS);
}
