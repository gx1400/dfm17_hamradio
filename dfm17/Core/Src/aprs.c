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
#include "gpio.h"
#include "analog.h"
#include "settings.h"

/*
 * the APRS data buffer
 * contains ASCII data
 */
static const char aprs_pos_template[APRS_POS_LEN + 1] = "/ddhhmmz/xxxxyyyyOaa1|ss00112233|";
char aprs_buf[APRS_BUF_SIZE];
/* number of bytes of aprs_buf that are sent */
uint8_t aprs_len;
// KC0TWY-5>APDR16,TCPIP*,qAC,T2LAUSITZ:=3858.33N/09439.08W>166/000/A=000973 https://aprsdroid.org/
//char aprs_buf[] = "=3858.33N/09439.08W>189/000/A=000972 https://aprsdroid.org/";

extern volatile uint16_t aprs_bit;
extern volatile uint16_t aprs_tick;
extern volatile uint16_t aprs_baud_tick;
extern volatile uint8_t ppsLockStatus;

/*
 * AFSK modulation, all settable over SWD for testing (takes effect at the next
 * packet):
 * 0: square wave on GPIO3, async direct 2FSK at the settings deviation
 * 1: sine AFSK from MODEM_FREQ_OFFSET writes (aprs_afsk_tick), with separate
 *    mark and space deviation. a louder space tone (pre-emphasis) makes up for
 *    the de-emphasis in FM receivers, which leaves 2200 Hz 5.3 dB below 1200 Hz
 */
volatile uint8_t aprs_afsk_sine = APRS_AFSK_SINE;
volatile uint16_t aprs_mark_dev_hz = APRS_MARK_DEV_HZ;
volatile uint16_t aprs_space_dev_hz = APRS_SPACE_DEV_HZ;
/* samples skipped in the last packet because the radio wasn't ready */
volatile uint32_t aprs_afsk_skipped;
static volatile uint8_t afsk_run;
static uint32_t afsk_phase, afsk_inc_mark, afsk_inc_space;
static int32_t afsk_amp_mark, afsk_amp_space;

static const int16_t sine64[64] = {
	0, 3212, 6393, 9512, 12539, 15446, 18204, 20787,
	23170, 25329, 27245, 28898, 30273, 31356, 32137, 32609,
	32767, 32609, 32137, 31356, 30273, 28898, 27245, 25329,
	23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212,
	0, -3212, -6393, -9512, -12539, -15446, -18204, -20787,
	-23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609,
	-32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329,
	-23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212,
};

/* phase increment per AFSK sample for a tone: f * 2^32 / sample rate, with
 * sample rate = 24 MHz / 909 / APRS_AFSK_TICKS_PER_SAMPLE */
static uint32_t afsk_inc(uint32_t f) {
	return (uint32_t)(((uint64_t)f << 32) * 909 * APRS_AFSK_TICKS_PER_SAMPLE / 24000000UL);
}

/*
 * aprs_afsk_tick
 *
 * called by the TIM15 ISR on every tick; while a sine AFSK packet is being
 * sent, writes one sample of the phase-continuous tone every
 * APRS_AFSK_TICKS_PER_SAMPLE ticks
 */
void aprs_afsk_tick(void) {
	static uint8_t div = 0;

	if (!afsk_run || ++div < APRS_AFSK_TICKS_PER_SAMPLE)
		return;
	div = 0;
	uint8_t space = aprs_bit == APRS_SPACE;
	afsk_phase += space ? afsk_inc_space : afsk_inc_mark;
	int32_t s = sine64[afsk_phase >> 26];
	int16_t offset = (int16_t)((s * (space ? afsk_amp_space : afsk_amp_mark)) >> 15);
	if (!si4060_set_offset_fast(offset))
		aprs_afsk_skipped++;
}

/* AX.25 header: APZDFM destination, then the source callsign/SSID and the
 * WIDEn-N path from the settings, filled in by aprs_set_station() */
unsigned char aprs_header[APRS_HEADER_LEN] = {'A' * 2, 'P' * 2, 'Z' * 2, 'D' * 2, 'F' * 2, 'M' * 2,
		SSID_RESC + (DST_SSID << 1), ' ' * 2, ' ' * 2, ' ' * 2, ' ' * 2, ' ' * 2, ' ' * 2,
		SSID_RESC, 'W' * 2, 'I' * 2, 'D' * 2, 'E' * 2, '1' * 2, ' ' * 2,
		SSID_RESC + (1 << 1) + HEADER_END, CONTROL_UI, PID_NONE};

/*
 * aprs_set_station
 *
 * puts the callsign, SSID and path from the settings (cfg) into the AX.25
 * header; settings_store.c calls it whenever the settings change
 */
void aprs_set_station(void) {
	for (int i = 0; i < 6; i++)
		aprs_header[7 + i] = (cfg.callsign[i] ? cfg.callsign[i] : ' ') << 1;
	aprs_header[13] = SSID_RESC + ((cfg.ssid & 0x0F) << 1);
	aprs_header[18] = ('0' + (cfg.path >> 4)) << 1;
	aprs_header[20] = SSID_RESC + ((cfg.path & 0x0F) << 1) + HEADER_END;
}

enum aprs_states {SM_INIT, SFLAG, AX25_HEADER, AX25_DATA, AX25_FCS1, AX25_FCS2, EFLAG};
volatile uint16_t aprs_state = SM_INIT;
volatile uint16_t fcs = 0;
volatile uint8_t bitcnt = 8;
volatile uint8_t onecnt = 0;
volatile uint8_t finished = 0;
volatile uint8_t stuffing = 0;

static uint16_t calc_aprscrc (uint16_t crcStart, const uint8_t *frame, uint8_t frame_len)
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
	crcval2 = calc_aprscrc(crcval1, (const uint8_t *)aprs_buf, aprs_len);

	// Take the one's compliment of the calculated CRC
	crcval2 = crcval2 ^ 0xffff;

	// AX.25 sends the FCS low byte first, which get_next_byte() already does
	fcs = crcval2;

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
 * checks for validity of the fix (GPS PPS and at least a 2D fix), then works as
 * aprs_prepare_fix().
 */
uint8_t aprs_prepare_buffer(GNSS_StateHandle *GNSS, uint8_t backlog_fix) {
	return aprs_prepare_fix(GNSS, ppsLockStatus && GNSS->fixType >= Fix2D, backlog_fix);
}

/*
 * aprs_prepare_fix
 *
 * prepares the buffer with the fix in GNSS if fix_valid; otherwise the fixed
 * APRS_TEST_* position is sent when APRS_TEST_POSITION is defined, or else a
 * status packet with the telemetry as text (see aprs_prepare_status).
 *
 * backlog_fix contains a flag marking the fix as a backlog fix, containing a zero TLM sequence ID
 *
 * always transmits the latest temperature / battery voltage, no historical values!
 *
 * returns: 1 if the packet carries a position, 0 for a status packet
 */
uint8_t aprs_prepare_fix(GNSS_StateHandle *GNSS, uint8_t fix_valid, uint8_t backlog_fix) {
	int16_t temp_aprs = 0;
	uint16_t seq_tmp;
	static uint16_t aprs_seqnum = 0;
	float lat, lon, alt_ft;
	uint8_t day, hour, min;

	if (fix_valid) {
		/* u-blox PVT: lat/lon in 1e-7 degrees, height above MSL in mm */
		lat = (float)GNSS->lat / 10000000.0f;
		lon = (float)GNSS->lon / 10000000.0f;
		alt_ft = (float)GNSS->hMSL / 1000.0f * 3.28084f;
		day = GNSS->day;
		hour = GNSS->hour;
		min = GNSS->min;
	} else {
#ifdef APRS_TEST_POSITION
		lat = APRS_TEST_LAT;
		lon = APRS_TEST_LON;
		alt_ft = APRS_TEST_ALT_M * 3.28084f;
		day = 1;
		hour = 0;
		min = 0;
#else
		aprs_prepare_status(GNSS);
		return 0;
#endif
	}

	__builtin_memcpy(aprs_buf, aprs_pos_template, APRS_POS_LEN);
	aprs_len = APRS_POS_LEN;
	aprs_buf[APRS_SYMTAB_POS] = cfg.symbol_table;
	aprs_buf[APRS_SYMBOL_POS] = cfg.symbol_code;

	/* compressed altitude is log1.002(feet), which cannot go below 1 ft */
	if (alt_ft < 1.0f)
		alt_ft = 1.0f;

	i16toa(day, 2, &aprs_buf[APRS_TIME_START]);
	i16toa(hour, 2, &aprs_buf[APRS_TIME_START + 2]);
	i16toa(min, 2, &aprs_buf[APRS_TIME_START + 4]);

	base91_encode_latlon(&aprs_buf[APRS_LAT_START], 380926.0f * (90.0f - lat));
	base91_encode_latlon(&aprs_buf[APRS_LON_START], 190463.0f * (180.0f + lon));
	base91_encode_tlm(&aprs_buf[APRS_ALT_START], logf(alt_ft)/logf(1.002f));

	if (backlog_fix) {
		seq_tmp = 0;
	} else {
		aprs_seqnum = (aprs_seqnum + 1) % 8281;
		seq_tmp = aprs_seqnum;
	}

	/* telemetry channels: MCU temperature (C + APRS_TLM_TEMP_OFFSET), supply
	 * voltage in mV, supply current in mA and USB voltage in mV (0 on
	 * battery), each 0..8280 in base91 */
	analog_reading analog;
	analog_read(&analog);
	temp_aprs = analog.temp_c + APRS_TLM_TEMP_OFFSET;

	base91_encode_tlm(&aprs_buf[APRS_SEQ_START], seq_tmp);
	base91_encode_tlm(&aprs_buf[APRS_TEMP_START], (uint16_t)temp_aprs);
	base91_encode_tlm(&aprs_buf[APRS_VOLT_START], analog.vin_mv);
	base91_encode_tlm(&aprs_buf[APRS_VSOL_START], analog.iin_ma);
	base91_encode_tlm(&aprs_buf[APRS_USB_START], analog.on_usb ? analog.usb_mv : 0);

	calculate_fcs();
	return 1;
}

/*
 * aprs_prepare_status
 *
 * prepares a status packet without a position, for when there is no GPS fix:
 * ">nofix 2D 4sv 4674mV 55mA 30C USB" (GPS fix type and satellites from the
 * last NAV-PVT, or "nopps" without GPS PPS; supply voltage, supply current, MCU
 * temperature, USB or BAT power). the GPS state comes first, a TH-D72 shows only 20 characters.
 * the station is identified by the source callsign in the header.
 */
void aprs_prepare_status(const GNSS_StateHandle *GNSS) {
	static const char text[] = ">nofix ";
	analog_reading analog;
	uint8_t n = sizeof(text) - 1;

	analog_read(&analog);
	__builtin_memcpy(aprs_buf, text, n);
	if (!ppsLockStatus) {
		__builtin_memcpy(&aprs_buf[n], "nopps ", 6);
		n += 6;
	} else {
		n += i16toav(GNSS->fixType, &aprs_buf[n]);
		aprs_buf[n++] = 'D';
		aprs_buf[n++] = ' ';
		n += i16toav(GNSS->numSV, &aprs_buf[n]);
		aprs_buf[n++] = 's';
		aprs_buf[n++] = 'v';
		aprs_buf[n++] = ' ';
	}
	n += i16toav(analog.vin_mv, &aprs_buf[n]);
	aprs_buf[n++] = 'm';
	aprs_buf[n++] = 'V';
	aprs_buf[n++] = ' ';
	n += i16toav(analog.iin_ma, &aprs_buf[n]);
	aprs_buf[n++] = 'm';
	aprs_buf[n++] = 'A';
	aprs_buf[n++] = ' ';
	if (analog.temp_c < 0) {
		aprs_buf[n++] = '-';
		n += i16toav(-analog.temp_c, &aprs_buf[n]);
	} else {
		n += i16toav(analog.temp_c, &aprs_buf[n]);
	}
	aprs_buf[n++] = 'C';
	__builtin_memcpy(&aprs_buf[n], analog.on_usb ? " USB" : " BAT", 4);
	n += 4;
	aprs_len = n;

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
			/* fall through */
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
			if (++i >= aprs_len) {
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
		default: /* GCOVR_EXCL_START: every state is handled above */
			retval = AX25_FLAG;
			/* GCOVR_EXCL_STOP */
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

/* whether the packet being sent uses sine AFSK, fixed at aprs_tx_start() */
static uint8_t tx_sine;

/*
 * aprs_tx_start
 *
 * keys up the transmitter for the packet prepared in aprs_buf; then call
 * aprs_tx_poll() until it returns 0, and aprs_tx_stop(). tx_aprs() does all
 * three; the parts are separate so a host simulation can run the TIM15 tick
 * between polls (tools/aprs_sim).
 */
void aprs_tx_start(void) {
	tx_sine = aprs_afsk_sine;

	if (tx_sine) {
		afsk_amp_mark = si4060_offset_units(aprs_mark_dev_hz);
		afsk_amp_space = si4060_offset_units(aprs_space_dev_hz);
		afsk_inc_mark = afsk_inc(1200);
		afsk_inc_space = afsk_inc(2200);
		afsk_phase = 0;
		aprs_afsk_skipped = 0;
		/* the carrier only moves by MODEM_FREQ_OFFSET */
		si4060_set_deviation(0);
	}
	aprs_init();
	ledOnGreen();
	deassertSiGPIO3();
	startAprsTickTimer();

	/* radio was set up once in initRadio(): async direct 2FSK, GPIO3 drives the
	 * deviation. not re-running si4060_setup() here avoids reconfiguring GPIO2,
	 * whose DIV_CLK output clocks the STM32 */
	si4060_start_tx(0);
	/* add some TX delay */
	HAL_Delay(250);

	if (tx_sine) {
		/* from here on only the TIM15 ISR uses the SPI */
		si4060_spi_fast(1);
		afsk_run = 1;
	}
	aprs_tick = 0;
}

/*
 * aprs_tx_poll
 *
 * handles the TIM15 tick flags: toggles GPIO3 for the square wave and fetches
 * the next bit at each baud tick
 *
 * returns: 0 once the packet has been sent
 */
uint8_t aprs_tx_poll(void) {
	if (aprs_tick) {
		/* running with APRS sample clock */
		aprs_tick = 0;
		if (!tx_sine)
			toggleSiGPIO3();
		if (aprs_baud_tick) {
			/* running with bit clock (1200 / sec) */
			aprs_baud_tick = 0;
			if (get_next_bit()) {
				aprs_bit = APRS_SPACE;
			} else {
				aprs_bit = APRS_MARK;
			}
		}
	}
	return !finished;
}

/*
 * aprs_tx_stop
 *
 * unkeys the transmitter after the packet and restores the radio settings
 */
void aprs_tx_stop(void) {
	deassertSiGPIO3();
	if (tx_sine) {
		afsk_run = 0;
		si4060_spi_fast(0);
		si4060_set_offset(0);
	}

	HAL_Delay(100);
	si4060_stop_tx();
	stopAprsTickTimer();
	if (tx_sine)
		si4060_set_deviation(cfg.dev_hz);
	ledOffGreen();
}

/*
 * tx_aprs
 *
 * transmits the APRS packet prepared in aprs_buf
 */
/* GCOVR_EXCL_START: the loop is paced by the TIM15 ISR; the three parts are
 * tested on their own (test_aprs.c) and through tools/aprs_sim */
void tx_aprs(void) {
	aprs_tx_start();
	while (aprs_tx_poll())
		;
	aprs_tx_stop();
}
/* GCOVR_EXCL_STOP */
