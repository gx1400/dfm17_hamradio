/*
 * test_ppsdisc.c - TCXO correction from the GPS PPS (ppsdisc.c)
 */
#include "test.h"
#include "fake_hal.h"
#include "ppsdisc.h"
#include "si4063.h"

/* ---- fakes ------------------------------------------------------------------ */

static uint32_t tuned_hz;
static int retunes;

void si4060_set_frequency(uint32_t freq_hz) {
	tuned_hz = freq_hz;
	retunes++;
}

static void on_tcxo(int yes) {
	RCC->CFGR = yes ? (RCC_CFGR_SWS_PLL | RCC_CFGR_PLLSRC) : RCC_CFGR_SWS_HSI;
}

static void reset(void) {
	pps_disc_reset();
	on_tcxo(1);
	tuned_hz = 0;
	retunes = 0;
}

static void samples(int n, uint32_t cycles) {
	for (int i = 0; i < n; i++)
		pps_disc_sample(cycles, 1000);
}

/* -1.4167 ppm, about what board 1's TCXO reads */
#define SLOW	(PPS_DISC_NOMINAL - 34)

/* ---- tests -------------------------------------------------------------------- */

static void needs_a_full_window(void) {
	reset();
	samples(PPS_DISC_WINDOW - 1, SLOW);
	pps_disc_update();
	CHECK_EQ(pps_disc_valid, 0);
	CHECK_EQ(retunes, 0);
	samples(1, SLOW);
	pps_disc_update();
	CHECK_EQ(pps_disc_valid, 1);
	CHECK_EQ(pps_disc_samples, PPS_DISC_WINDOW);
}

static void corrects_slow_tcxo(void) {
	reset();
	samples(PPS_DISC_WINDOW, SLOW);
	pps_disc_update();
	CHECK_NEAR(pps_disc_ppb, -1416.7, 1);
	CHECK_EQ(pps_disc_applied_ppb, pps_disc_ppb);
	CHECK_EQ(retunes, 1);
	/* slow TCXO: program higher, by f * 1.4167e-6 = 632 Hz */
	CHECK_NEAR((double)tuned_hz - RF_FREQ_HZ_DFM, 632.1, 1);
	/* the carrier then lands on the requested frequency */
	CHECK_NEAR(tuned_hz * (1.0 + pps_disc_ppb * 1e-9), RF_FREQ_HZ_DFM, 1);
}

static void corrects_fast_tcxo(void) {
	reset();
	samples(PPS_DISC_WINDOW, PPS_DISC_NOMINAL + 48);	/* +2 ppm */
	pps_disc_update();
	CHECK_NEAR(pps_disc_ppb, 2000, 1);
	CHECK_NEAR((double)tuned_hz - RF_FREQ_HZ_DFM, -892.35, 1);
}

static void sub_cycle_resolution(void) {
	/* alternating counts average to half a cycle (20.8 ppb) */
	reset();
	for (int i = 0; i < PPS_DISC_WINDOW; i++)
		pps_disc_sample(SLOW + (i & 1), 1000);
	pps_disc_update();
	CHECK_NEAR(pps_disc_ppb, -1416.7 + 20.8, 1);
}

static void delayed_interrupts_trimmed(void) {
	/* a pulse held back by a higher-priority interrupt makes one interval
	 * long and the next short. the pair cancels in a mean, but only when
	 * both are in the window; 4 one-sided outliers in 16 must be dropped */
	reset();
	samples(PPS_DISC_WINDOW - 4, SLOW);
	samples(4, SLOW + 150);		/* 6 us late: accepted (< 10 ppm) */
	pps_disc_update();
	CHECK_NEAR(pps_disc_ppb, -1416.7, 1);
}

static void rejects_implausible(void) {
	reset();
	pps_disc_sample(PPS_DISC_NOMINAL * 2, 2000);		/* missed pulse */
	pps_disc_sample(SLOW, 2000);						/* tick says 2 s */
	pps_disc_sample(SLOW, 300000);						/* first after wake-up */
	pps_disc_sample(PPS_DISC_NOMINAL + 300, 1000);		/* +12.5 ppm */
	pps_disc_sample(PPS_DISC_NOMINAL - 300, 1000);
	pps_disc_update();
	CHECK_EQ(pps_disc_rejected, 5);
	CHECK_EQ(pps_disc_samples, 0);
}

static void ignores_samples_on_hsi(void) {
	reset();
	on_tcxo(0);		/* SYSCLK from HSI: the count doesn't measure the TCXO */
	samples(PPS_DISC_WINDOW, SLOW);
	pps_disc_update();
	CHECK_EQ(pps_disc_rejected, PPS_DISC_WINDOW);
	CHECK_EQ(pps_disc_valid, 0);
}

static void keeps_estimate_without_gps(void) {
	reset();
	samples(PPS_DISC_WINDOW, SLOW);
	pps_disc_update();
	int32_t ppb = pps_disc_ppb;
	for (int i = 0; i < 100; i++)
		pps_disc_update();		/* GPS asleep: no new samples */
	CHECK_EQ(pps_disc_ppb, ppb);
	CHECK_EQ(retunes, 1);
}

static void retunes_only_on_real_change(void) {
	reset();
	samples(PPS_DISC_WINDOW, SLOW);
	pps_disc_update();
	CHECK_EQ(retunes, 1);
	/* one sample a cycle different: a few ppb, under PPS_DISC_RETUNE_PPB */
	samples(1, SLOW + 1);
	pps_disc_update();
	CHECK_EQ(retunes, 1);
	/* the TCXO warms up by 0.15 ppm */
	samples(PPS_DISC_WINDOW, SLOW - 4);
	pps_disc_update();
	CHECK_EQ(retunes, 2);
	CHECK_NEAR(pps_disc_applied_ppb, -1583.3, 1);
}

static void disable_and_enable(void) {
	reset();
	samples(PPS_DISC_WINDOW, SLOW);
	pps_disc_update();
	pps_disc_enable = 0;
	pps_disc_update();
	CHECK_EQ(tuned_hz, RF_FREQ_HZ_DFM);		/* back on the nominal setting */
	CHECK_EQ(pps_disc_applied_ppb, 0);
	pps_disc_enable = 1;
	pps_disc_update();
	CHECK_NEAR((double)tuned_hz - RF_FREQ_HZ_DFM, 632.1, 1);
}

static void queue_overflow(void) {
	/* more samples than the queue holds between two updates (a long
	 * transmission): the oldest are dropped, nothing breaks */
	reset();
	samples(100, SLOW);
	pps_disc_update();
	CHECK_EQ(pps_disc_samples, 32);
	CHECK_EQ(pps_disc_valid, 1);
	CHECK_NEAR(pps_disc_ppb, -1416.7, 1);
}

static void freq_without_correction(void) {
	reset();
	CHECK_EQ(pps_disc_freq(RF_FREQ_HZ_DFM), RF_FREQ_HZ_DFM);
}

TEST_MAIN("ppsdisc",
	TEST(needs_a_full_window),
	TEST(corrects_slow_tcxo),
	TEST(corrects_fast_tcxo),
	TEST(sub_cycle_resolution),
	TEST(delayed_interrupts_trimmed),
	TEST(rejects_implausible),
	TEST(ignores_samples_on_hsi),
	TEST(keeps_estimate_without_gps),
	TEST(retunes_only_on_real_change),
	TEST(disable_and_enable),
	TEST(queue_overflow),
	TEST(freq_without_correction))
