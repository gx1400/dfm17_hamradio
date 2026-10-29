/*
 * afsk_harness.c
 *
 * Runs the firmware's tx_aprs() (dfm17/Core/Src/aprs.c) on a PC with the
 * TIM15 ISR simulated in a second thread, in lockstep with the main loop, and
 * prints the carrier frequency offset in Hz at every TIM15 tick (26402.6 Hz).
 *   afsk_harness sine    sine AFSK from MODEM_FREQ_OFFSET writes
 *   afsk_harness square  square wave on GPIO3 at +-RF_DEV_HZ_DFM
 * afsk_sim.py turns it into receiver audio, with or without de-emphasis.
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <inttypes.h>
#include "aprs.h"

#define XO_FREQ		25600000LL
#define OUTDIV		8
#define BAUD_TICKS	22

volatile uint16_t aprs_bit;
volatile uint16_t aprs_tick;
volatile uint16_t aprs_baud_tick;
volatile uint8_t ppsLockStatus = 1;
extern volatile uint8_t finished;
extern volatile uint8_t aprs_afsk_sine;

static atomic_int running = 0;
static int16_t offset_units;
static int gpio3;

/* hardware stubs */
void startAprsTickTimer(void) { running = 1; }
void stopAprsTickTimer(void) { running = 0; }
void HAL_Delay(unsigned int ms) { (void)ms; }
void toggleSiGPIO3(void) { gpio3 ^= 1; }
void deassertSiGPIO3(void) { gpio3 = 0; }
void ledOnGreen(void) {}
void ledOffGreen(void) {}
void si4060_setup(uint8_t mod_type) { (void)mod_type; }
void si4060_start_tx(uint8_t channel) { (void)channel; }
void si4060_stop_tx(void) {}
void si4060_set_deviation(uint32_t dev_hz) { (void)dev_hz; }
void si4060_set_offset(uint16_t offset) { offset_units = offset; }
int32_t si4060_offset_units(int32_t hz) {
	return (int32_t)(((int64_t)hz * OUTDIV * (1 << 19)) / (2 * XO_FREQ));
}
void si4060_spi_fast(uint8_t on) { (void)on; }
uint8_t si4060_set_offset_fast(int16_t offset) { offset_units = offset; return 1; }

#include "analog.h"
void analog_read(analog_reading *r) {
	r->vdda_mv = 3310;
	r->vin_mv = 4660;
	r->iin_ma = 54;
	r->temp_c = 31;
}

/* the TIM15 ISR: processAprsTick() from tim.c, one tick at a time, waiting
 * for the main loop to take each aprs_tick as the real MCU would in time */
static void *isr(void *arg) {
	uint16_t nco = 0, bitc = 0;
	(void)arg;
	while (!running)
		;
	while (running && !finished) {
		while (aprs_tick && running)
			;
		aprs_afsk_tick();
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
		double hz = aprs_afsk_sine
			? offset_units * 2.0 * XO_FREQ / OUTDIV / (1 << 19)
			: (gpio3 ? 2600.0 : -2600.0);
		printf("%.1f\n", hz);
	}
	return NULL;
}

int main(int argc, char **argv) {
	GNSS_StateHandle gnss = {0};
	pthread_t t;

	aprs_afsk_sine = argc > 1 && strcmp(argv[1], "sine") == 0;
	aprs_prepare_buffer(&gnss, 0);
	pthread_create(&t, NULL, isr, NULL);
	tx_aprs();
	running = 0;
	pthread_join(t, NULL);
	return 0;
}
