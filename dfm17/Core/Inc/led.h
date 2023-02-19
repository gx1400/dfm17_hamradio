/**
  ******************************************************************************
  * @file    led.c
  * @brief   This file provides code for the Red, Yellow, and Green LED
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

#ifndef INC_LED_H_
#define INC_LED_H_

#include "stm32f1xx_hal.h"

void ledOnGreen(void);
void ledOffGreen(void);
void ledToggleGreen(void);
void ledOnRed(void);
void ledOffRed(void);
void ledToggleRed(void);
void ledOnYellow(void);
void ledOffYellow(void);
void ledToggleYellow(void);


#endif /* INC_LED_H_ */
