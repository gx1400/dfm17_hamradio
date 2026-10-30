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

/* timing from the settings (cfg, settings.h):
 * - tracker_period_s: one APRS packet per period, from one GPS wake-up to the
 *   next (default 300 s);
 * - tracker_fix_timeout_s: how long to wait for a fix before sending without
 *   a position (120 s);
 * - tracker_settle_s: how long a 3D fix must hold before it is sent, for a
 *   better position after a hot start; NAV-PVT is polled every 5 s (10 s) */

void tracker_start(void);
void tracker_poll(GNSS_StateHandle *GNSS);

#endif /* INC_TRACKER_H_ */
