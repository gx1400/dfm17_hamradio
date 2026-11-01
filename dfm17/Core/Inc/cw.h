/**
  ******************************************************************************
  * @file    cw.h
  * @brief   Morse code as a keyed audio tone on the FM carrier (MCW)
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

#ifndef INC_CW_H_
#define INC_CW_H_

#include <stdint.h>

/* the Morse tone: TIM15 tick 26402.6 Hz / (2 * 16) = 825 Hz, the same as
 * the fox's hunt tone */
#define CW_TONE_HALF_TICKS 16

const char *cw_morse(char c);
void cw_tone(uint8_t on);
void cw_send(const char *text, uint8_t wpm);

#endif /* INC_CW_H_ */
