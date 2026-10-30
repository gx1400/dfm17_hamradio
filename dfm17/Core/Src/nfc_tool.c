/**
  ******************************************************************************
  * @file    nfc_tool.c
  * @brief   Test build: inspect and open the M24LR04E-R sector 3 for NFC
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
  * Built when HW_NFC is defined (make variant nfc_tool). Does nothing on its
  * own: commands are written over SWD into the nfc_tool mailbox one step at a
  * time, and the results are read back from it (tools/hw_survey/nfc_tool.sh).
  * Issue #8.
  *
  * The only write allowed is the sector security status byte of sector 3, the
  * free sector; Graw's sectors 0-2, the passwords, the I2C_Write_Lock bits and
  * the configuration byte are never written.
  *
  * PC13 (the chip's RF WIP/BUSY output, open drain, pulled up) is watched all
  * the time, so a phone reading or writing the tag shows up as low pulses.
  ******************************************************************************
  */

#include "nfc_tool.h"
#include "main.h"
#include "i2c_ee.h"

enum {
	NFC_CMD_NONE,
	NFC_CMD_READ,		/* read the system area fields below */
	NFC_CMD_PRESENT,	/* present the I2C password in arg_password */
	NFC_CMD_WRITE_SSS3,	/* write arg_value to SSS[3], then read it back */
};

enum {
	NFC_OK = 1,
	NFC_NACK,			/* the EEPROM didn't acknowledge every byte */
	NFC_TIMEOUT,		/* no answer after the write cycle */
	NFC_BAD_CMD,
};

struct {
	/* written over SWD */
	uint8_t cmd;				/* NFC_CMD_*, cleared when done */
	uint8_t arg_value;
	uint8_t pad[2];
	uint32_t arg_password;
	/* results */
	uint32_t done;				/* commands completed */
	uint8_t status;				/* NFC_OK, ... of the last command */
	uint8_t poll_ms;			/* write cycle / password compare time */
	uint8_t sss[4];				/* sector security status, sectors 0-3 */
	uint8_t i2c_lock[2];
	uint8_t config;				/* bit 3: RF WIP/BUSY mode */
	uint8_t control;
	uint8_t uid[8];				/* LSB first */
	uint8_t read_ok;
	uint8_t pc13;				/* current RF WIP/BUSY level */
	uint32_t pc13_low_count;	/* falling edges seen on PC13 */
	uint32_t pc13_last_low_ms;
} volatile nfc_tool;

static uint8_t read_system(void) {
	return ee_read(EE_SYS_ADDR, EE_SYS_SSS, nfc_tool.sss, 4)
			&& ee_read(EE_SYS_ADDR, EE_SYS_I2C_LOCK, nfc_tool.i2c_lock, 2)
			&& ee_read(EE_SYS_ADDR, EE_SYS_CONFIG, &nfc_tool.config, 1)
			&& ee_read(EE_SYS_ADDR, EE_SYS_UID, nfc_tool.uid, 8)
			&& ee_read(EE_SYS_ADDR, EE_SYS_CONTROL, &nfc_tool.control, 1);
}

static uint8_t run(uint8_t cmd) {
	switch (cmd) {
	case NFC_CMD_READ:
		nfc_tool.read_ok = read_system();
		return nfc_tool.read_ok ? NFC_OK : NFC_NACK;
	case NFC_CMD_PRESENT:
		if (!ee_present_password(nfc_tool.arg_password))
			return NFC_NACK;
		nfc_tool.poll_ms = ee_poll(EE_SYS_ADDR);
		return nfc_tool.poll_ms == 0xFF ? NFC_TIMEOUT : NFC_OK;
	case NFC_CMD_WRITE_SSS3: {
		uint8_t v = nfc_tool.arg_value;
		uint8_t ok = ee_write(EE_SYS_ADDR, EE_SYS_SSS + 3, &v, 1);
		nfc_tool.poll_ms = ee_poll(EE_SYS_ADDR);
		nfc_tool.read_ok = read_system();
		if (!ok)
			return NFC_NACK;
		return nfc_tool.poll_ms == 0xFF ? NFC_TIMEOUT : NFC_OK;
	}
	default:
		return NFC_BAD_CMD;
	}
}

void nfc_tool_start(void) {
	i2c_init();
	nfc_tool.read_ok = read_system();
}

/*
 * nfc_tool_poll
 *
 * call from the main loop: runs a command written over SWD and watches PC13
 */
void nfc_tool_poll(void) {
	uint8_t pc13 = (GPIOC->IDR >> 13) & 1;

	if (nfc_tool.pc13 && !pc13) {
		nfc_tool.pc13_low_count++;
		nfc_tool.pc13_last_low_ms = HAL_GetTick();
	}
	nfc_tool.pc13 = pc13;

	uint8_t cmd = nfc_tool.cmd;
	if (cmd == NFC_CMD_NONE)
		return;
	nfc_tool.status = run(cmd);
	nfc_tool.done++;
	nfc_tool.cmd = NFC_CMD_NONE;
}
