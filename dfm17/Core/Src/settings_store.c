/**
  ******************************************************************************
  * @file    settings_store.c
  * @brief   Station settings: EEPROM storage, SWD editing, applying them
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
  * At boot the settings block is read from the EEPROM and used only if
  * settings_check() passes; otherwise the built-in defaults (callsign N0CALL,
  * no transmitting). A bad block can't stop the board booting or being fixed
  * over SWD.
  *
  * SWD mailbox (tools/config/dfmcfg.py): copy the live settings into
  * settings_edit (EDIT), change fields there, then APPLY (checked; refused if
  * invalid, the old settings stay) and SAVE (written to the EEPROM, read back
  * and compared). RELOAD and DEFAULTS too. Results in settings_result.
  *
  * Every main loop pass re-checks the live settings; if something corrupted
  * them (e.g. a stray SWD write into cfg), the last good copy is restored.
  * The transmit gate (settings_tx_allowed() in si4060_start_tx()) checks them
  * again before every key-up.
  ******************************************************************************
  */

#include "settings.h"
#include "main.h"
#include "aprs.h"
#include "i2c_ee.h"
#include "ppsdisc.h"
#include "si4063.h"
#include <stdio.h>

enum settings_cmd {
	CMD_NONE,
	CMD_EDIT,	  /* settings_edit = live settings */
	CMD_APPLY,	  /* check settings_edit and use it */
	CMD_SAVE,	  /* write the live settings to the EEPROM */
	CMD_RELOAD,	  /* load and use the EEPROM block, if valid */
	CMD_DEFAULTS, /* use the built-in defaults (not saved) */
};

enum settings_result {
	RESULT_NONE,
	RESULT_OK,
	RESULT_INVALID, /* settings_error says why; nothing changed */
	RESULT_EEPROM,	/* EEPROM write or read-back failed */
	RESULT_BAD_CMD,
};

settings_t settings_edit;
volatile uint8_t settings_cmd;
volatile uint8_t settings_result;
volatile uint32_t settings_done;
volatile uint32_t settings_repairs;

/* the last settings that passed settings_check(), for the runtime check */
static settings_t good;

static uint8_t same(const settings_t *a, const settings_t *b) {
	const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
	for (unsigned i = 0; i < sizeof(*a); i++)
		if (p[i] != q[i])
			return 0;
	return 1;
}

static uint8_t ee_load(settings_t *s) {
	return ee_read(EE_USER_ADDR, SETTINGS_EE_ADDR, (volatile uint8_t *)s, sizeof(*s));
}

/* 4-byte page writes, then read back and compare */
static uint8_t ee_store(const settings_t *s) {
	const uint8_t *p = (const uint8_t *)s;
	settings_t back;

	for (unsigned off = 0; off < sizeof(*s); off += 4) {
		if (!ee_write(EE_USER_ADDR, SETTINGS_EE_ADDR + off, p + off, 4))
			return 0;
		if (ee_poll(EE_USER_ADDR) == 0xFF)
			return 0;
	}
	return ee_load(&back) && same(&back, s);
}

/* make s the live settings (already checked) */
static void use(const settings_t *s, enum settings_source source) {
	cfg = *s;
	good = *s;
	settings_source = source;
	aprs_set_station();
}

static void print_settings(void) {
	static const char *const from[] = {"defaults", "EEPROM", "SWD"};
	printf("settings: %s (error %u), %s-%u, %lu Hz, mode %s, %s\r\n", from[settings_source],
			settings_error, settings_callsign(), cfg.ssid, (unsigned long)cfg.freq_hz,
			cfg.mode == MODE_FOX ? "fox" : "tracker",
			settings_tx_allowed() ? "TX allowed" : "TX disabled (no callsign)");
}

/*
 * settings_init
 *
 * load the settings at boot, before the radio is set up (initRadio() uses
 * them)
 */
void settings_init(void) {
	settings_t s;

	i2c_init();
	if (!ee_load(&s)) {
		settings_error = SETTINGS_ERR_EEPROM;
	} else {
		settings_error = settings_check(&s);
		if (settings_error == SETTINGS_OK) {
			use(&s, SETTINGS_FROM_EEPROM);
			print_settings();
			return;
		}
	}
	settings_defaults(&s);
	use(&s, SETTINGS_FROM_DEFAULTS);
	print_settings();
}

/*
 * settings_apply
 *
 * use new settings now (between transmissions): checked first, and refused
 * with nothing changed if invalid. returns 1 if applied
 */
uint8_t settings_apply(const settings_t *s, enum settings_source source) {
	settings_error = settings_check(s);
	if (settings_error != SETTINGS_OK)
		return 0;
	use(s, source);
	si4060_set_frequency(pps_disc_freq(cfg.freq_hz));
	si4060_set_deviation(cfg.dev_hz);
	si4060_set_tx_power(cfg.pa_level);
	print_settings();
	return 1;
}

static uint8_t run(uint8_t cmd) {
	settings_t s;

	switch (cmd) {
	case CMD_EDIT:
		settings_edit = cfg;
		return RESULT_OK;
	case CMD_APPLY:
		/* the editor changes fields; magic, version, length and CRC are ours */
		s = settings_edit;
		settings_seal(&s);
		return settings_apply(&s, SETTINGS_FROM_SWD) ? RESULT_OK : RESULT_INVALID;
	case CMD_SAVE:
		if (!ee_store(&cfg))
			return RESULT_EEPROM;
		settings_source = SETTINGS_FROM_EEPROM;
		return RESULT_OK;
	case CMD_RELOAD:
		if (!ee_load(&s)) {
			settings_error = SETTINGS_ERR_EEPROM;
			return RESULT_EEPROM;
		}
		return settings_apply(&s, SETTINGS_FROM_EEPROM) ? RESULT_OK : RESULT_INVALID;
	case CMD_DEFAULTS:
		settings_defaults(&s);
		return settings_apply(&s, SETTINGS_FROM_DEFAULTS) ? RESULT_OK : RESULT_INVALID;
	default:
		return RESULT_BAD_CMD;
	}
}

/*
 * settings_poll
 *
 * call from the main loop, between transmissions: runs an SWD command, and
 * restores the last good settings if the live ones no longer check out
 */
void settings_poll(void) {
	if (settings_check(&cfg) != SETTINGS_OK || !same(&cfg, &good)) {
		settings_repairs++;
		cfg = good;
		aprs_set_station();
		si4060_set_frequency(pps_disc_freq(cfg.freq_hz));
		si4060_set_deviation(cfg.dev_hz);
		si4060_set_tx_power(cfg.pa_level);
		printf("settings: live settings were changed outside settings_apply, restored\r\n");
	}

	uint8_t cmd = settings_cmd;
	if (cmd == CMD_NONE)
		return;
	settings_result = run(cmd);
	settings_done++;
	settings_cmd = CMD_NONE;
}
