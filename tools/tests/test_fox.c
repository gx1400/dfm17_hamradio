/*
 * test_fox.c - fox transmissions: Morse ID timing, hunt tone, APRS cadence,
 * button test transmissions (fox.c)
 */
#include "test.h"
#include "fake_hal.h"
#include "fox.h"
#include "aprs.h"
#include "tim.h"
#include "fake_settings.h"

/* ---- fakes ------------------------------------------------------------------ */

volatile uint16_t tone_half_ticks;
static int tx_on, aprs_sent, cw_mode, carrier_ms;

void diag_set_tx(uint8_t on) { tx_on = on; }
void si4060_start_tx(uint8_t channel) { (void)channel; }
void si4060_stop_tx(void) {}
void si4060_set_cw(uint8_t on) { cw_mode = on; }
void startAprsTickTimer(void) {}
void stopAprsTickTimer(void) {}
void deassertSiGPIO3(void) {}
uint8_t aprs_prepare_buffer(GNSS_StateHandle *g, uint8_t b) { (void)g; (void)b; return 1; }
void calculate_fcs(void) {}
void tx_aprs(void) { aprs_sent++; }

/* the tone on/off timeline seen at each HAL_Delay(): runs of (tone, ms) */
typedef struct { int tone; uint32_t ms; } run_t;
static run_t runs[512];
static int nruns;

static void record(uint32_t ms) {
	int tone = tone_half_ticks != 0;
	if (cw_mode && tx_on)
		carrier_ms += ms;
	if (nruns && runs[nruns - 1].tone == tone)
		runs[nruns - 1].ms += ms;
	else if (nruns < 512)
		runs[nruns++] = (run_t){tone, ms};
}

static void reset(void) {
	nruns = 0;
	aprs_sent = 0;
	carrier_ms = 0;
	tone_half_ticks = 0;
	fake_tick = 0;
	fake_delay_hook = record;
}

/* turns the Morse part of the timeline back into text: dit = 1200 / WPM ms */
static void decode_morse(char *out, size_t max) {
	static const char *const table[] = {
		"A.-", "B-...", "C-.-.", "D-..", "E.", "F..-.", "G--.", "H....", "I..",
		"J.---", "K-.-", "L.-..", "M--", "N-.", "O---", "P.--.", "Q--.-", "R.-.",
		"S...", "T-", "U..-", "V...-", "W.--", "X-..-", "Y-.--", "Z--..",
		"0-----", "1.----", "2..---", "3...--", "4....-", "5.....", "6-....",
		"7--...", "8---..", "9----.",
	};
	const uint32_t dit = 1200 / cfg.cw_wpm;
	char code[8];
	size_t nc = 0, n = 0;

	/* runs[0] is the 500 ms squelch-opening carrier; stop at the hunt tone */
	for (int i = 1; i < nruns && n + 1 < max; i++) {
		if (runs[i].tone) {
			if (runs[i].ms > 5 * dit)
				break;				/* the hunt tone */
			if (nc < sizeof(code) - 1)
				code[nc++] = runs[i].ms >= 2 * dit ? '-' : '.';
		} else if (runs[i].ms >= 2 * dit || i == nruns - 1) {
			code[nc] = 0;		/* end of a letter */
			for (size_t k = 0; k < sizeof(table) / sizeof(table[0]); k++)
				if (strcmp(table[k] + 1, code) == 0)
					out[n++] = table[k][0];
			nc = 0;
		}
	}
	out[n] = 0;
}

/* ---- tests -------------------------------------------------------------------- */

static void morse_id_decodes(void) {
	char text[16];
	GNSS_StateHandle g = {0};
	reset();
	fox_transmit(&g);
	decode_morse(text, sizeof(text));
	CHECK_STR(text, "NZ0P");
}

static void morse_timing(void) {
	GNSS_StateHandle g = {0};
	const uint32_t dit = 1200 / cfg.cw_wpm; /* 80 ms at 15 WPM */
	reset();
	fox_transmit(&g);
	/* every element is 1 or 3 dits, every gap 1 dit (within a letter), 3
	 * (between letters) or more (before the hunt tone) */
	for (int i = 1; i < nruns; i++) {
		if (runs[i].tone && runs[i].ms < 5 * dit)
			CHECK(runs[i].ms == dit || runs[i].ms == 3 * dit);
		else if (!runs[i].tone && i + 1 < nruns && runs[i + 1].tone
				&& runs[i + 1].ms < 5 * dit)
			CHECK(runs[i].ms == dit || runs[i].ms == 3 * dit);
	}
}

static void hunt_tone_length(void) {
	GNSS_StateHandle g = {0};
	uint32_t longest = 0;
	reset();
	fox_transmit(&g);
	for (int i = 0; i < nruns; i++)
		if (runs[i].tone && runs[i].ms > longest)
			longest = runs[i].ms;
	CHECK_EQ(longest, cfg.fox_tone_ms);
	CHECK_EQ(tone_half_ticks, 0);	/* tone off afterwards */
	CHECK_EQ(tx_on, 0);
}

static void cycle_fits_period(void) {
	GNSS_StateHandle g = {0};
	reset();
	fox_transmit(&g);
	CHECK(fake_tick < cfg.fox_period_s * 1000UL);
}

static void aprs_every_nth_cycle(void) {
	/* the first cycle after boot (this test runs first among those that count),
	 * then every fox_aprs_every-th */
	GNSS_StateHandle g = {0};
	int total = 0;
	for (int i = 0; i < 2 * cfg.fox_aprs_every; i++) {
		reset();
		fox_transmit(&g);
		total += aprs_sent;
	}
	CHECK_EQ(total, 2);
}

static void test_carrier(void) {
	reset();
	tx_on = 0;
	fox_test_carrier();
	CHECK_EQ(carrier_ms, FOX_TEST_MS);
	CHECK_EQ(cw_mode, 0);		/* back to FSK afterwards */
	CHECK_EQ(tx_on, 0);
}

static void test_tone_then_aprs(void) {
	GNSS_StateHandle g = {0};
	reset();
	fox_test_tone_aprs(&g);
	CHECK_EQ(aprs_sent, 1);
	int seen = 0;
	for (int i = 0; i < nruns; i++)
		if (runs[i].tone && runs[i].ms == FOX_TEST_MS)
			seen = 1;
	CHECK(seen);
}

/* #51: send-now is just the packet, no test tone or delays */
static void send_now_no_tone(void) {
	GNSS_StateHandle g = {0};
	reset();
	int before = aprs_sent;
	fox_send_now(&g);
	CHECK_EQ(aprs_sent, before + 1);
	CHECK_EQ(nruns, 0);
	CHECK(!tx_on);
}

/* every test runs on a board set up as NZ0P (fake_settings.h) */
void test_setup(void) {
	provision();
}

TEST_MAIN("fox", TEST(aprs_every_nth_cycle), TEST(morse_id_decodes), TEST(morse_timing),
		TEST(hunt_tone_length), TEST(cycle_fits_period), TEST(test_carrier),
		TEST(test_tone_then_aprs), TEST(send_now_no_tone))
