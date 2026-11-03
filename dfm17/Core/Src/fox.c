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
  * the hunt tone (MCW). The tone comes from the TIM15 tick ISR (tone_half_ticks);
  * the Morse is cw.c.
  ******************************************************************************
  */

#include "fox.h"
#include "main.h"
#include "aprs.h"
#include "cw.h"
#include "diag.h"
#include "foxgps.h"
#include "gpio.h"
#include "gps.h"
#include "si4063.h"
#include "tim.h"
#include "settings.h"

/* key up with the tone off on the fox's frequency and PA level (#82); the
 * carrier sits at one end of the deviation */
static void tx_start(void) {
	radio_use(TX_FOX);
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

/* the current fix while the GPS is locked, else the last one (it sleeps
 * between resyncs, foxgps.c), else a status packet */
static void send_aprs(const GNSS_StateHandle *GNSS) {
	const GNSS_StateHandle *last = fox_gps_last_fix();
	diag_set_tx(1);
	if ((ppsLockStatus && GNSS->fixType >= Fix3D) || !last)
		aprs_prepare_buffer(GNSS, 0);
	else
		aprs_prepare_fix(last, 1, 0);
	tx_aprs();
	diag_set_tx(0);
}

/*
 * fox_transmit
 *
 * one fox transmission: Morse ID, the hunt tone (fox_tone_ms), then every
 * fox_aprs_every-th time (0 = never) an APRS position packet. the caller
 * repeats it every fox_period_s.
 */
void fox_transmit(const GNSS_StateHandle *GNSS) {
	tx_start();
	/* let receiver squelch open before the ID */
	HAL_Delay(500);

	cw_send(settings_callsign(), cfg.cw_wpm);
	HAL_Delay(500);

	cw_tone(1);
	HAL_Delay(cfg.fox_tone_ms);
	cw_tone(0);
	HAL_Delay(250);
	tx_stop();

	/* the first cycle after boot sends one, then every fox_aprs_every-th */
	static uint16_t cycle = 0;
	if (cfg.fox_aprs_every && cycle++ % cfg.fox_aprs_every == 0) {
		HAL_Delay(1000);
		send_aprs(GNSS);
	}
}

/*
 * fox_test_carrier
 *
 * button short press: FOX_TEST_MS of unmodulated carrier exactly on
 * the fox's frequency (fox_freq, else freq_hz), a single clean line on an
 * SDR for reading it
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
void fox_test_tone_aprs(const GNSS_StateHandle *GNSS) {
	tx_start();
	tone_half_ticks = FOX_TEST_TONE_HALF_TICKS;
	HAL_Delay(FOX_TEST_MS);
	cw_tone(0);
	tx_stop();

	HAL_Delay(500);
	send_aprs(GNSS);
}

/*
 * fox_send_now
 *
 * send-now trigger (#51): one APRS packet, no test tone
 */
void fox_send_now(const GNSS_StateHandle *GNSS) {
	send_aprs(GNSS);
}
