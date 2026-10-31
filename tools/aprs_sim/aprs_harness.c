/*
 * aprs_harness.c
 *
 * Host-side test harness for dfm17/Core/Src/aprs.c. Builds the real firmware
 * APRS code on a PC with the hardware stubbed out, then:
 *   - prints the AX.25 frame bytes (flags excluded) to stderr
 *   - prints the NRZI-coded, bit-stuffed line bits ('0'/'1') to stdout,
 *     exactly as get_next_bit() hands them to the tone generator
 *
 * aprs_sim.py turns the bit stream into audio for Dire Wolf's atest.
 */

#include <stdio.h>
#include <inttypes.h>
#include "aprs.h"
#include "settings.h"

/* globals that live in main.c / gps.c on the target */
volatile uint16_t aprs_bit;
volatile uint16_t aprs_tick;
volatile uint16_t aprs_baud_tick;
volatile uint8_t ppsLockStatus = 1;

/* hardware stubs */
void startAprsTickTimer(void) {}
void stopAprsTickTimer(void) {}
void HAL_Delay(unsigned int ms) { (void)ms; }
void toggleSiGPIO3(void) {}
void deassertSiGPIO3(void) {}
void ledOnGreen(void) {}
void ledOffGreen(void) {}
void si4060_setup(uint8_t mod_type) { (void)mod_type; }
volatile uint32_t si4060_tx_refused;
void si4060_start_tx(uint8_t channel) { (void)channel; }
void si4060_stop_tx(void) {}
void si4060_set_deviation(uint32_t dev_hz) { (void)dev_hz; }
void si4060_set_offset(uint16_t offset) { (void)offset; }
int32_t si4060_offset_units(int32_t hz) { return hz; }
void si4060_spi_fast(uint8_t on) { (void)on; }
uint8_t si4060_set_offset_fast(int16_t offset) { (void)offset; return 1; }

/* fixed telemetry, values like the survey measured on the bench */
#include "analog.h"
void analog_read(analog_reading *r) {
	r->vdda_mv = 3310;
	r->vin_mv = 4660;
	r->iin_ma = 54;
	r->temp_c = 31;
	r->usb_mv = 5020;
	r->on_usb = 1;
}

/* non-static functions/state from aprs.c that have no prototype in aprs.h */
extern volatile uint8_t finished;
extern volatile uint16_t aprs_state;
void aprs_init(void);
void calculate_fcs(void);
uint8_t get_next_byte(void);
uint8_t get_next_bit(void);

/* the station the firmware would use on a board set up as NZ0P-12 */
static void station(void) {
	static const char call[6] = {'N', 'Z', '0', 'P', 0, 0};
	settings_defaults(&cfg);
	for (int i = 0; i < 6; i++)
		cfg.callsign[i] = call[i];
	settings_seal(&cfg);
	aprs_set_station();
}

int main(int argc, char **argv) {
	GNSS_StateHandle gnss = {0};

	station();
	/* "fix": simulate a 3D GPS fix; otherwise there is no fix and aprs.c
	 * sends a status packet (or its APRS_TEST_* position, if defined) */
	if (argc > 1 && argv[1][0] == 'f') {
		gnss.fixType = Fix3D;
		gnss.lat = 391000000;		/* 39.1 N, u-blox units of 1e-7 deg */
		gnss.lon = -945800000;		/* 94.58 W */
		gnss.hMSL = 25000000;		/* 25 km, in mm */
		gnss.day = 28;
		gnss.hour = 21;
		gnss.min = 45;
	}

	aprs_prepare_buffer(&gnss, 0);
	calculate_fcs();

	/* pass 1: dump the frame bytes between the opening and closing flags */
	aprs_init();
	fprintf(stderr, "frame:");
	while (!finished) {
		uint16_t state_before = aprs_state;
		uint8_t b = get_next_byte();
		/* states 2..5 are AX25_HEADER, AX25_DATA, AX25_FCS1, AX25_FCS2 */
		if (state_before >= 2 && state_before <= 5)
			fprintf(stderr, " %02x", b);
	}
	fprintf(stderr, "\n");

	/* pass 2: the line bit stream */
	aprs_init();
	while (!finished)
		putchar(get_next_bit() ? '1' : '0');
	putchar('\n');

	return 0;
}
