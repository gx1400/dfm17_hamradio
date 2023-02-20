/**
  ******************************************************************************
  * @file    gps.c
  * @brief   This file contains all functions for integrating GNSS library
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


#include "gps.h"
#include "GNSS.h"
#include "usart.h"

extern GNSS_StateHandle GNSS_Handle;


void gpsUpdate(void) {
		printf("GPS Update!\r\n");

		if( GNSS_Handle.uniqueID[0] == 0x00 && GNSS_Handle.uniqueID[1] == 0x00 &&
				GNSS_Handle.uniqueID[2] == 0x00 && GNSS_Handle.uniqueID[3] == 0x00 &&
				GNSS_Handle.uniqueID[4] == 0x00) {
			GNSS_GetUniqID(&GNSS_Handle);

		}

		GNSS_GetPVTData(&GNSS_Handle);


		if(GNSS_Handle.selectedMode == ModeNotSet){
			GNSS_SetMode(&GNSS_Handle,ModeAutomotive);
		}

		printf("Status of fix: %d \r\n", GNSS_Handle.fixType);

		if(GNSS_Handle.fixType >= Fix2D) {
			printf("Day: %d-%02d-%02d \r\n", GNSS_Handle.year, GNSS_Handle.month,GNSS_Handle.day);
			printf("Time: %02d:%02d:%02d UTC \r\n", GNSS_Handle.hour, GNSS_Handle.min,GNSS_Handle.sec);

			printf("Number of Sats: %d \r\n", GNSS_Handle.numSV);

			printf("Latitude: %f \r\n", GNSS_Handle.fLat);
			printf("Longitude: %f \r\n",(float) GNSS_Handle.lon / 10000000.0);



		}
		printf("Unique ID: %02X %02X %02X %02X %02X \r\n",
			GNSS_Handle.uniqueID[0], GNSS_Handle.uniqueID[1],
			GNSS_Handle.uniqueID[2], GNSS_Handle.uniqueID[3],
			GNSS_Handle.uniqueID[4]);
		printf("\r\n");

}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)	{
	//printf("  TxComplete callback!\r\n");
	GNSS_Handle.txDone = 1;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	//printf("  RxComplete callback!\r\n");
	GNSS_ParseBuffer(&GNSS_Handle);
	GNSS_Handle.rxDone = 1;
}


