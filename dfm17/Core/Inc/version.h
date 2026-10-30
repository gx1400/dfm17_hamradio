/**
  ******************************************************************************
  * @file    version.h
  * @brief   This file contains all the function prototypes for
  *          the version.c file
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

#ifndef INC_VERSION_H_
#define INC_VERSION_H_


/* "DFMFW <git describe> <variant>", e.g. "DFMFW v0.1.0-3-gabc1234 tracker".
 * The marker lets tools/hw_survey/read_version.sh find it in a flash dump
 * without knowing which build is on the board. */
#define FW_VERSION_MARKER "DFMFW "

extern const char fw_version[];

#endif /* INC_VERSION_H_ */
