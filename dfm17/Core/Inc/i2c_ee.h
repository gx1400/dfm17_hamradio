/**
  ******************************************************************************
  * @file    i2c_ee.h
  * @brief   This file contains all the function prototypes for
  *          the i2c_ee.c file
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

#ifndef INC_I2C_EE_H_
#define INC_I2C_EE_H_

#include <stdint.h>

#define EE_USER_ADDR	0x53	/* M24LR04E-R user memory, 7-bit address (E2 = 0) */
#define EE_SYS_ADDR		0x57	/* M24LR04E-R system area (E2 = 1) */

/* system area addresses (M24LR04E-R datasheet, DocID022208 Rev 10) */
#define EE_SYS_SSS			0x0000	/* sector security status, 1 byte per sector */
#define EE_SYS_I2C_LOCK		0x0800	/* I2C_Write_Lock bits, sectors 0-3 in bits 3:0 */
#define EE_SYS_PASSWORD		0x0900	/* I2C present / write password */
#define EE_SYS_UID			0x0914	/* 8 bytes, LSB first */
#define EE_SYS_CONFIG		0x0910	/* bit 3: RF WIP/BUSY pin, 1 = RF WIP mode */
#define EE_SYS_CONTROL		0x0920	/* volatile: energy harvesting, FIELD_ON */

void i2c_init(void);
void i2c_start(void);
void i2c_stop(void);
uint8_t i2c_write(uint8_t b);
uint8_t i2c_read(uint8_t ack);
uint8_t i2c_bus_idle(void);
uint8_t ee_read(uint8_t dev, uint16_t addr, volatile uint8_t *buf, uint16_t len);
uint8_t ee_write(uint8_t dev, uint16_t addr, const uint8_t *buf, uint8_t len);
uint8_t ee_poll(uint8_t dev);
uint8_t ee_present_password(uint32_t password);

#endif /* INC_I2C_EE_H_ */
