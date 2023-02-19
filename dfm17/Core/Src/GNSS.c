/*
 * GNSS.c
 *
 *  Created on: 03.10.2020
 *      Author: SimpleMethod
 *
 *Copyright 2020 SimpleMethod
 *
 *Permission is hereby granted, free of charge, to any person obtaining a copy of
 *this software and associated documentation files (the "Software"), to deal in
 *the Software without restriction, including without limitation the rights to
 *use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *of the Software, and to permit persons to whom the Software is furnished to do
 *so, subject to the following conditions:
 *
 *The above copyright notice and this permission notice shall be included in all
 *copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *THE SOFTWARE.
 *
 * Modified by Derek Rowland (gx1400@gmail.com) - 2023-02-15
 * - changing receive to use DMA
 * - added some additional values to GNSS_Init
 * - added an rx/tx interlock with DMA interrupts
 ******************************************************************************
 */

#include "GNSS.h"

volatile union u_Short uShort;
volatile union i_Short iShort;
volatile union u_Long uLong;
volatile union i_Long iLong;

/*!
 * Structure initialization.
 * @param GNSS Pointer to main GNSS structure.
 * @param huart Pointer to uart handle.
 */
void GNSS_Init(GNSS_StateHandle *GNSS, UART_HandleTypeDef *huart, uint8_t *txDone, uint8_t *rxDone) {
	GNSS->huart = huart;
	GNSS->txDone = txDone;
	GNSS->rxDone = rxDone;
	GNSS->year = 0;
	GNSS->month = 0;
	GNSS->day = 0;
	GNSS->hour = 0;
	GNSS->min = 0;
	GNSS->sec = 0;
	GNSS->fixType = 0;
	GNSS->numSV = 0;
	GNSS->lon = 0;
	GNSS->lat = 0;
	GNSS->height = 0;
	GNSS->hMSL = 0;
	GNSS->hAcc = 0;
	GNSS->vAcc = 0;
	GNSS->gSpeed = 0;
	GNSS->headMot = 0;
	GNSS->uniqueID[0] = 0;
	GNSS->uniqueID[1] = 0;
	GNSS->uniqueID[2] = 0;
	GNSS->uniqueID[3] = 0;
	GNSS->uniqueID[4] = 0;
	GNSS->selectedMode = ModeNotSet;

}

/*!
 * Searching for a header in data buffer and matching class and message ID to buffer data.
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_ParseBuffer(GNSS_StateHandle *GNSS) {
	//printf("Parsing Buffer...\r\n");
	for (int var = 0; var <= 100; ++var) {
		if (GNSS->uartWorkingBuffer[var] == 0xB5
				&& GNSS->uartWorkingBuffer[var + 1] == 0x62) {
			if (GNSS->uartWorkingBuffer[var + 2] == 0x27
					&& GNSS->uartWorkingBuffer[var + 3] == 0x03) { //Look at: 32.19.1.1 u-blox 8 Receiver description
				GNSS_ParseUniqID(GNSS);
			} else if (GNSS->uartWorkingBuffer[var + 2] == 0x01
					&& GNSS->uartWorkingBuffer[var + 3] == 0x21) { //Look at: 32.17.14.1 u-blox 8 Receiver description
				GNSS_ParseNavigatorData(GNSS);
			} else if (GNSS->uartWorkingBuffer[var + 2] == 0x01
					&& GNSS->uartWorkingBuffer[var + 3] == 0x07) { //ook at: 32.17.30.1 u-blox 8 Receiver description
				GNSS_ParsePVTData(GNSS);
			} else if (GNSS->uartWorkingBuffer[var + 2] == 0x01
					&& GNSS->uartWorkingBuffer[var + 3] == 0x02) { // Look at: 32.17.15.1 u-blox 8 Receiver description
				GNSS_ParsePOSLLHData(GNSS);
			}
		}
	}
}

/*!
 * Make request for unique chip ID data.
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_GetUniqID(GNSS_StateHandle *GNSS) {
	//printf("Sending GetUniqID...\r\n");
	GNSS->txDone = 0x00;
	GNSS->rxDone = 0x00;
	HAL_UART_Transmit_DMA(GNSS->huart, getDeviceID,
			sizeof(getDeviceID) / sizeof(uint8_t));
	HAL_UART_Receive_DMA(GNSS->huart, GNSS->uartWorkingBuffer, 17);
	while((GNSS->txDone == 0x00) || (GNSS->rxDone == 0x00)) {};
}

/*!
 * Make request for UTC time solution data.
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_GetNavigatorData(GNSS_StateHandle *GNSS) {
	//printf("Sending GetNavigatorData...\r\n");
	GNSS->txDone = 0x00;
	GNSS->rxDone = 0x00;
	HAL_UART_Transmit_DMA(GNSS->huart, getNavigatorData,
			sizeof(getNavigatorData) / sizeof(uint8_t));
	HAL_UART_Receive_DMA(GNSS->huart, GNSS->uartWorkingBuffer, 28);
	while((GNSS->txDone == 0x00) || (GNSS->rxDone == 0x00)) {};
}

/*!
 * Make request for geodetic position solution data.
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_GetPOSLLHData(GNSS_StateHandle *GNSS) {
	//printf("Sending GetPOSLLHData...\r\n");
	GNSS->txDone = 0x00;
	GNSS->rxDone = 0x00;
	HAL_UART_Transmit_DMA(GNSS->huart, getPOSLLHData,
			sizeof(getPOSLLHData) / sizeof(uint8_t));
	HAL_UART_Receive_DMA(GNSS->huart, GNSS->uartWorkingBuffer, 36);
	while((GNSS->txDone == 0x00) || (GNSS->rxDone == 0x00)) {};
}

/*!
 * Make request for navigation position velocity time solution data.
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_GetPVTData(GNSS_StateHandle *GNSS) {
	//printf("Sending GetPVTData...\r\n");
	GNSS->txDone = 0x00;
	GNSS->rxDone = 0x00;
	HAL_UART_Transmit_DMA(GNSS->huart, getPVTData,
			sizeof(getPVTData) / sizeof(uint8_t));
	HAL_UART_Receive_DMA(GNSS->huart, GNSS->uartWorkingBuffer, 100);
	while((GNSS->txDone == 0x00) || (GNSS->rxDone == 0x00)) {};
}

/*!
 * Parse data to unique chip ID standard.
 * Look at: 32.19.1.1 u-blox 8 Receiver description
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_ParseUniqID(GNSS_StateHandle *GNSS) {
	//printf("Parsing GetUniqID...\r\n");
	for (int var = 0; var < 5; ++var) {
		GNSS->uniqueID[var] = GNSS->uartWorkingBuffer[10 + var];
	}
}

/*!
 * Changing the GNSS mode.
 * Look at: 32.10.19 u-blox 8 Receiver description
 */
void GNSS_SetMode(GNSS_StateHandle *GNSS, short gnssMode) {
	GNSS->txDone = 0x00;
	GNSS->rxDone = 0x00;

	if (gnssMode == 0) {
		HAL_UART_Transmit_DMA(GNSS->huart, setPortableMode,sizeof(setPortableMode) / sizeof(uint8_t));
	} else if (gnssMode == 1) {
		HAL_UART_Transmit_DMA(GNSS->huart, setStationaryMode,sizeof(setStationaryMode) / sizeof(uint8_t));
	} else if (gnssMode == 2) {
		HAL_UART_Transmit_DMA(GNSS->huart, setPedestrianMode,sizeof(setPedestrianMode) / sizeof(uint8_t));
	} else if (gnssMode == 3) {
		HAL_UART_Transmit_DMA(GNSS->huart, setAutomotiveMode,sizeof(setAutomotiveMode) / sizeof(uint8_t));
	} else if (gnssMode == 4) {
		HAL_UART_Transmit_DMA(GNSS->huart, setAutomotiveMode,sizeof(setAutomotiveMode) / sizeof(uint8_t));
	} else if (gnssMode == 5) {
		HAL_UART_Transmit_DMA(GNSS->huart, setAirbone1GMode,sizeof(setAirbone1GMode) / sizeof(uint8_t));
	} else if (gnssMode == 6) {
		HAL_UART_Transmit_DMA(GNSS->huart, setAirbone2GMode,sizeof(setAirbone2GMode) / sizeof(uint8_t));
	} else if (gnssMode == 7) {
		HAL_UART_Transmit_DMA(GNSS->huart, setAirbone4GMode,sizeof(setAirbone4GMode) / sizeof(uint8_t));
	} else if (gnssMode == 8) {
		HAL_UART_Transmit_DMA(GNSS->huart, setWristMode,sizeof(setWristMode) / sizeof(uint8_t));
	} else if (gnssMode == 9) {
		HAL_UART_Transmit_DMA(GNSS->huart, setBikeMode,sizeof(setBikeMode) / sizeof(uint8_t));
	}
	HAL_UART_Receive_DMA(GNSS->huart, GNSS->uartWorkingBuffer, 10);
	while((GNSS->txDone == 0x00) || (GNSS->rxDone == 0x00)) {};

	GNSS->selectedMode = gnssMode;
}
/*!
 * Parse data to navigation position velocity time solution standard.
 * Look at: 32.17.15.1 u-blox 8 Receiver description.
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_ParsePVTData(GNSS_StateHandle *GNSS) {
	//printf("Parsing PVT Data...\r\n");
	uShort.bytes[0] = GNSS->uartWorkingBuffer[10];
	GNSS->yearBytes[0]=GNSS->uartWorkingBuffer[10];
	uShort.bytes[1] = GNSS->uartWorkingBuffer[11];
	GNSS->yearBytes[1]=GNSS->uartWorkingBuffer[11];
	GNSS->year = uShort.uShort;
	GNSS->month = GNSS->uartWorkingBuffer[12];
	GNSS->day = GNSS->uartWorkingBuffer[13];
	GNSS->hour = GNSS->uartWorkingBuffer[14];
	GNSS->min = GNSS->uartWorkingBuffer[15];
	GNSS->sec = GNSS->uartWorkingBuffer[16];
	GNSS->fixType = GNSS->uartWorkingBuffer[26];
	GNSS->numSV = GNSS->uartWorkingBuffer[29];

	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 30];
		GNSS->lonBytes[var]= GNSS->uartWorkingBuffer[var + 30];
	}
	GNSS->lon = iLong.iLong;
	GNSS->fLon=(float)iLong.iLong/10000000.0;
	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 34];
		GNSS->latBytes[var]=GNSS->uartWorkingBuffer[var + 34];
	}
	GNSS->lat = iLong.iLong;
	GNSS->fLat=(float)iLong.iLong/10000000.0;
	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 38];
	}
	GNSS->height = iLong.iLong;

	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 42];
		GNSS->hMSLBytes[var] = GNSS->uartWorkingBuffer[var + 42];
	}
	GNSS->hMSL = iLong.iLong;

	for (int var = 0; var < 4; ++var) {
		uLong.bytes[var] = GNSS->uartWorkingBuffer[var + 46];
	}
	GNSS->hAcc = uLong.uLong;

	for (int var = 0; var < 4; ++var) {
		uLong.bytes[var] = GNSS->uartWorkingBuffer[var + 50];
	}
	GNSS->vAcc = uLong.uLong;

	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 66];
		GNSS->gSpeedBytes[var] = GNSS->uartWorkingBuffer[var + 66];
	}
	GNSS->gSpeed = iLong.iLong;

	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 70];
	}
	GNSS->headMot = iLong.iLong * 1e-5; // todo I'm not sure this good options.
}

/*!
 * Parse data to UTC time solution standard.
 * Look at: 32.17.30.1 u-blox 8 Receiver description.
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_ParseNavigatorData(GNSS_StateHandle *GNSS) {
	//printf("Parsing Navigator Data...\r\n");
	uShort.bytes[0] = GNSS->uartWorkingBuffer[18];
	uShort.bytes[1] = GNSS->uartWorkingBuffer[19];
	GNSS->year = uShort.uShort;
	GNSS->month = GNSS->uartWorkingBuffer[20];
	GNSS->day = GNSS->uartWorkingBuffer[21];
	GNSS->hour = GNSS->uartWorkingBuffer[22];
	GNSS->min = GNSS->uartWorkingBuffer[23];
	GNSS->sec = GNSS->uartWorkingBuffer[24];
}

/*!
 * Parse data to geodetic position solution standard.
 * Look at: 32.17.14.1 u-blox 8 Receiver description.
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_ParsePOSLLHData(GNSS_StateHandle *GNSS) {
	//printf("Parsing POS LLH Data...\r\n");
	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 10];
	}
	GNSS->lon = iLong.iLong;
	GNSS->fLon=(float)iLong.iLong/10000000.0;

	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 14];
	}
	GNSS->lat = iLong.iLong;
	GNSS->fLat=(float)iLong.iLong/10000000.0;

	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 18];
	}
	GNSS->height = iLong.iLong;

	for (int var = 0; var < 4; ++var) {
		iLong.bytes[var] = GNSS->uartWorkingBuffer[var + 22];
	}
	GNSS->hMSL = iLong.iLong;

	for (int var = 0; var < 4; ++var) {
		uLong.bytes[var] = GNSS->uartWorkingBuffer[var + 26];
	}
	GNSS->hAcc = uLong.uLong;

	for (int var = 0; var < 4; ++var) {
		uLong.bytes[var] = GNSS->uartWorkingBuffer[var + 30];
	}
	GNSS->vAcc = uLong.uLong;
}

/*!
 *  Sends the basic configuration: Activation of the UBX standard, change of NMEA version to 4.10 and turn on of the Galileo system.
 * @param GNSS Pointer to main GNSS structure.
 */
void GNSS_LoadConfig(GNSS_StateHandle *GNSS) {
	printf("Sending ubx config...\r\n");
	GNSS->txDone = 0x00;
	GNSS->rxDone = 0x00;
	HAL_UART_Transmit_DMA(GNSS->huart, configUBX,
			sizeof(configUBX) / sizeof(uint8_t));
	HAL_UART_Receive_DMA(GNSS->huart, GNSS->uartWorkingBuffer, 10);
	while((GNSS->txDone == 0x00) || (GNSS->rxDone == 0x00)) {};

	printf("Sending NMEA410 config...\r\n");
	GNSS->txDone = 0x00;
	GNSS->rxDone = 0x00;
	HAL_UART_Transmit_DMA(GNSS->huart, setNMEA410,
			sizeof(setNMEA410) / sizeof(uint8_t));
	HAL_UART_Receive_DMA(GNSS->huart, GNSS->uartWorkingBuffer, 10);
	while((GNSS->txDone == 0x00) || (GNSS->rxDone == 0x00)) {};

	printf("Sending GNSS config...\r\n");
	GNSS->txDone = 0x00;
	GNSS->rxDone = 0x00;
	HAL_UART_Transmit_DMA(GNSS->huart, setGNSS,
			sizeof(setGNSS) / sizeof(uint8_t));
	HAL_UART_Receive_DMA(GNSS->huart, GNSS->uartWorkingBuffer, 10);
	while((GNSS->txDone == 0x00) || (GNSS->rxDone == 0x00)) {};
}




/*!
 *  Creates a checksum based on UBX standard.
 * @param class Class value from UBX doc.
 * @param messageID MessageID value from UBX doc.
 * @param dataLength Data length value from UBX doc.
 * @param payload Just payload.
 * @return  Returns checksum.
 */
uint8_t GNSS_Checksum(uint8_t class, uint8_t messageID, uint8_t dataLength,uint8_t *payload) {
//todo: Look at 32.4 UBX Checksum
	return 0;
}
