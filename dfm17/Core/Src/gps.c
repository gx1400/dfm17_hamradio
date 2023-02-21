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
#include <stdio.h>

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
	GNSS_Handle.rxDone = 1; //todo try *GNSS to mitigate warning
}

/**
 * @brief Ubx packet crc checker
 * Checks if Ubx packet has proper crc (or if valid packet)
 * @param packet - packet reference
 * @param size - size of packet
 * @return - 1 = valid packet, 0 = not a valid packet
 */
uint8_t checkUbxCrc(uint8_t *packet, uint8_t size) {
    uint8_t CK_A = 0;
    uint8_t CK_B = 0;

    for(int x = 2; x < (size-2) ; x++) {
        CK_A = CK_A + packet[x];
        CK_B = CK_B + CK_A;
    }

    return (packet[size-2] == CK_A) && (packet[size-1] == CK_B);
}

/**
 * @brief ubx Packet builder
 * Build a ubx packet using a payload.  Packet param must be preallocated
 * to the correct size of the ending packet (payload + 4 bytes)
 * @param packet - preallocated (correctly sized) array for output packet
 * @param payload - array containing the payload
 * @param sizeOfPayload - size of the payload
 * @return - size of generated packet
 */
uint8_t buildUbxPacket(uint8_t *packet, uint8_t *payload, uint8_t sizeOfPayload) {
    packet[0] = 0xB5;
    packet[1] = 0x62;

    uint8_t CK_A = 0;
    uint8_t CK_B = 0;

    for(int x = 0; x < (sizeOfPayload) ; x++) {
        packet[2+x] = payload[x];
        CK_A = CK_A + payload[x];
        CK_B = CK_B + CK_A;
    }

    packet[sizeOfPayload+2] = CK_A;
    packet[sizeOfPayload+3] = CK_B;

    return (sizeOfPayload + 4);
}
