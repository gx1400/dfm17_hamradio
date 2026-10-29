/**
  ******************************************************************************
  * @file    diag.c
  * @brief   Button and LED diagnostics
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
  * diag_tick() runs every 1 ms from SysTick, so the LEDs keep their patterns and
  * the button is sampled even while the main loop is blocked transmitting.
  *
  * Green:  on while transmitting; otherwise a short heartbeat flash every 2 s
  *         (a double flash while the fox is paused)
  * Yellow: on with a 3D GPS fix; a short flash on each GPS PPS pulse without one
  * Red:    blink code of the lowest active diag_error, repeating
  * Button: held 1 s lights green, held 3 s lights all three, showing which
  *         action a release will pick (short / medium / long press)
  ******************************************************************************
  */

#include "diag.h"
#include "main.h"
#include "gps.h"
#include "led.h"

#define DEBOUNCE_MS			20
#define HEARTBEAT_MS		2000
#define FLASH_MS			50
#define PPS_FLASH_MS		100
#define BLINK_ON_MS			200
#define BLINK_SLOT_MS		500
#define BLINK_GAP_MS		1500
#define ACK_MS				200

static volatile uint8_t ready = 0;
static volatile uint8_t errors = 0;			/* bit n set = diag_error n active */
static volatile uint8_t tx = 0;
static volatile uint8_t gps_fix = 0;
static volatile uint8_t paused = 0;
static volatile uint32_t last_pps = 0;
static volatile uint32_t ack_until = 0;
static volatile diag_button button_event = DIAG_BUTTON_NONE;

/* debounced button state, only touched from diag_tick() */
static uint8_t button_down = 0;
static uint8_t button_armed = 0;	/* released once since boot */
static uint8_t button_raw_count = 0;
static uint32_t button_down_tick = 0;

static void set_green(uint8_t on) { if (on) ledOnGreen(); else ledOffGreen(); }
static void set_yellow(uint8_t on) { if (on) ledOnYellow(); else ledOffYellow(); }
static void set_red(uint8_t on) { if (on) ledOnRed(); else ledOffRed(); }

/*
 * diag_selftest
 *
 * all LEDs on for half a second at boot, then hands the LEDs to diag_tick()
 */
void diag_selftest(void) {
	ledOnGreen();
	ledOnYellow();
	ledOnRed();
	HAL_Delay(500);
	ledOffGreen();
	ledOffYellow();
	ledOffRed();
	ready = 1;
}

/*
 * diag_leds_off
 *
 * stops diag_tick() driving the LEDs and turns them off (low-power measurements);
 * diag_selftest() hands them back
 */
void diag_leds_off(void) {
	ready = 0;
	ledOffGreen();
	ledOffYellow();
	ledOffRed();
}

static void button_sample(uint32_t now) {
	/* PC8 reads high while pressed */
	uint8_t raw = HAL_GPIO_ReadPin(iButton_GPIO_Port, iButton_Pin) == GPIO_PIN_SET;

	/* released since boot (or never held): presses count from now on */
	if (!raw && !button_down)
		button_armed = 1;

	if (raw == button_down) {
		button_raw_count = 0;
		return;
	}
	if (++button_raw_count < DEBOUNCE_MS)
		return;
	button_raw_count = 0;
	button_down = raw;

	if (button_down) {
		button_down_tick = now;
		return;
	}

	/* released. the first release after boot only arms the button: it was held
	 * to power the board on */
	if (!button_armed) {
		button_armed = 1;
		return;
	}
	uint32_t held = now - button_down_tick;
	if (held < DIAG_BUTTON_MEDIUM_MS)
		button_event = DIAG_BUTTON_SHORT;
	else if (held < DIAG_BUTTON_LONG_MS)
		button_event = DIAG_BUTTON_MEDIUM;
	else
		button_event = DIAG_BUTTON_LONG;
	ack_until = now + ACK_MS;
}

static uint8_t red_pattern(uint32_t now) {
	uint8_t code;

	for (code = 1; code < 8; code++)
		if (errors & (1 << code))
			break;
	if (code == 8)
		return 0;

	uint32_t phase = now % (code * BLINK_SLOT_MS + BLINK_GAP_MS);
	return phase < code * BLINK_SLOT_MS && (phase % BLINK_SLOT_MS) < BLINK_ON_MS;
}

/*
 * diag_tick
 *
 * call every 1 ms (SysTick)
 */
void diag_tick(void) {
	if (!ready)
		return;

	uint32_t now = HAL_GetTick();
	button_sample(now);

	if (button_down && button_armed) {
		uint32_t held = now - button_down_tick;
		set_green(held >= DIAG_BUTTON_MEDIUM_MS);
		set_yellow(held >= DIAG_BUTTON_LONG_MS);
		set_red(held >= DIAG_BUTTON_LONG_MS);
		return;
	}

	uint32_t beat = now % HEARTBEAT_MS;
	uint8_t heartbeat = beat < FLASH_MS
			|| (paused && beat >= 4 * FLASH_MS && beat < 5 * FLASH_MS);
	set_green(tx || (int32_t)(ack_until - now) > 0 || heartbeat);
	set_yellow(gps_fix || (ppsLockStatus && now - last_pps < PPS_FLASH_MS));
	set_red(red_pattern(now));
}

/* call from the GPS PPS interrupt */
void diag_pps(void) {
	last_pps = HAL_GetTick();
}

void diag_set_error(diag_error err) {
	errors |= 1 << err;
}

void diag_clear_error(diag_error err) {
	errors &= ~(1 << err);
}

void diag_set_tx(uint8_t on) {
	tx = on;
}

void diag_set_gps_fix(uint8_t fix3d) {
	gps_fix = fix3d;
}

void diag_set_paused(uint8_t on) {
	paused = on;
}

/*
 * diag_get_button
 *
 * returns the last completed button press and clears it
 */
diag_button diag_get_button(void) {
	__disable_irq();
	diag_button event = button_event;
	button_event = DIAG_BUTTON_NONE;
	__enable_irq();
	return event;
}
