/**
  ******************************************************************************
  * @file    rf_test.c
  * @brief   Carrier and transmit-mode tests controlled over SWD
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
  * Built with RF_TEST_SWD defined in main.c; the main loop then calls
  * rf_test_poll() instead of running the fox. Write rf_test_power, then
  * rf_test_freq_hz, over SWD (hot-plug, e.g. STM32_Programmer_CLI -w32): the
  * radio transmits an unmodulated carrier on that frequency until
  * rf_test_freq_hz changes. 0 stops. Frequencies outside 420-450 MHz are refused.
  * tools/rf_sweep/ drives it together with a HackRF.
  *
  * Transmit tests (tools/rf_sweep/tx_test.py), on the settings' frequency at
  * rf_test_power: write rf_test_tx in one go (the command is its last word),
  * then wait for rf_test_tx_done to count up and read rf_test_tx_result.
  * - RF_TEST_CW: the text in Morse (MCW, as the fox), then "DE <callsign>"
  * - RF_TEST_CWID: the fox's Morse ID, the callsign alone
  * - RF_TEST_RTTY: CR LF, the text, "DE <callsign>", CR LF in RTTY (rtty.c)
  * Every message ends with the callsign, so a test always identifies.
  ******************************************************************************
  */

#include "main.h"
#include "si4063.h"
#include "settings.h"
#include "diag.h"
#include "cw.h"
#include "gpio.h"
#include "rtty.h"
#include "tim.h"

enum { RF_TEST_NONE = 0, RF_TEST_CW, RF_TEST_CWID, RF_TEST_RTTY };
enum { RF_TEST_OK = 1, RF_TEST_BAD, RF_TEST_REFUSED, RF_TEST_UNKNOWN };

#define RF_TEST_TEXT_LEN 48

typedef struct {
	rtty_params_t rtty;
	uint8_t wpm; /* CW speed, 0 = the settings' cw_wpm */
	uint8_t reserved[3];
	char text[RF_TEST_TEXT_LEN]; /* NUL-terminated */
	uint32_t cmd;				 /* written last; 0 again when done */
} rf_test_tx_t;

_Static_assert(sizeof(rf_test_tx_t) == 64, "rf_test_tx layout (tools/rf_sweep/tx_test.py)");

volatile uint32_t rf_test_freq_hz = 0;
volatile uint8_t rf_test_power = 0x00;

/* amateur RTTY defaults: 45.45 baud, 170 Hz shift, ITA2, 1.5 stop bits */
volatile rf_test_tx_t rf_test_tx = {
		.rtty = {4545, 170, 5, 3, 0, 0},
		.text = "RYRYRY TEST",
};
volatile uint32_t rf_test_tx_done;
volatile uint8_t rf_test_tx_result;

/* appends src to dst (size n), always terminated; returns dst */
static char *append(char *dst, const char *src, uint16_t n) {
	uint16_t i = 0;
	while (i + 1 < n && dst[i])
		i++;
	while (i + 1 < n && *src)
		dst[i++] = *src++;
	dst[i] = 0;
	return dst;
}

static void cw_key_up(void) {
	diag_set_tx(1);
	cw_tone(0);
	startAprsTickTimer();
	si4060_start_tx(0);
	/* let a receiver's squelch open */
	HAL_Delay(500);
}

static void cw_key_down(void) {
	HAL_Delay(250);
	si4060_stop_tx();
	stopAprsTickTimer();
	diag_set_tx(0);
}

static uint8_t tx_test(uint32_t cmd) {
	rtty_params_t p;
	char text[RF_TEST_TEXT_LEN];
	char msg[RF_TEST_TEXT_LEN + 24];
	uint32_t refused = si4060_tx_refused;

	/* a copy: the SWD side may write again while this runs */
	for (uint8_t i = 0; i < RF_TEST_TEXT_LEN; i++)
		text[i] = rf_test_tx.text[i];
	text[RF_TEST_TEXT_LEN - 1] = 0;
	p.baud_x100 = rf_test_tx.rtty.baud_x100;
	p.shift_hz = rf_test_tx.rtty.shift_hz;
	p.bits = rf_test_tx.rtty.bits;
	p.stop_halves = rf_test_tx.rtty.stop_halves;
	p.reverse = rf_test_tx.rtty.reverse;
	p.reserved = 0;
	uint8_t wpm = rf_test_tx.wpm ? rf_test_tx.wpm : cfg.cw_wpm;

	msg[0] = 0;
	if (cmd == RF_TEST_RTTY)
		append(msg, "\r\n", sizeof(msg));
	if (cmd != RF_TEST_CWID) {
		append(msg, text, sizeof(msg));
		append(msg, " DE ", sizeof(msg));
	}
	append(msg, settings_callsign(), sizeof(msg));
	if (cmd == RF_TEST_RTTY)
		append(msg, "\r\n", sizeof(msg));

	if (cmd == RF_TEST_RTTY && !rtty_params_ok(&p))
		return RF_TEST_BAD;
	if (wpm < 5 || wpm > 40)
		return RF_TEST_BAD;

	si4060_set_tx_power(rf_test_power);
	if (cmd == RF_TEST_RTTY) {
		rtty_send(msg, &p);
	} else {
		cw_key_up();
		cw_send(msg, wpm);
		cw_key_down();
	}
	si4060_set_tx_power(cfg.pa_level);
	return si4060_tx_refused == refused ? RF_TEST_OK : RF_TEST_REFUSED;
}

void rf_test_poll(void) {
	static uint32_t current = 0;
	uint32_t f = rf_test_freq_hz;
	uint32_t cmd = rf_test_tx.cmd;

	if (cmd) {
		/* not while the test carrier is on */
		if (current)
			rf_test_tx_result = RF_TEST_REFUSED;
		else if (cmd == RF_TEST_CW || cmd == RF_TEST_CWID || cmd == RF_TEST_RTTY)
			rf_test_tx_result = tx_test(cmd);
		else
			rf_test_tx_result = RF_TEST_UNKNOWN;
		rf_test_tx.cmd = 0;
		rf_test_tx_done++;
	}

	if (f == current)
		return;
	if (current) {
		si4060_stop_tx();
		diag_set_tx(0);
	}
	current = f;
	if (f < 420000000UL || f > 450000000UL) {
		/* off (or refused): back to the normal settings */
		current = 0;
		si4060_set_cw(0);
		si4060_set_tx_power(cfg.pa_level);
		si4060_set_frequency(cfg.freq_hz);
		si4060_set_deviation(cfg.dev_hz);
		return;
	}
	si4060_set_cw(1);
	si4060_set_tx_power(rf_test_power);
	si4060_set_frequency(f);
	si4060_set_deviation(cfg.dev_hz);
	si4060_start_tx(0);
	diag_set_tx(1);
}
