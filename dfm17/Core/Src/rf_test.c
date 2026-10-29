/**
  ******************************************************************************
  * @file    rf_test.c
  * @brief   Carrier test controlled over SWD, for frequency sweeps
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
  ******************************************************************************
  */

#include "main.h"
#include "si4063.h"
#include "diag.h"

volatile uint32_t rf_test_freq_hz = 0;
volatile uint8_t rf_test_power = 0x00;

void rf_test_poll(void) {
	static uint32_t current = 0;
	uint32_t f = rf_test_freq_hz;

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
		si4060_set_tx_power(RF_TX_POWER_DFM);
		si4060_set_frequency(RF_FREQ_HZ_DFM);
		si4060_set_deviation(RF_DEV_HZ_DFM);
		return;
	}
	si4060_set_cw(1);
	si4060_set_tx_power(rf_test_power);
	si4060_set_frequency(f);
	si4060_set_deviation(RF_DEV_HZ_DFM);
	si4060_start_tx(0);
	diag_set_tx(1);
}
