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
  * The CAL_* commands do the same for the analog calibration block (cal.c,
  * cal_edit, cal_error), which is loaded first at boot and saved separately.
  *
  * Every main loop pass re-checks the live settings; if something corrupted
  * them (e.g. a stray SWD write into cfg), the last good copy is restored.
  * The transmit gate (settings_tx_allowed() in si4060_start_tx()) checks them
  * again before every key-up.
  ******************************************************************************
  */

#include "settings.h"
#include "main.h"
#include "analog.h"
#include "aprs.h"
#include "cal.h"
#include "i2c_ee.h"
#include "ppsdisc.h"
#include "si4063.h"
#include "version.h"
#include <stdio.h>

settings_t settings_edit;
cal_t cal_edit;
volatile uint8_t settings_cmd;
volatile uint8_t settings_result;
volatile uint32_t settings_done;
volatile uint32_t settings_repairs;

const swd_info_t swd_info = {
		.magic = SWD_INFO_MAGIC,
		.version = SWD_INFO_VERSION,
		.size = sizeof(swd_info_t),
		.settings_version = SETTINGS_VERSION,
		.settings_size = sizeof(settings_t),
		.fw_version = fw_version,
		.cfg = &cfg,
		.settings_edit = &settings_edit,
		.settings_cmd = &settings_cmd,
		.settings_result = &settings_result,
		.settings_source = &settings_source,
		.settings_error = &settings_error,
		.settings_done = &settings_done,
		.settings_repairs = &settings_repairs,
		.tx_refused = &si4060_tx_refused,
		.aprs_send_now = &aprs_send_now,
		.aprs_packets_sent = &aprs_packets_sent,
		.cal = &cal,
		.cal_edit = &cal_edit,
		.cal_source = &cal_source,
		.cal_error = &cal_error,
		.analog_last = &analog_last,
};

/* the last settings that passed settings_check(), for the runtime check */
static settings_t good;

static uint8_t same_bytes(const void *a, const void *b, unsigned n) {
	const uint8_t *p = a, *q = b;
	for (unsigned i = 0; i < n; i++)
		if (p[i] != q[i])
			return 0;
	return 1;
}

static uint8_t same(const settings_t *a, const settings_t *b) {
	return same_bytes(a, b, sizeof(*a));
}

/* a block of the user memory: the settings or the calibration */
static uint8_t ee_load_at(uint16_t addr, void *buf, unsigned n) {
	return ee_read(EE_USER_ADDR, addr, (volatile uint8_t *)buf, n);
}

/* 4-byte page writes, then read back and compare (n a multiple of 4, at
 * most 64) */
static uint8_t ee_store_at(uint16_t addr, const void *buf, unsigned n) {
	const uint8_t *p = buf;
	uint8_t back[SETTINGS_SIZE];

	for (unsigned off = 0; off < n; off += 4) {
		if (!ee_write(EE_USER_ADDR, addr + off, p + off, 4))
			return 0;
		if (ee_poll(EE_USER_ADDR) == 0xFF)
			return 0;
	}
	return ee_load_at(addr, back, n) && same_bytes(back, buf, n);
}

static uint8_t ee_load(settings_t *s) {
	return ee_load_at(SETTINGS_EE_ADDR, s, sizeof(*s));
}

static uint8_t ee_store(const settings_t *s) {
	return ee_store_at(SETTINGS_EE_ADDR, s, sizeof(*s));
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

/* the calibration block at boot: each channel from the EEPROM if it's in
 * range, else its default */
static void cal_init(void) {
	static const char *const from[] = {"defaults", "EEPROM", "SWD"};
	cal_t c;

	if (!ee_load_at(CAL_EE_ADDR, &c, sizeof(c)))
		cal_error = CAL_ERR_EEPROM;
	else
		cal_error = cal_load(&c);
	if (cal_error != CAL_OK && cal_error != CAL_ERR_RANGE) {
		cal_defaults(&cal);
		cal_source = CAL_FROM_DEFAULTS;
	}
	printf("calibration: %s (error %u), measured 0x%x\r\n", from[cal_source], cal_error,
			cal.measured);
}

/*
 * settings_init
 *
 * load the analog calibration and the settings at boot, before the radio is
 * set up (initRadio() uses them)
 */
void settings_init(void) {
	settings_t s;

	i2c_init();
	cal_init();
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

static uint8_t run_cal(uint8_t cmd) {
	cal_t c;

	switch (cmd) {
	case CMD_CAL_EDIT:
		cal_edit = cal;
		return RESULT_OK;
	case CMD_CAL_APPLY:
		/* refused whole if anything is wrong; the old factors stay */
		c = cal_edit;
		cal_seal(&c);
		cal_error = cal_check(&c);
		if (cal_error != CAL_OK)
			return RESULT_INVALID;
		cal = c;
		cal_source = CAL_FROM_SWD;
		return RESULT_OK;
	case CMD_CAL_SAVE:
		cal_seal(&cal);
		if (!ee_store_at(CAL_EE_ADDR, &cal, sizeof(cal)))
			return RESULT_EEPROM;
		cal_source = CAL_FROM_EEPROM;
		return RESULT_OK;
	case CMD_CAL_RELOAD: {
		if (!ee_load_at(CAL_EE_ADDR, &c, sizeof(c))) {
			cal_error = CAL_ERR_EEPROM;
			return RESULT_EEPROM;
		}
		/* as at boot, out-of-range channels get their defaults; no valid
		 * block at all: the factors in use stay */
		enum cal_error err = cal_load(&c);
		if (err != CAL_OK && err != CAL_ERR_RANGE) {
			cal_error = err;
			return RESULT_INVALID;
		}
		return RESULT_OK;
	}
	default: /* CMD_CAL_DEFAULTS */
		cal_defaults(&cal);
		cal_source = CAL_FROM_DEFAULTS;
		cal_error = CAL_OK;
		return RESULT_OK;
	}
}

/*
 * settings_command
 *
 * runs a mailbox command (enum settings_cmd) and returns its enum
 * settings_result: the SWD mailbox below, and the serial console (console.c)
 */
uint8_t settings_command(uint8_t cmd) {
	settings_t s;

	if (cmd >= CMD_CAL_EDIT && cmd <= CMD_CAL_DEFAULTS)
		return run_cal(cmd);
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
	settings_result = settings_command(cmd);
	settings_done++;
	settings_cmd = CMD_NONE;
}
