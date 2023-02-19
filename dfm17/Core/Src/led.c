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


#include "led.h"


// Green LED is PC6
void ledOnGreen(void) {
	GPIOC->BSRR = (1 << 6);
}

void ledOffGreen(void) {
	GPIOC->BSRR = (1 << (16+6));
}

void ledToggleGreen(void) {
	GPIOC->ODR ^= (1 << 6);
}

// Red LED is PB12
void ledOnRed(void) {
	GPIOB->BSRR = (1 << 12);
}

void ledOffRed(void) {
	GPIOB->BSRR = (1 << (16+12));
}

void ledToggleRed(void) {
	GPIOB->ODR ^= (1 << 12);
}

// Yellow LED is PC7
void ledOnYellow(void) {
	GPIOC->BSRR = (1 << 7);
}

void ledOffYellow(void) {
	GPIOC->BSRR = (1 << (16+7));
}

void ledToggleYellow(void) {
	GPIOC->ODR ^= (1 << 7);
}
