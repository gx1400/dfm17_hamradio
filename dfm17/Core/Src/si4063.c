/**
  ******************************************************************************
  * @file    init.c
  * @brief   This file contains all functions for initializing hardware
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 Derek Rowland <gx1400@gmail.com>
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
  * Leveraged Project
  *
  * - thasti/utrak
  *   - https://github.com/thasti/utrak
  *   - Stefan Biereigel
  *   - Modified 2023 in si4063.c and si4063.h
  ******************************************************************************
  */

#include "si4063.h"
#include "spi.h"
#include "tim.h"

/*
 * si4060_reset
 *
 * cleanly does the POR as specified in datasheet
 */
void si4060_reset(void) {
	si4060_shutdown();
	si4060_wakeup();
}

/*
 * si4060_shutdown
 *
 * makes the Si4060 go to shutdown state.
 * all register content is lost.
 */
void si4060_shutdown(void) {
	// SDN is PC3
	GPIOC->BSRR = (1U << (16+3));
	/* wait 10us */
	//__delay_cycles(50000);
	delay_us(10);
}

/*
 * si4060_wakeup
 *
 * wakes up the Si4060 from shutdown state.
 * si4060_power_up and si4060_setup have to be called afterwards
 */
void si4060_wakeup(void) {
	// SDN is PC3
	GPIOC->BSRR = (1U << (16+3));
	/* wait 20ms */
	//__delay_cycles(50000);
	delay_us(50);
	si4060_get_cts(0);
}

/*
 * si4060_part_info
 *
 * gets the PART_ID from the Si4060
 * this can be used to check for successful communication.
 * as the SPI bus returns 0xFF (MISO=high) when no slave is connected,
 * reading CTS can not verify communication to the Si4060.
 *
 * returns:	the PART_ID - should be 0x4060
 */
uint16_t si4060_part_info(void) {
	uint16_t temp;

	si4060_get_cts(0);
	temp = 0;
	spi_select();
	spi_write(CMD_PART_INFO);
	spi_deselect();
	si4060_get_cts(1);
	spi_read();	 	/* ignore CHIPREV */
	temp = spi_read(); 	/* read PART[0] */
	temp = temp << 8;
	temp |= spi_read(); 	/* read PART[1] */
	spi_deselect();
	return temp;
}

/*
 * si4060_get_cts
 *
 * waits for a CTS from the Si4060, includes a "time out"
 *
 * read_response: do not deselect the slave, if you want to read from it afterwards
 */
uint8_t si4060_get_cts(uint8_t read_response) {
	uint8_t temp = 0;
	uint8_t timeout = 0;
	if (!read_response) {
		while (si4060_read_cmd_buf(1) != 0xff && timeout < SI_TIMEOUT) {
			timeout++;
			delay_us(5);
		}
	} else {
		while (temp != 0xff && timeout < SI_TIMEOUT) {
			timeout++;
			temp = si4060_read_cmd_buf(0);
			if (temp != 0xff) {
				spi_deselect();
			}
		}
	}
	printf("timeout count: %d\r\n", timeout);
	return 0;

}

/*
 * si4060_read_cmd_buf
 *
 * reads the Si4060 command buffer from via SPI
 *
 * deselect: 	whether to deselect the slave after reading the response or not.
 * 		any command reading subsequent bytes after the CTS should use this
 * 		function to get CTS and continue doing spi_read afterwards
 * 		and finally deselecting the slave.
 *
 * returns:	the value of the first command buffer byte (i.e. CTS or not)
 */
uint8_t si4060_read_cmd_buf(uint8_t deselect) {
	uint8_t ret;
	spi_select();
	spi_write(CMD_READ_CMD_BUF);
	ret = spi_read();
	if (deselect) {
		spi_deselect();
	}
	return ret;
}





void __delay_cycles(uint32_t delay) {
	for (uint32_t x = 0; x < delay; x++);
}
