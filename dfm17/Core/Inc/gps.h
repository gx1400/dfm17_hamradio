/**
  ******************************************************************************
  * @file    gps.h
  * @brief   This file contains all the function prototypes for
  *          the gps.c file
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
  */

#ifndef INC_GPS_H_
#define INC_GPS_H_

#include "stm32f1xx_hal.h"

void gpsUpdate(void);
void assertGpsLock(void);
void deassertGpsLock(void);

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);

uint8_t checkUbxCrc(uint8_t *packet, uint8_t size);
uint8_t buildUbxPacket(uint8_t *packet, uint8_t *payload, uint8_t sizeOfPayload);


#endif /* INC_GPS_H_ */
