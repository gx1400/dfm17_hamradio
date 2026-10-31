/*
 * test_tracker.c - the tracker cycle (tracker.c) against a scripted GPS
 *
 * The fake GPS behaves like gps.c + the u-blox: while awake it reports a fix
 * from a per-test script, polled every 5 s like gpsUpdate(); asleep, PPS stops
 * but the last fixType stays in the handle until gpsWake() clears it.
 */
#include "test.h"
#include "fake_hal.h"
#include "tracker.h"
#include "aprs.h"
#include "gps.h"
#include "fake_settings.h"

/* ---- fakes ------------------------------------------------------------------ */

volatile uint8_t ppsLockStatus;
static GNSS_StateHandle gnss;
static int asleep;
static uint32_t woke_at;
static int sleeps, wakes;

/* what the GPS reports t ms after waking: fix type, or -1 for no PPS */
static int (*script)(uint32_t t);

typedef struct { uint32_t at; int position; int from_last_fix; long lat; } sent_packet;
static sent_packet sent[64];
static int nsent;
static int prepared_from_last_fix;

void gpsSleep(void) { sleeps++; asleep = 1; }
void gpsWake(void) {
	wakes++;
	/* waking a GPS that never slept doesn't restart its acquisition */
	if (asleep)
		woke_at = fake_tick;
	asleep = 0;
	gnss.fixType = 0;
	gnss.numSV = 0;
}
void diag_set_tx(uint8_t on) { (void)on; }

uint8_t aprs_prepare_buffer(GNSS_StateHandle *g, uint8_t backlog_fix) {
	(void)backlog_fix;
	prepared_from_last_fix = 0;
	sent[nsent].lat = g->lat;
	sent[nsent].position = ppsLockStatus && g->fixType >= Fix2D;
	return sent[nsent].position;
}

uint8_t aprs_prepare_fix(GNSS_StateHandle *g, uint8_t fix_valid, uint8_t backlog_fix) {
	(void)backlog_fix;
	prepared_from_last_fix = 1;
	sent[nsent].lat = g->lat;
	sent[nsent].position = fix_valid;
	return fix_valid;
}

void tx_aprs(void) {
	sent[nsent].at = fake_tick;
	sent[nsent].from_last_fix = prepared_from_last_fix;
	nsent++;
}

/* whether packet i carried a position (what aprs_prepare_* returned) */
#define POSITION(i) (sent[i].position)

static void reset(int (*s)(uint32_t)) {
	memset(&gnss, 0, sizeof(gnss));
	memset(sent, 0, sizeof(sent));
	nsent = sleeps = wakes = 0;
	asleep = 0;
	fake_tick = 1000;
	woke_at = fake_tick;
	script = s;
	ppsLockStatus = 0;
	tracker_start();
}

/* run the main loop for ms, 100 ms per pass, polling the GPS every 5 s */
static void run(uint32_t ms) {
	uint32_t end = fake_tick + ms;
	while (fake_tick < end) {
		if (asleep) {
			ppsLockStatus = 0;
		} else {
			int f = script(fake_tick - woke_at);
			ppsLockStatus = f >= 0;
			if (f >= 0 && (fake_tick - woke_at) % 5000 == 0) {
				gnss.fixType = f;
				gnss.numSV = f >= Fix3D ? 8 : 3;
				gnss.lat = f >= Fix2D ? 389987994 + (long)fake_tick : 0;
			}
		}
		tracker_poll(&gnss);
		fake_tick += 100;
	}
}

/* ---- GPS scripts -------------------------------------------------------------- */

static int fix_after_5s(uint32_t t) { return t >= 5000 ? Fix3D : -1; }
static int never(uint32_t t) { (void)t; return -1; }
static int pps_but_2d(uint32_t t) { (void)t; return Fix2D; }
/* 3D at every other 5 s poll: never holds for 10 s */
static int flaky(uint32_t t) { return (t / 5000) % 2 ? Fix3D : NoFix; }
/* time (PPS) but no position until 150 s of uninterrupted signal, longer
 * than tracker_fix_timeout_s: a cold start, or reacquiring with fresh
 * ephemeris in a parked car */
static int fix_after_150s(uint32_t t) { return t >= 150000 ? Fix3D : NoFix; }

/* ---- tests -------------------------------------------------------------------- */

static void quick_fix_sends_position_and_sleeps(void) {
	reset(fix_after_5s);
	run(30000);
	CHECK_EQ(nsent, 1);
	/* fix first seen at the 5 s poll, held tracker_settle_s */
	CHECK(sent[0].at - 1000 >= 5000 + cfg.tracker_settle_s * 1000UL);
	CHECK(sent[0].at - 1000 < 5000 + cfg.tracker_settle_s * 1000UL + 5000);
	CHECK(POSITION(0));
	CHECK_EQ(sent[0].from_last_fix, 0);
	CHECK_EQ(sleeps, 1);
}

static void cycle_repeats_every_period(void) {
	reset(fix_after_5s);
	run(3 * cfg.tracker_period_s * 1000UL + 30000);
	CHECK_EQ(nsent, 4);
	CHECK_EQ(wakes, 3);
	CHECK_EQ(sleeps, 4);
	for (int i = 1; i < nsent; i++)
		CHECK_EQ(sent[i].at - sent[i - 1].at, cfg.tracker_period_s * 1000UL);
}

static void no_fix_times_out_and_keeps_gps_on(void) {
	reset(never);
	run(cfg.tracker_period_s * 1000UL);
	CHECK_EQ(nsent, 1);
	CHECK_EQ(sent[0].at - 1000, cfg.tracker_fix_timeout_s * 1000UL);
	CHECK(!POSITION(0));
	/* no fix yet since power-up: the GPS stays on for its cold start */
	CHECK_EQ(sleeps, 0);
}

static void two_d_only_is_sent_at_timeout(void) {
	reset(pps_but_2d);
	run(cfg.tracker_period_s * 1000UL);
	CHECK_EQ(nsent, 1);
	CHECK_EQ(sent[0].at - 1000, cfg.tracker_fix_timeout_s * 1000UL);
	/* no 3D fix to fall back on; aprs_prepare_buffer sends the 2D fix */
	CHECK_EQ(sent[0].from_last_fix, 0);
	CHECK(POSITION(0));
}

static void flaky_fix_sends_last_3d_fix(void) {
	reset(flaky);
	run(cfg.tracker_period_s * 1000UL);
	CHECK_EQ(nsent, 1);
	CHECK_EQ(sent[0].at - 1000, cfg.tracker_fix_timeout_s * 1000UL);
	CHECK_EQ(sent[0].from_last_fix, 1);
	CHECK(POSITION(0));
	CHECK_EQ(sleeps, 1);
}

static void last_fix_does_not_carry_over(void) {
	/* cycle 1 gets a fix; cycle 2 none: it must not resend cycle 1's fix */
	reset(fix_after_5s);
	run(cfg.tracker_period_s * 1000UL - 1000);
	CHECK_EQ(nsent, 1);
	script = never;
	run(cfg.tracker_period_s * 1000UL);
	CHECK_EQ(nsent, 2);
	CHECK_EQ(sent[1].from_last_fix, 0);
	CHECK(!POSITION(1));
	/* no position this cycle: the GPS stays on to reacquire */
	CHECK_EQ(sleeps, 1);
}

static void stale_fix_after_wake_is_ignored(void) {
	/* the handle still says 3D from before sleep; gpsWake must clear it */
	reset(fix_after_5s);
	run(cfg.tracker_period_s * 1000UL - 1000);
	CHECK_EQ(gnss.fixType, Fix3D);		/* stale while asleep */
	script = never;
	run(cfg.tracker_fix_timeout_s * 1000UL + 2000);
	CHECK_EQ(nsent, 2);
	CHECK(!POSITION(1));
}

static void late_first_fix(void) {
	/* cold start longer than the timeout: status first, position next cycle */
	reset(fix_after_150s);
	run(2 * cfg.tracker_period_s * 1000UL);
	CHECK_EQ(nsent, 2);
	CHECK(!POSITION(0));
	CHECK(POSITION(1));
	CHECK_EQ(sleeps, 1);
}

static void reacquires_after_losing_fix(void) {
	/* 2026-10-29: after a fix on the drive, 31 cycles of 2 min windows in a
	 * parked car never got the fix back; the GPS must stay on until it does */
	reset(fix_after_5s);
	run(cfg.tracker_period_s * 1000UL - 1000);
	CHECK_EQ(nsent, 1);
	CHECK(POSITION(0));
	CHECK_EQ(sleeps, 1);

	script = fix_after_150s;
	run(cfg.tracker_period_s * 1000UL); /* cycle 2: times out */
	CHECK_EQ(nsent, 2);
	CHECK(!POSITION(1));
	CHECK_EQ(sleeps, 1);			/* stayed on */
	run(cfg.tracker_period_s * 1000UL); /* cycle 3: the fix is back */
	CHECK_EQ(nsent, 3);
	CHECK(POSITION(2));
	/* sent as soon as the fix has settled, not at the timeout */
	CHECK(sent[2].at - sent[1].at < cfg.tracker_period_s * 1000UL
					- cfg.tracker_fix_timeout_s * 1000UL + cfg.tracker_settle_s * 1000UL + 10000);
	CHECK_EQ(sleeps, 2);
}

/* every test runs on a board set up as NZ0P (fake_settings.h) */
/* gps_power=always_on (#37): positions as before, the GPS never sleeps */
static void always_on_never_sleeps(void) {
	cfg.gps_power = GPS_POWER_ALWAYS_ON;
	settings_seal(&cfg);
	reset(fix_after_5s);
	run(3 * cfg.tracker_period_s * 1000UL + 30000);
	CHECK_EQ(nsent, 4);
	for (int i = 0; i < nsent; i++)
		CHECK(POSITION(i));
	CHECK_EQ(sleeps, 0);
	CHECK_EQ(wakes, 0);
}

/* switched on while the GPS sleeps: woken at once, and never slept again */
static void always_on_wakes_a_sleeping_gps(void) {
	reset(fix_after_5s);
	run(30000);
	CHECK_EQ(sleeps, 1);
	CHECK(asleep);
	cfg.gps_power = GPS_POWER_ALWAYS_ON;
	settings_seal(&cfg);
	run(200);
	CHECK(!asleep);
	CHECK_EQ(wakes, 1);
	run(2 * cfg.tracker_period_s * 1000UL);
	CHECK_EQ(sleeps, 1);
	CHECK_EQ(wakes, 1);
}

void test_setup(void) {
	provision();
}

TEST_MAIN("tracker", TEST(quick_fix_sends_position_and_sleeps), TEST(cycle_repeats_every_period),
		TEST(no_fix_times_out_and_keeps_gps_on), TEST(two_d_only_is_sent_at_timeout),
		TEST(flaky_fix_sends_last_3d_fix), TEST(last_fix_does_not_carry_over),
		TEST(stale_fix_after_wake_is_ignored), TEST(late_first_fix),
		TEST(reacquires_after_losing_fix), TEST(always_on_never_sleeps),
		TEST(always_on_wakes_a_sleeping_gps))
