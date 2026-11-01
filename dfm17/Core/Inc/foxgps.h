/**
  ******************************************************************************
  * @file    foxgps.h
  * @brief   This file contains all the function prototypes for
  *          the foxgps.c file
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

#ifndef INC_FOXGPS_H_
#define INC_FOXGPS_H_

#include <stdint.h>
#include "GNSS.h"

/* asleep this long, the GPS wakes to refresh the fix and the time */
#define FOX_GPS_RESYNC_S	   3600
/* if the PPS is still there this long after a sleep command, the GPS didn't
 * take it (seen right after a reset): send it again, up to FOX_GPS_SLEEP_TRIES */
#define FOX_GPS_SLEEP_CHECK_MS 5000
#define FOX_GPS_SLEEP_TRIES	   3

void fox_gps_start(void);
void fox_gps_poll(GNSS_StateHandle *GNSS, uint32_t now);
/* the last 3D fix, or NULL if there hasn't been one */
GNSS_StateHandle *fox_gps_last_fix(void);
uint8_t fox_gps_asleep(void);

#endif /* INC_FOXGPS_H_ */
