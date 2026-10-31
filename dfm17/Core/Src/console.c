/**
  ******************************************************************************
  * @file    console.c
  * @brief   Serial console on the mini-USB connector (#50)
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
  * Line-based ASCII commands on USART1, on only while USB power is present:
  * on battery the UART is off, its pins are analog and printf output is
  * dropped. docs/serial-console.md lists the commands.
  *
  * The receive interrupt (console_hw.c) hands each byte to console_rx(),
  * which assembles one line; the main loop echoes it and runs it in
  * console_poll(), never in the interrupt. Replies end in a line starting
  * "ok" or "err".
  *
  * The firmware only moves the raw settings and calibration blocks as hex
  * (cfg/cal get/put): tools/config/dfmcfg.py keeps the field names, parsing
  * and messages, and the mailbox commands (settings_command()) check
  * everything again, as over SWD. Changing them needs config mode, in which
  * nothing is scheduled or transmitted; it ends when USB power goes away, so
  * a board can't be left mute in the field. No command keys the transmitter.
  ******************************************************************************
  */

#include "console.h"
#include "analog.h"
#include "cal.h"
#include "diag.h"
#include "gps.h"
#include "settings.h"
#include "si4063.h"
#include "tracker.h"
#include "version.h"
#include <stdarg.h>

static volatile uint8_t enabled; /* UART on: USB power */
static uint8_t usb_changes;		 /* readings in a row that disagree with enabled */
static uint8_t config_mode;
static uint8_t log_on = 1;
static uint8_t gps_stopped; /* by "gps off": woken again by "run" */

/* the line being typed; the interrupt stops adding to it once it's ready */
static char line[CONSOLE_LINE_MAX];
static volatile uint8_t line_len, line_ready, line_long;
static uint8_t echoed;
static uint8_t replying;		   /* printf output is a reply: sent with "log off" too */
volatile uint32_t console_dropped; /* bytes lost while a line waited to run */

/*
 * console_printf
 *
 * a reply, sent whatever the log setting, while the console is on
 */
void console_printf(const char *fmt, ...) {
	va_list ap;

	if (!enabled)
		return;
	replying = 1;
	va_start(ap, fmt);
	console_hw_vprintf(fmt, ap);
	va_end(ap);
	replying = 0;
}

/* back to normal: scheduling and transmissions, and the GPS if "gps off"
 * stopped it (the tracker starts a new cycle) */
static void run_mode(void) {
	config_mode = 0;
	if (gps_stopped) {
		gps_stopped = 0;
		gpsWake();
		tracker_start();
	}
}

/* a one-line reply */
static void reply(const char *s) {
	console_printf("%s\r\n", s);
}

static void set_enabled(uint8_t on) {
	enabled = on;
	console_hw_enable(on);
	line_len = line_ready = line_long = echoed = 0;
	log_on = 1;
	if (on)
		console_printf("\r\n%s, serial console (help)\r\n> ", fw_version);
	else
		run_mode(); /* not left mute when USB power goes */
}

/*
 * console_init
 *
 * at boot, after USART1 is set up and before anything is printed: on with
 * USB power (from a first analog reading), else off
 */
void console_init(uint8_t on_usb) {
	set_enabled(on_usb);
}

/*
 * console_usb
 *
 * every analog reading (once a second): follows USB power, after two
 * readings in a row, so a glitch doesn't switch it
 */
void console_usb(uint8_t on_usb) {
	if (!on_usb == !enabled) {
		usb_changes = 0;
		return;
	}
	if (++usb_changes < 2)
		return;
	usb_changes = 0;
	set_enabled(on_usb);
}

uint8_t console_enabled(void) {
	return enabled;
}

uint8_t console_config_mode(void) {
	return config_mode;
}

/* printf diagnostics: only with the console on, and "log on" */
uint8_t console_log_on(void) {
	return enabled && log_on;
}

/* printf output to send (_write): the diagnostics, or a reply */
uint8_t console_output_on(void) {
	return enabled && (log_on || replying);
}

/*
 * console_rx
 *
 * one received byte, from the USART1 interrupt
 */
void console_rx(uint8_t c) {
	if (line_ready) {
		console_dropped++;
		return;
	}
	if (c == '\r' || c == '\n') {
		if (line_len || line_long)
			line_ready = 1;
	} else if (c == '\b' || c == 0x7F) {
		if (line_len)
			line_len--;
	} else if (c < ' ' || c > '~') {
		console_dropped++;
	} else if (line_len >= CONSOLE_LINE_MAX - 1) {
		line_long = 1;
	} else {
		line[line_len++] = (char)c;
	}
}

/* ---- commands ------------------------------------------------------------ */

/* the next space-separated word of *p ("" at the end) */
static char *word(char **p) {
	char *s = *p;
	while (*s == ' ')
		s++;
	char *w = s;
	while (*s && *s != ' ')
		s++;
	if (*s)
		*s++ = 0;
	*p = s;
	return w;
}

static uint8_t is(const char *a, const char *b) {
	return __builtin_strcmp(a, b) == 0;
}

static int hexval(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* a block the console moves as hex: the settings or the calibration */
typedef struct {
	void *live, *edit;
	uint8_t size;
	uint8_t cmd_edit; /* CMD_EDIT or CMD_CAL_EDIT; APPLY..DEFAULTS follow */
	volatile uint8_t *source, *error;
} block;

static void block_get(const block *b) {
	const uint8_t *p = b->live;
	static const char digits[] = "0123456789abcdef";
	char hex[2 * SETTINGS_SIZE + 1];
	for (int i = 0; i < b->size; i++) {
		hex[2 * i] = digits[p[i] >> 4];
		hex[2 * i + 1] = digits[p[i] & 0xF];
	}
	hex[2 * b->size] = 0;
	console_printf("ok %s %u %u %lu %lu\r\n", hex, *b->source, *b->error,
			(unsigned long)settings_repairs, (unsigned long)si4060_tx_refused);
}

/* the mailbox command's result as a reply */
static void block_result(const block *b, uint8_t result) {
	if (result == RESULT_OK)
		reply("ok");
	else if (result == RESULT_INVALID)
		console_printf("err invalid %u\r\n", *b->error);
	else
		reply("err eeprom");
}

/* put <hex>: the whole block, CRC included, checked and applied */
static void block_put(const block *b, const char *hex) {
	uint8_t *p = b->edit;
	if (__builtin_strlen(hex) != 2u * b->size) {
		reply("err length");
		return;
	}
	for (int i = 0; i < b->size; i++) {
		int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
		if (hi < 0 || lo < 0) {
			reply("err hex");
			return;
		}
		p[i] = (uint8_t)(hi << 4 | lo);
	}
	/* the CRC catches a damaged line; APPLY would otherwise reseal it */
	uint16_t crc = (uint16_t)(p[b->size - 2] | p[b->size - 1] << 8);
	if (crc != crc16_ccitt(p, b->size - 2u)) {
		reply("err crc");
		return;
	}
	block_result(b, settings_command(b->cmd_edit + 1));
}

static void block_cmd(const block *b, const char *sub, const char *arg) {
	static const char *const subs[] = {"put", "save", "reload", "defaults"};

	if (is(sub, "get")) {
		block_get(b);
		return;
	}
	for (uint8_t i = 0; i < 4; i++) {
		if (!is(sub, subs[i]))
			continue;
		if (!config_mode) {
			reply("err config");
		} else if (i == 0) {
			block_put(b, arg);
		} else {
			/* SAVE, RELOAD, DEFAULTS follow APPLY */
			block_result(b, settings_command(b->cmd_edit + 1 + i));
		}
		return;
	}
	reply("err usage");
}

static void on_off(const char *sub, uint8_t *flag, void (*on)(void), void (*off)(void)) {
	if (is(sub, "on") || is(sub, "off")) {
		uint8_t want = is(sub, "on");
		if (flag)
			*flag = want;
		if (want && on)
			on();
		if (!want && off)
			off();
		reply("ok");
	} else {
		reply("err usage");
	}
}

static void gps_on(void) {
	gps_stopped = 0;
	gpsWake();
}

static void gps_off(void) {
	gps_stopped = 1;
	gpsSleep();
}

static void run_line(char *s) {
	static const block cfg_block
			= {&cfg, &settings_edit, SETTINGS_SIZE, CMD_EDIT, &settings_source, &settings_error};
	static const block cal_block
			= {&cal, &cal_edit, CAL_SIZE, CMD_CAL_EDIT, &cal_source, &cal_error};
	char *p = s;
	const char *cmd = word(&p), *sub = word(&p), *arg = word(&p);

	if (is(cmd, "help")) {
		reply("ok help ver status analog log fox config run gps cfg cal reset");
	} else if (is(cmd, "ver")) {
		console_hw_ver();
	} else if (is(cmd, "status")) {
		console_hw_status();
	} else if (is(cmd, "analog")) {
		const analog_reading *r = &analog_last;
		console_printf("ok %u %u %u %u %u %u %d %u %u %u\r\n", r->vin_raw, r->iin_raw, r->vref_raw,
				r->vdda_mv, r->vin_mv, r->iin_ma, r->temp_c, r->usb_raw, r->usb_mv, r->on_usb);
	} else if (is(cmd, "log")) {
		on_off(sub, &log_on, NULL, NULL);
	} else if (is(cmd, "fox")) {
		/* the same as a long button press: pause or resume the fox/tracker */
		if (is(sub, "start") || is(sub, "stop")) {
			diag_set_paused(is(sub, "stop"));
			reply("ok");
		} else {
			reply("err usage");
		}
	} else if (is(cmd, "config")) {
		/* the reply says whether it was on already, so a tool can put it back */
		console_printf("ok %u\r\n", config_mode);
		config_mode = 1;
	} else if (is(cmd, "run")) {
		run_mode();
		reply("ok");
	} else if (is(cmd, "gps")) {
		if (!config_mode)
			reply("err config");
		else
			on_off(sub, NULL, gps_on, gps_off);
	} else if (is(cmd, "cfg")) {
		block_cmd(&cfg_block, sub, arg);
	} else if (is(cmd, "cal")) {
		block_cmd(&cal_block, sub, arg);
	} else if (is(cmd, "reset")) {
		/* on battery a reset releases the power hold and turns the board off */
		if (!analog_last.on_usb) {
			reply("err battery");
		} else {
			reply("ok");
			console_hw_reset();
		}
	} else {
		reply("err unknown");
	}
}

/*
 * console_poll
 *
 * from the main loop: echoes what was typed, and runs a finished line
 */
void console_poll(void) {
	if (!enabled)
		return;
	/* ready first: once it's set the line is complete, so n is all of it */
	uint8_t ready = line_ready;
	uint8_t n = line_len;
	if (echoed < n)
		console_hw_send(&line[echoed], n - echoed);
	while (echoed > n) {
		console_hw_send("\b \b", 3);
		echoed--;
	}
	echoed = n;
	if (!ready)
		return;

	console_hw_send("\r\n", 2);
	line[line_len] = 0;
	if (line_long)
		reply("err long");
	else
		run_line(line);
	console_hw_send("> ", 2);
	line_len = echoed = line_long = 0;
	line_ready = 0;
}
