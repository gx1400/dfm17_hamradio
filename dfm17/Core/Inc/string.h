/**
  ******************************************************************************
  * @file    string.h
  * @brief   This file contains all the function prototypes for
  *          the string.c file
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

#include <inttypes.h>

void i16toa(uint16_t in, uint8_t len, volatile char *out);
uint8_t i16toav(uint16_t in, volatile char *out);
