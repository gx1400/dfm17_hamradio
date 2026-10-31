/**
  ******************************************************************************
  * @file    settings.h
  * @brief   This file contains all the function prototypes for
  *          the settings.c and settings_store.c files
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

#ifndef INC_SETTINGS_H_
#define INC_SETTINGS_H_

#include <stddef.h>
#include <stdint.h>
#include "analog.h"
#include "cal.h"

/* station settings in the NFC EEPROM, user memory 0x180 (sector 3, the free
 * sector; open for phone access since #8). docs/settings.md has the map;
 * tools/config/dfmcfg.py reads and writes it over SWD. */
#define SETTINGS_EE_ADDR 0x0180
#define SETTINGS_VERSION 1
#define SETTINGS_SIZE	 64

enum settings_mode { MODE_TRACKER = 0, MODE_FOX = 1 };

/* GPS power (gps_power). 0xFF, the erased value, is the default so blocks
 * saved before the field existed stay valid; it duty-cycles like before
 * (issue #36 may make it switch on USB power). */
enum settings_gps_power {
	GPS_POWER_DUTY_CYCLE = 0, /* backup mode between tracker fixes */
	GPS_POWER_ALWAYS_ON = 1,  /* never sleep the GPS: continuous 1PPS, ~20-25 mA more */
	GPS_POWER_DEFAULT = 0xFF, /* duty cycle */
};

/* flags */
#define SETTINGS_FLAG_START_PAUSED 0x01 /* fox/tracker start paused */

typedef struct __attribute__((packed)) {
	char magic[2];							  /* "DF" */
	uint8_t version;						  /* SETTINGS_VERSION */
	uint8_t length;							  /* SETTINGS_SIZE */
	char callsign[6];						  /* A-Z 0-9, NUL-padded; "N0CALL" = not set up */
	uint8_t ssid;							  /* 0-15 */
	char symbol_table;						  /* '/', '\\' or an overlay A-Z */
	char symbol_code;						  /* e.g. 'O' balloon, '>' car */
	uint8_t path;							  /* WIDEn-N as 0xnN: 0x11 WIDE1-1, 0x21, 0x22 */
	uint8_t mode;							  /* enum settings_mode */
	uint8_t flags;							  /* SETTINGS_FLAG_* */
	uint32_t freq_hz;						  /* 420-450 MHz */
	uint8_t pa_level;						  /* Si4063 PA_PWR_LVL, 0x00-0x7F */
	uint8_t cw_wpm;							  /* 5-40 */
	uint16_t dev_hz;						  /* FM deviation of tone/AFSK, 1000-5000 */
	uint16_t tracker_period_s;				  /* time between packets, >= 30 */
	uint16_t tracker_fix_timeout_s;			  /* give up on a fix after this */
	uint16_t tracker_settle_s;				  /* hold a 3D fix this long before sending */
	uint16_t fox_period_s;					  /* time between fox transmissions, >= 10 */
	uint16_t fox_tone_ms;					  /* hunt tone length, <= 60000 */
	uint8_t fox_aprs_every;					  /* APRS after every Nth fox cycle, 0 = never */
	uint8_t gps_power;						  /* enum settings_gps_power */
	uint8_t reserved[SETTINGS_SIZE - 36 - 2]; /* 0xFF */
	uint16_t crc;							  /* CRC-16/CCITT-FALSE of the bytes before it */
} settings_t;

/* the layout is shared with tools/config/dfmcfg.py: pin it */
_Static_assert(sizeof(settings_t) == SETTINGS_SIZE, "settings_t size");
_Static_assert(offsetof(settings_t, callsign) == 4, "settings_t layout");
_Static_assert(offsetof(settings_t, freq_hz) == 16, "settings_t layout");
_Static_assert(offsetof(settings_t, dev_hz) == 22, "settings_t layout");
_Static_assert(offsetof(settings_t, fox_aprs_every) == 34, "settings_t layout");
_Static_assert(offsetof(settings_t, gps_power) == 35, "settings_t layout");
_Static_assert(offsetof(settings_t, crc) == SETTINGS_SIZE - 2, "settings_t layout");

/* why a settings block was rejected (settings_check) */
enum settings_error {
	SETTINGS_OK = 0,
	SETTINGS_ERR_MAGIC, /* not a settings block (e.g. erased: FF) */
	SETTINGS_ERR_VERSION,
	SETTINGS_ERR_CRC,
	SETTINGS_ERR_CALLSIGN,
	SETTINGS_ERR_SSID,
	SETTINGS_ERR_SYMBOL,
	SETTINGS_ERR_PATH,
	SETTINGS_ERR_MODE,
	SETTINGS_ERR_FREQ,
	SETTINGS_ERR_POWER,
	SETTINGS_ERR_CW,
	SETTINGS_ERR_DEVIATION,
	SETTINGS_ERR_TRACKER_TIMING,
	SETTINGS_ERR_FOX_TIMING,
	SETTINGS_ERR_EEPROM, /* the EEPROM didn't answer */
	SETTINGS_ERR_GPS_POWER,
};

/* where the live settings came from */
enum settings_source {
	SETTINGS_FROM_DEFAULTS = 0, /* nothing valid in the EEPROM */
	SETTINGS_FROM_EEPROM,
	SETTINGS_FROM_SWD, /* applied over SWD, not saved yet */
};

/* the settings in use; change them only through settings_apply() */
extern settings_t cfg;
extern volatile uint8_t settings_source;
extern volatile uint8_t settings_error;	   /* enum settings_error of the last load */
extern volatile uint32_t settings_repairs; /* live settings found corrupt and restored */

/* settings.c: no hardware access, so the host tests can link it */
void settings_defaults(settings_t *s);
uint16_t crc16_ccitt(const void *data, size_t n);
uint16_t settings_crc(const settings_t *s);
void settings_seal(settings_t *s);
enum settings_error settings_check(const settings_t *s);
uint8_t settings_callsign_set(const settings_t *s);
const char *settings_callsign(void);
uint8_t settings_tx_allowed(void);
uint8_t settings_gps_always_on(void);

/* where tools/config (dfmcfg.py, dfmgui.py) finds the SWD mailbox without the
 * build's ELF: it scans flash for the magic (docs/config-gui.md). Append
 * fields only, and bump SWD_INFO_VERSION when the layout changes; the tools
 * check the layout against the ELF in CI (dfmcfg.py --check-elf) */
#define SWD_INFO_MAGIC	 "DFM-SWD"
#define SWD_INFO_VERSION 3

typedef struct {
	char magic[8];			  /* SWD_INFO_MAGIC */
	uint8_t version;		  /* SWD_INFO_VERSION */
	uint8_t size;			  /* sizeof(swd_info_t) */
	uint8_t settings_version; /* SETTINGS_VERSION */
	uint8_t settings_size;	  /* sizeof(settings_t) */
	const char *fw_version;
	settings_t *cfg;
	settings_t *settings_edit;
	volatile uint8_t *settings_cmd;
	volatile uint8_t *settings_result;
	volatile uint8_t *settings_source;
	volatile uint8_t *settings_error;
	volatile uint32_t *settings_done;
	volatile uint32_t *settings_repairs;
	volatile uint32_t *tx_refused;
	/* version 2: the send-now trigger (#51) */
	volatile uint8_t *aprs_send_now;
	volatile uint32_t *aprs_packets_sent;
	/* version 3: the analog calibration (#43; same mailbox, CMD_CAL_*), and
	 * the latest readings */
	cal_t *cal;
	cal_t *cal_edit;
	volatile uint8_t *cal_source;
	volatile uint8_t *cal_error;
	analog_reading *analog_last;
} swd_info_t;

extern const swd_info_t swd_info;

/* the mailbox commands (settings_cmd over SWD, or settings_command()) */
enum settings_cmd {
	CMD_NONE,
	CMD_EDIT,	  /* settings_edit = live settings */
	CMD_APPLY,	  /* check settings_edit and use it */
	CMD_SAVE,	  /* write the live settings to the EEPROM */
	CMD_RELOAD,	  /* load and use the EEPROM block, if valid */
	CMD_DEFAULTS, /* use the built-in defaults (not saved) */
	/* the same for the analog calibration (cal_edit, cal_error) */
	CMD_CAL_EDIT,
	CMD_CAL_APPLY,
	CMD_CAL_SAVE,
	CMD_CAL_RELOAD,
	CMD_CAL_DEFAULTS,
};

enum settings_result {
	RESULT_NONE,
	RESULT_OK,
	RESULT_INVALID, /* settings_error says why; nothing changed */
	RESULT_EEPROM,	/* EEPROM write or read-back failed */
	RESULT_BAD_CMD,
};

/* settings_store.c: EEPROM, the SWD mailbox, applying to the radio */
void settings_init(void);
uint8_t settings_apply(const settings_t *s, enum settings_source source);
void settings_poll(void);
uint8_t settings_command(uint8_t cmd);
extern settings_t settings_edit;
extern cal_t cal_edit;

#endif /* INC_SETTINGS_H_ */
