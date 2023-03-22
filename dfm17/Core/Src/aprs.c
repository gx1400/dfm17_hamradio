/**
  ******************************************************************************
  * @file    aprs.c
  * @brief   This file contains all functions for integrating APRS
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 Stefan Biereigel
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
  * Based on code from https://github.com/bristol-seds/pico-tracker
  ******************************************************************************
  * Example message from: https://www.picoballoons.net/aprs-protocol/aprs-message-in-binary
  * 0x00000000: 88 A6 A8 40 40 40 64 A6 | 10001000 10100110 10101000 01000000 01000000 01000000 01100100 10100110 | ...@@@d.
  * 0x00000008: A4 86 40 40 40 66 AE 92 | 10100100 10000110 01000000 01000000 01000000 01100110 10101110 10010010 | ..@@@f..
  * 0x00000010: 88 8A 62 40 63 03 F0 4A | 10001000 10001010 01100010 01000000 01100011 00000011 11110000 01001010 | ..b@c..J
  * 0x00000018: 75 73 74 20 53 6F 6D 65 | 01110101 01110011 01110100 00100000 01010011 01101111 01101101 01100101 | ust Some
  * 0x00000020: 20 44 61 74 61 A5 6E    | 00100000 01000100 01100001 01110100 01100001 10100101 01101110          |  Data.n
  *
  *   uint8_t aprspacket[] = {0x88,0xA6,0xA8,0x40,0x40,0x40,0x64,0xA6,  \
  *                           0xA4,0x86,0x40,0x40,0x40,0x66,0xAE,0x92,  \
  *                           0x88,0x8A,0x62,0x40,0x63,0x03,0xF0,0x4A,  \
  *                           0x75,0x73,0x74,0x20,0x53,0x6F,0x6D,0x65,  \
  *                           0x20,0x44,0x61,0x74,0x61};
  *  Expected CRC: 0x A56E
  ******************************************************************************
  */

#include "aprs.h"
#include <inttypes.h>
#include "GNSS.h"
#include "gps.h"
#include "si4063.h"
#include "tim.h"
#include "string.h"
#include <math.h>
#include "led.h"

/*
 * the APRS data buffer
 * contains ASCII data
 */
char aprs_buf[APRS_BUF_LEN] = "/ddhhmmz/xxxxyyyyOaa1|ss001122|";
// KC0TWY-5>APDR16,TCPIP*,qAC,T2LAUSITZ:=3858.33N/09439.08W>166/000/A=000973 https://aprsdroid.org/
//char aprs_buf[] = "=3858.33N/09439.08W>189/000/A=000972 https://aprsdroid.org/";

extern volatile uint16_t aprs_bit;
extern volatile uint16_t aprs_tick;
extern volatile uint16_t aprs_baud_tick;
extern volatile uint8_t ppsLockStatus;

const unsigned char aprs_header[APRS_HEADER_LEN] = {
	'A'*2, 'P'*2, 'R'*2, 'S'*2, ' '*2, ' '*2, SSID_RESC + (DST_SSID << 1),
	'K'*2, 'E'*2, '0'*2, 'P'*2, 'R'*2, 'Y'*2, SSID_RESC + (SRC_SSID << 1),
	'W'*2, 'I'*2, 'D'*2, 'E'*2, '1'*2, ' '*2, SSID_RESC + (WIDE_SSID << 1) + HEADER_END,
	CONTROL_UI, PID_NONE};

enum aprs_states {SM_INIT, SFLAG, AX25_HEADER, AX25_DATA, AX25_FCS1, AX25_FCS2, EFLAG};
volatile uint16_t aprs_state = SM_INIT;
volatile uint16_t fcs = 0;
volatile uint8_t bitcnt = 8;
volatile uint8_t onecnt = 0;
volatile uint8_t finished = 0;
volatile uint8_t stuffing = 0;

uint16_t rev16(uint16_t x) {
  return (x << 8) | (x >>8);
}

static uint16_t calc_aprscrc (uint8_t crcStart, uint8_t *frame, uint8_t frame_len)
{

    uint8_t i, j;
    // Preload the CRC register with ones
    //uint16_t crc = 0xffff;
    uint16_t crc = crcStart;


    // Iterate over every octet in the frame
    for (i = 0; i < frame_len; i++)
    {
        // Iterate over every bit, LSb first
        for (j = 0; j < 8; j++)
        {
            uint8_t bit = (frame[i] >> j) & 0x01;

            // Divide by a bit - reversed 0x1021
            if ((crc & 0x0001) != bit)
            {
                crc = (crc >> 1) ^ 0x8408;
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }


    return crc;
}

void calculate_fcs(void) {
	// calculate crc of header with initial value of 0xFFFF
	uint16_t crcval1 = 0;
	crcval1 = calc_aprscrc(0xFFFF, aprs_header, APRS_HEADER_LEN);

	// pass header calculation back into crc calc to process buffer
	uint16_t crcval2 = 0;
	crcval2 = calc_aprscrc(crcval1, aprs_buf, APRS_BUF_LEN);

	// Take the one's compliment of the calculated CRC
	crcval2 = crcval2 ^ 0xffff;

	// bytes are swapped, so reverse
	fcs = rev16(crcval2);

}

/*
 * base91_encode
 *
 * encodes one short value for the telemetry extension to base91
 * does not work for positions etc. this way yet!
 *
 */
void base91_encode_tlm(char *buf, uint16_t value) {
    value = value % 8281;

    buf[0] = 33 + (value / 91);
    buf[1] = 33 + (value % 91);
}

void base91_encode_latlon(char *buf, uint32_t value) {
    buf[0] = 33 + (value / ((uint32_t)91*91*91));
    value = value % ((uint32_t)91*91*91);
    buf[1] = 33 + (value / (91*91));
    value = value % (91*91);
    buf[2] = 33 + (value / 91);
    buf[3] = 33 + (value % 91);
}

/*
 * aprs_prepare_buffer
 *
 * prepares the buffer for APRS transmission with the fix given as a reference.
 * checks for validity of the fix, does not change the buffer if the fix is unsuitable for transmission.
 *
 * backlog_fix contains a flag marking the fix as a backlog fix, containing a zero TLM sequence ID
 *
 * always transmits the latest temperature / battery voltage, no historical values!
 *
 */
void aprs_prepare_buffer(GNSS_StateHandle *GNSS, uint8_t backlog_fix) {
	int16_t temp_aprs = 0;
	uint16_t seq_tmp;
	static uint16_t aprs_seqnum = 0;

	if (!ppsLockStatus)
		return;

	i16toa(22, 2, &aprs_buf[APRS_TIME_START]);
	i16toa(18, 2, &aprs_buf[APRS_TIME_START + 2]);
	i16toa(13, 2, &aprs_buf[APRS_TIME_START + 4]);
	//i16toa(fix->day, 2, &aprs_buf[APRS_TIME_START]); 			// todo link to gps info
	//i16toa(fix->hour, 2, &aprs_buf[APRS_TIME_START + 2]);		// todo link to gps info
	//i16toa(fix->min, 2, &aprs_buf[APRS_TIME_START + 4]);		// todo link to gps info

	//base91_encode_latlon(&aprs_buf[APRS_LAT_START], 380926.0f * (90.0f - (float)fix->lat/10000000.0f)); 	// todo link to gps info
	//base91_encode_latlon(&aprs_buf[APRS_LON_START], 190463.0f * (180.0f + (float)fix->lon/10000000.0f));	// todo link to gps info
	//base91_encode_tlm(&aprs_buf[APRS_ALT_START], logf((float)fix->alt * 3.28f)/logf(1.002f));				// todo link to gps info

	base91_encode_latlon(&aprs_buf[APRS_LAT_START], 380926.0f * (90.0f - (float)38.9987994f/10000000.0f));
	base91_encode_latlon(&aprs_buf[APRS_LON_START], 190463.0f * (180.0f + (float)-94.6819493557f/10000000.0f));
	base91_encode_tlm(&aprs_buf[APRS_ALT_START], logf((float)100.0f * 3.28f)/logf(1.002f));

	if (backlog_fix) {
		seq_tmp = 0;
	} else {
		aprs_seqnum = (aprs_seqnum + 1) % 8281;
		seq_tmp = aprs_seqnum;
	}

	//temp_aprs = fix->temperature_int + APRS_TLM_TEMP_OFFSET;

	//base91_encode_tlm(&aprs_buf[APRS_SEQ_START], seq_tmp);
	//	base91_encode_tlm(&aprs_buf[APRS_TEMP_START], (uint16_t)temp_aprs);
	//base91_encode_tlm(&aprs_buf[APRS_VOLT_START], fix->voltage_bat);
	//base91_encode_tlm(&aprs_buf[APRS_VSOL_START], fix->voltage_sol);

	temp_aprs = 32 + APRS_TLM_TEMP_OFFSET;

	base91_encode_tlm(&aprs_buf[APRS_SEQ_START], seq_tmp);
	base91_encode_tlm(&aprs_buf[APRS_TEMP_START], (uint16_t)temp_aprs);
	base91_encode_tlm(&aprs_buf[APRS_VOLT_START], 3000);
	base91_encode_tlm(&aprs_buf[APRS_VSOL_START], 3100);

	calculate_fcs();
}

/*
 * aprs_init
 *
 * prepares the state machine for transmission of the next APRS frame
 */
void aprs_init(void) {
	aprs_state = SM_INIT;
	finished = 0;
	bitcnt = 8;
	onecnt = 0;
	stuffing = 0;
}

/*
 * get_next_byte
 *
 * fetches the next byte to be transmitted, until there is none left. it advances the
 * internal AX.25 state machine. first, a configurable number of flags are returned,
 * which are followed by the header, the payload, and end flags. an infinite number of
 * end flags are returned, until the state machine is reset by init_aprs()
 *
 * returns:
 * 	byte to be transmitted
 */
uint8_t get_next_byte(void) {
	static int i = 0;
	uint8_t retval;
	switch (aprs_state) {
		case SM_INIT:
			stuffing = 0;
			aprs_state = SFLAG;
			i = 0;
		case SFLAG:
			retval = AX25_FLAG;
			if (++i >= AX25_SFLAGS) {
				aprs_state = AX25_HEADER;
				i = 0;
			}
			break;
		case AX25_HEADER:
			stuffing = 1;
			retval = aprs_header[i];
			if (++i >= APRS_HEADER_LEN) {
				aprs_state = AX25_DATA;
				i = 0;
			}
			break;
		case AX25_DATA:
			retval = aprs_buf[i];
			if (++i >= APRS_BUF_LEN) {
				aprs_state = AX25_FCS1;
				i = 0;
			}
			break;
		case AX25_FCS1:
			retval = (uint8_t)fcs;
			aprs_state = AX25_FCS2;
			break;
		case AX25_FCS2:
			retval = (uint8_t)(fcs >> 8);
			aprs_state = EFLAG;
			break;
		case EFLAG:
			stuffing = 0;
			retval = AX25_FLAG;
			if (++i > AX25_EFLAGS) {
				finished = 1;
				i = 0;
			}
			break;
		default:
			retval = AX25_FLAG;
	}
	return retval;
}

/*
 * get_next_bit
 *
 * fetches the next bit for the data stream.
 *
 * returns: the next bit to be transmitted, already NRZI coded and bit stuffed
 */
uint8_t get_next_bit(void) {
	static uint8_t byte = 0;
	static uint8_t bit = 1;
	static uint8_t bit_d = 0;
	if (bitcnt >= 8) {
		byte = get_next_byte();
		bitcnt = 0;
	}
	/* all bytes LSB first */
	bit = byte & 0x01;
	if (bit) {
		/* bit stuffing */
		onecnt++;
		if ((stuffing == 1) && (onecnt >= 5)) {
			/* next time the same bit will be a zero -> stuffed bit */
			byte &= ~0x01;
			onecnt = 0;
		} else {
			byte >>= 1;
			bitcnt = bitcnt + 1;
		}

	} else {
		/* grab next bit only if not stuffed */
		onecnt = 0;
		byte >>= 1;
		bitcnt = bitcnt + 1;
	}

	/* NRZI encoding */
	if (bit == 0) {
		bit_d ^= 0x01;
	}
	return bit_d;
}

/*
 * tx_aprs
 *
 * transmits an APRS packet.
 *
 */
void tx_aprs(void) {
	aprs_init();
	ledOnGreen();
	deassertSiGPIO3();
	startAprsTickTimer();

	/* use 2FSK mode so we can adjust the OFFSET register */
	si4060_setup(MOD_TYPE_2GFSK);
	si4060_freq_aprs_dfm17();
	si4060_start_tx(0);
	/* add some TX delay */
	HAL_Delay(250);

	aprs_tick = 0;
	do {
		if (aprs_tick) {
			/* running with APRS sample clock */
			aprs_tick = 0;
			toggleSiGPIO3();
			if (aprs_baud_tick) {
				/* running with bit clock (1200 / sec) */
				//WDTCTL = WDTPW + WDTCNTCL + WDTIS1;
				aprs_baud_tick = 0;
				//toggleSiGPIO3();

				if (get_next_bit()) {
					aprs_bit = APRS_SPACE;
				} else {
					aprs_bit = APRS_MARK;
				}
			}

			/* tell us when we fail to meet the timing */
//			if (aprs_tick) {
//				while(1) {
//					WDTCTL = WDTPW + WDTCNTCL + WDTIS1;
//				}
//			}
		}
	} while(!finished);

	deassertSiGPIO3();

	HAL_Delay(100);
	si4060_stop_tx();
	stopAprsTickTimer();
	ledOffGreen();
}





