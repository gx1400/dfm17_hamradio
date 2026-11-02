/*
 * afsk_harness.c
 *
 * Runs the firmware's APRS transmitter (aprs_tx_start/poll/stop in
 * dfm17/Core/Src/aprs.c) on a PC: one simulated TIM15 tick, then one pass of
 * the transmit loop, as on the MCU, where the loop reacts to a tick within a
 * microsecond. Prints the carrier frequency offset in Hz at every TIM15 tick
 * (26402.6 Hz): the square wave on GPIO3 at +-2.6 kHz. afsk_sim.py turns it
 * into receiver audio, through the radio's TX filter or not, with or without
 * de-emphasis.
 */

#include <stdio.h>
#include <inttypes.h>
#include "aprs.h"
#include "settings.h"

#define BAUD_TICKS	22

volatile uint16_t aprs_bit;
volatile uint16_t aprs_tick;
volatile uint16_t aprs_baud_tick;
volatile uint8_t ppsLockStatus = 1;
static int gpio3;

/* hardware stubs */
void startAprsTickTimer(void) {}
void stopAprsTickTimer(void) {}
void HAL_Delay(unsigned int ms) { (void)ms; }
void toggleSiGPIO3(void) { gpio3 ^= 1; }
void deassertSiGPIO3(void) { gpio3 = 0; }
void ledOnGreen(void) {}
void ledOffGreen(void) {}
void si4060_setup(uint8_t mod_type) { (void)mod_type; }
volatile uint32_t si4060_tx_refused;
void si4060_start_tx(uint8_t channel) { (void)channel; }
void si4060_stop_tx(void) {}

#include "analog.h"
void analog_read(analog_reading *r) {
	r->vdda_mv = 3310;
	r->vin_mv = 4660;
	r->iin_ma = 54;
	r->temp_c = 31;
	r->usb_mv = 5020;
	r->on_usb = 1;
}

/* the TIM15 ISR: processAprsTick() from tim.c */
static void tick(void) {
	static uint16_t nco = 0, bitc = 0;

	nco++;
	bitc++;
	if ((aprs_bit == 1 && nco >= 6) || (aprs_bit == 0 && nco >= 11)) {
		nco = 0;
		aprs_tick = 1;
	}
	if (bitc == BAUD_TICKS) {
		bitc = 0;
		aprs_baud_tick = 1;
	}
}

/* the station the firmware would use on a board set up as NZ0P-12 */
static void station(void) {
	static const char call[6] = {'N', 'Z', '0', 'P', 0, 0};
	settings_defaults(&cfg);
	for (int i = 0; i < 6; i++)
		cfg.callsign[i] = call[i];
	settings_seal(&cfg);
	aprs_set_station();
}

int main(void) {
	GNSS_StateHandle gnss = {0};

	station();
	aprs_prepare_buffer(&gnss, 0);
	aprs_tx_start();
	do {
		tick();
		printf("%.1f\n", gpio3 ? 2600.0 : -2600.0);
	} while (aprs_tx_poll());
	aprs_tx_stop();
	return 0;
}
