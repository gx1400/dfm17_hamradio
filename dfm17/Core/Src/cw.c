/**
  ******************************************************************************
  * @file    cw.c
  * @brief   Morse code as a keyed audio tone on the FM carrier (MCW)
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
  * The carrier stays up and the tone on Si4063 GPIO3 is keyed on and off, so
  * any FM receiver hears the Morse. The tone comes from the TIM15 tick ISR
  * (tone_half_ticks): the caller keys up with the tick timer running, as the
  * fox does, and keys down afterwards.
  ******************************************************************************
  */

#include "cw.h"
#include "main.h"
#include "gpio.h"
#include "tim.h"

/*
 * cw_morse
 *
 * returns the dot/dash pattern for a character (either case), or NULL if
 * there is none
 */
const char *cw_morse(char c) {
	static const char *const letters[26] = {".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....",
			"..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
			"..-", "...-", ".--", "-..-", "-.--", "--.."};
	static const char *const digits[10] = {"-----", ".----", "..---", "...--", "....-", ".....",
			"-....", "--...", "---..", "----."};
	static const char punct[] = "/?.,=-";
	static const char *const punct_code[]
			= {"-..-.", "..--..", ".-.-.-", "--..--", "-...-", "-....-"};

	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	if (c >= 'A' && c <= 'Z')
		return letters[c - 'A'];
	if (c >= '0' && c <= '9')
		return digits[c - '0'];
	for (uint8_t i = 0; punct[i]; i++)
		if (c == punct[i])
			return punct_code[i];
	return NULL;
}

/*
 * cw_tone
 *
 * keys the Morse tone; off parks the carrier at one end of the deviation,
 * no audio
 */
void cw_tone(uint8_t on) {
	tone_half_ticks = on ? CW_TONE_HALF_TICKS : 0;
	if (!on)
		deassertSiGPIO3();
}

/*
 * cw_send
 *
 * keys text in Morse at wpm (PARIS timing: dit = 1200 / WPM ms). A space is
 * a word gap; characters without a Morse code are skipped. The transmitter
 * must already be on.
 */
void cw_send(const char *text, uint8_t wpm) {
	uint32_t dit = 1200 / (wpm ? wpm : 1);

	for (; *text; text++) {
		if (*text == ' ') {
			/* word gap is 7 dits, 3 were already sent after the last letter */
			HAL_Delay(4 * dit);
			continue;
		}
		const char *code = cw_morse(*text);
		if (code == NULL)
			continue;
		for (; *code; code++) {
			cw_tone(1);
			HAL_Delay(*code == '-' ? 3 * dit : dit);
			cw_tone(0);
			HAL_Delay(dit);
		}
		/* letter gap is 3 dits, 1 was already sent after the last element */
		HAL_Delay(2 * dit);
	}
}
