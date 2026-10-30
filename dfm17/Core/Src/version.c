/**
  ******************************************************************************
  * @file    version.c
  * @brief   Firmware version string
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
  * FW_VERSION (git describe --tags --always --dirty) and FW_VARIANT (tracker,
  * fox, ...) come from the Makefile. STM32CubeIDE builds don't set them and
  * report "unknown".
  ******************************************************************************
  */

#include "version.h"

#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif
#ifndef FW_VARIANT
#define FW_VARIANT "cubeide"
#endif

/* printed at boot (initHw), which also keeps it in the image with
 * --gc-sections, so it can always be found in a flash dump */
const char fw_version[] = FW_VERSION_MARKER FW_VERSION " " FW_VARIANT;
