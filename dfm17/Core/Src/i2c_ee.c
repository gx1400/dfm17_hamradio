/**
  ******************************************************************************
  * @file    i2c_ee.c
  * @brief   M24LR04E-R EEPROM over I2C, bit-banged on PB6 (SCL) / PB7 (SDA)
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
  * Shared by the hardware survey and the NFC tool test builds, and meant for
  * the settings block later. The bus is driven open-drain from GPIO registers,
  * about 100 kHz, with clock stretching allowed; no HAL I2C module needed.
  ******************************************************************************
  */

#include "i2c_ee.h"
#include "main.h"
#include "tim.h"

#define SCL_H()	(GPIOB->BSRR = GPIO_PIN_6)
#define SCL_L()	(GPIOB->BRR = GPIO_PIN_6)
#define SDA_H()	(GPIOB->BSRR = GPIO_PIN_7)
#define SDA_L()	(GPIOB->BRR = GPIO_PIN_7)
#define SDA_IN()	((GPIOB->IDR >> 7) & 1)
#define SCL_IN()	((GPIOB->IDR >> 6) & 1)

static void i2c_wait(void) {
	delay_us(5);
}

void i2c_init(void) {
	SET_BIT(RCC->APB2ENR, RCC_APB2ENR_IOPBEN);
	SCL_H();
	SDA_H();
	/* PB6, PB7: open-drain output, 2 MHz (CNF 01, MODE 10) */
	MODIFY_REG(GPIOB->CRL, 0xFFUL << 24, 0x66UL << 24);
	i2c_wait();
}

static void scl_release(void) {
	SCL_H();
	/* allow clock stretching, up to ~1 ms */
	for (int i = 0; i < 200 && !SCL_IN(); i++)
		i2c_wait();
	i2c_wait();
}

void i2c_start(void) {
	SDA_H();
	scl_release();
	SDA_L();
	i2c_wait();
	SCL_L();
	i2c_wait();
}

void i2c_stop(void) {
	SDA_L();
	i2c_wait();
	scl_release();
	SDA_H();
	i2c_wait();
}

/* returns 1 if the byte was ACKed */
uint8_t i2c_write(uint8_t b) {
	for (int i = 7; i >= 0; i--) {
		if (b & (1 << i))
			SDA_H();
		else
			SDA_L();
		i2c_wait();
		scl_release();
		SCL_L();
	}
	SDA_H();
	i2c_wait();
	scl_release();
	uint8_t ack = !SDA_IN();
	SCL_L();
	i2c_wait();
	return ack;
}

uint8_t i2c_read(uint8_t ack) {
	uint8_t b = 0;
	SDA_H();
	for (int i = 7; i >= 0; i--) {
		i2c_wait();
		scl_release();
		b |= SDA_IN() << i;
		SCL_L();
	}
	if (ack)
		SDA_L();
	i2c_wait();
	scl_release();
	SCL_L();
	SDA_H();
	i2c_wait();
	return b;
}

/* random read with a 16-bit memory address; returns 1 on success */
uint8_t ee_read(uint8_t dev, uint16_t addr, volatile uint8_t *buf, uint16_t len) {
	i2c_start();
	if (!i2c_write(dev << 1) || !i2c_write(addr >> 8) || !i2c_write(addr & 0xFF)) {
		i2c_stop();
		return 0;
	}
	i2c_start();
	if (!i2c_write((dev << 1) | 1)) {
		i2c_stop();
		return 0;
	}
	for (uint16_t i = 0; i < len; i++)
		buf[i] = i2c_read(i + 1 < len);
	i2c_stop();
	return 1;
}

/* page write (up to 4 bytes, same 4-byte row) with a 16-bit memory address.
 * returns 1 if every byte was ACKed */
uint8_t ee_write(uint8_t dev, uint16_t addr, const uint8_t *buf, uint8_t len) {
	uint8_t ok;
	i2c_start();
	ok = i2c_write(dev << 1) && i2c_write(addr >> 8) && i2c_write(addr & 0xFF);
	for (uint8_t i = 0; ok && i < len; i++)
		ok = i2c_write(buf[i]);
	i2c_stop();
	return ok;
}

/* ACK polling: ms until the device answers again after a write, 0xFF if never */
uint8_t ee_poll(uint8_t dev) {
	for (uint8_t ms = 0; ms < 50; ms++) {
		i2c_start();
		uint8_t ack = i2c_write(dev << 1);
		i2c_stop();
		if (ack)
			return ms;
		HAL_Delay(1);
	}
	return 0xFF;
}


/* bit0 SCL, bit1 SDA high when released */
uint8_t i2c_bus_idle(void) {
	return SCL_IN() | (SDA_IN() << 1);
}

/*
 * ee_present_password
 *
 * I2C present password: 0x0900, password (MSB first), validation code 0x09,
 * password again. A wrong password has no side effect (no lockout or counter);
 * the protected sectors just stay protected. The device compares during a
 * write-cycle time and doesn't answer meanwhile, so poll afterwards. A correct
 * password opens the I2C security session until power-off.
 *
 * returns 1 if every byte was ACKed (which doesn't say whether it matched)
 */
uint8_t ee_present_password(uint32_t password) {
	uint8_t frame[9];

	for (int i = 0; i < 4; i++)
		frame[i] = frame[5 + i] = password >> (24 - 8 * i);
	frame[4] = 0x09;
	i2c_start();
	uint8_t ok = i2c_write(EE_SYS_ADDR << 1) && i2c_write(EE_SYS_PASSWORD >> 8)
			&& i2c_write(EE_SYS_PASSWORD & 0xFF);
	for (int i = 0; ok && i < 9; i++)
		ok = i2c_write(frame[i]);
	i2c_stop();
	return ok;
}
