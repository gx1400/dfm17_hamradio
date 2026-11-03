/**
  ******************************************************************************
  * @file    posdither.h
  * @brief   fox mode: report a randomly offset position, the area rather than
  *          the spot (#72)
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

#ifndef INC_POSDITHER_H_
#define INC_POSDITHER_H_

#include <inttypes.h>
#include "settings.h"

/* the PRNG: seeded at boot from the UID, ADC noise and the cycle counters,
 * stirred with every 1PPS timing sample, so the offsets differ every hunt */
void posdither_seed(uint32_t seed);
void posdither_stir(uint32_t x);
uint32_t posdither_rand(void);

/* the dither radius in m, ms after the start (boot, for now): pos_dither_m,
 * or shrinking linearly to pos_dither_min_m over pos_dither_minutes */
uint32_t posdither_radius_m(const settings_t *s, uint32_t ms);

/* cos(latitude) in 1/65535, for the longitude scale; |lat| > 89 deg is held
 * at cos(89 deg) */
uint32_t posdither_cos_q16(int32_t lat_e7);

/* offsets a position (1e-7 deg, mm above MSL) by a fresh random draw, uniform
 * over a disc of the current radius, the altitude by up to +-pos_dither_alt_m.
 * returns 1 if it changed anything (dithering on), 0 if not */
uint8_t posdither_apply(
		const settings_t *s, uint32_t ms, int32_t *lat_e7, int32_t *lon_e7, int32_t *hmsl_mm);

#endif /* INC_POSDITHER_H_ */
