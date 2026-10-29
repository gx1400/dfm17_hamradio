/**
  ******************************************************************************
  * @file    tracker.h
  * @brief   This file contains all the function prototypes for
  *          the tracker.c file
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

#ifndef INC_TRACKER_H_
#define INC_TRACKER_H_

#include "GNSS.h"

/* one APRS packet per period, timed from one GPS wake-up to the next */
#define TRACKER_PERIOD_MS		300000

/* how long to wait for a fix before sending without a position */
#define TRACKER_FIX_TIMEOUT_MS	120000

/* how long a 3D fix must hold before it is sent, for a better position
 * after a hot start (NAV-PVT is polled every 5 s) */
#define TRACKER_SETTLE_MS		10000

void tracker_start(void);
void tracker_poll(GNSS_StateHandle *GNSS);

#endif /* INC_TRACKER_H_ */
