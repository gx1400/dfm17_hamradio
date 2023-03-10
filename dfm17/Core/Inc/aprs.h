/**
  ******************************************************************************
  * @file    aprs.h
  * @brief   This file contains all the function prototypes for
  *          the aprs.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) Stefan Biereigel
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
  * Based on code from https://github.com/bristol-seds/pico-tracker
  ******************************************************************************
  */

#ifndef INC_APRS_H_
#define INC_APRS_H_

#include <inttypes.h>
#include "GNSS.h"

void aprs_prepare_buffer(GNSS_StateHandle *GNSS, uint8_t backlog_fix);
void tx_aprs(void);


/* APRS destination SSID is 0 */
#define DST_SSID	0
/* APRS source SSID */
#define SRC_SSID	12
/* WIDE1-x SSID */
#define WIDE_SSID	1

/* data from matlab script */
#define APRS_MARK		0
#define APRS_SPACE		1
#define APRS_MARK_TICKS		11
#define APRS_SPACE_TICKS	6
#define APRS_BAUD_TICKS		22

/* AX.25 header consists of:
 * 	7 bytes source
 * 	7 bytes destination
 * 	7 bytes path
 * 	7 bytes repeater subfield
 * 	1 byte control field
 * 	1 byte PID field
 */
#define APRS_HEADER_LEN	23

#define PID_NONE	0xf0
#define CONTROL_UI	0x03
#define SSID_RESC	0x60
#define HEADER_END	0x01

#define APRS_TLM_TEMP_OFFSET	512

/*
 * buffer length
 * example: /ddhhmmz/xxxxyyyyOaa1|ss001122|
 * 1 report indicator (real time, no APRS messaging)
 * 2 + 2 + 2 + 1: day / hour / minute / zulu time indicator
 * 1 symbol table
 * 4 lat
 * 4 lon
 * 1 symbol
 * 3 compressed alt + indicator
 * 1 tlm delimiter
 * 2 tlm sequence id
 * 2 tlm temperature
 * 2 tlm battery
 * 2 tlm solar
 * 1 tlm delimiter
 */
#define APRS_BUF_LEN	1 + 7 + 1 + 4 + 4 + 1 + 3 + 1 + 2 + 2 + 2 + 2 + 1
//#define APRS_BUF_LEN 60

#define APRS_TIME_START	1
#define APRS_TIME_LEN	6
#define APRS_LAT_START	9
#define APRS_LAT_LEN	4
#define APRS_LON_START	13
#define APRS_LON_LEN	4
#define APRS_ALT_START	18
#define APRS_ALT_LEN	2
#define APRS_SEQ_START	22
#define APRS_SEQ_LEN	1
#define APRS_TEMP_START	24
#define APRS_TEMP_LEN	1
#define APRS_VOLT_START	26
#define APRS_VOLT_LEN	1
#define APRS_VSOL_START	28
#define APRS_VSOL_LEN	1

#define AX25_SFLAGS	75
#define AX25_EFLAGS	2

#define AX25_FLAG	0b01111110

#endif /* INC_APRS_H_ */
