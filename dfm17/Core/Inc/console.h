/**
  ******************************************************************************
  * @file    console.h
  * @brief   This file contains all the function prototypes for
  *          the console.c and console_hw.c files
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

#ifndef INC_CONSOLE_H_
#define INC_CONSOLE_H_

#include <stdarg.h>
#include <stdint.h>

/* the serial console on the mini-USB connector (USART1, PA9 TX on D+, PA10
 * RX on D-, 115200 8N1), on only while USB power is present (#50).
 * docs/serial-console.md has the commands and the protocol. */
#define CONSOLE_PROTO	 1
/* "cal put " + 64 hex digits fits; "cfg put " + 128 does too */
#define CONSOLE_LINE_MAX 144

/* console.c: the protocol, no hardware access (host-tested) */
void console_init(uint8_t on_usb);
void console_usb(uint8_t on_usb);
void console_rx(uint8_t c);
void console_poll(void);
uint8_t console_enabled(void);
uint8_t console_config_mode(void);
uint8_t console_log_on(void);
uint8_t console_output_on(void);
void console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* console_hw.c: USART1, the pins, and the board facts for ver/status */
void console_hw_enable(uint8_t on);
void console_hw_send(const char *s, uint16_t n);
void console_hw_vprintf(const char *fmt, va_list ap);
void console_hw_ver(void);
void console_hw_status(void);
void console_hw_reset(void);

#endif /* INC_CONSOLE_H_ */
