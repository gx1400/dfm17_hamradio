/*
 * test_foxgps.c - the fox's GPS power policy (foxgps.c) against a scripted GPS
 *
 * As in test_tracker.c: awake, the fake GPS reports a fix from a per-test
 * script, polled every 5 s like gpsUpdate(); asleep, the PPS stops. It can
 * also ignore sleep commands, as the u-blox did once right after a reset.
 */
#include "test.h"
#include "fake_hal.h"
#include "foxgps.h"
#include "gps.h"
#include "fake_settings.h"

/* ---- fakes ------------------------------------------------------------------ */

volatile uint8_t ppsLockStatus;
static GNSS_StateHandle gnss;
static int asleep, ignore_sleeps, wakes, sleeps;
static uint32_t woke_at;
/* the fix fixType at t ms since the last wake (and the wake number), -1 = no PPS */
static int (*script)(uint32_t t, int wake);

void gpsSleep(void) {
	sleeps++;
	if (ignore_sleeps > 0)
		ignore_sleeps--;
	else
		asleep = 1;
}
void gpsWake(void) {
	wakes++;
	asleep = 0;
	woke_at = fake_tick;
	gnss.fixType = NoFix;
}

static void reset(int (*s)(uint32_t, int)) {
	memset(&gnss, 0, sizeof(gnss));
	asleep = ignore_sleeps = wakes = sleeps = 0;
	fake_tick = 1000;
	woke_at = fake_tick;
	script = s;
	ppsLockStatus = 0;
	fox_gps_start();
}

/* the main loop for ms, 100 ms per pass, the GPS polled every 5 s */
static void run(uint32_t ms) {
	uint32_t end = fake_tick + ms;
	while (fake_tick < end) {
		if (asleep) {
			ppsLockStatus = 0;
		} else {
			int f = script(fake_tick - woke_at, wakes);
			ppsLockStatus = f >= 0;
			if (f >= 0 && (fake_tick - woke_at) % 5000 == 0) {
				gnss.fixType = f;
				gnss.lat = f >= Fix3D ? 389987994 + wakes : 0;
			}
		}
		fox_gps_poll(&gnss, fake_tick);
		fake_tick += 100;
	}
}

/* ---- GPS scripts -------------------------------------------------------------- */

static int fix_after_5s(uint32_t t, int w) {
	(void)w;
	return t >= 5000 ? Fix3D : -1;
}
static int never(uint32_t t, int w) {
	(void)t;
	(void)w;
	return -1;
}
/* a fix at power-up, then none at any resync (antenna covered) */
static int first_fix_only(uint32_t t, int w) {
	return w == 0 && t >= 5000 ? Fix3D : NoFix;
}

#define HOUR (FOX_GPS_RESYNC_S * 1000UL)

/* ---- tests -------------------------------------------------------------------- */

static void sleeps_after_settled_fix(void) {
	reset(fix_after_5s);
	run(60000);
	CHECK_EQ(sleeps, 1);
	CHECK(fox_gps_asleep());
	CHECK(fox_gps_last_fix() != NULL);
	CHECK_EQ(fox_gps_last_fix()->fixType, Fix3D);
}

/* a cold start needs uninterrupted signal: no timeout before the first fix */
static void stays_on_until_first_fix(void) {
	reset(never);
	run(2 * HOUR);
	CHECK_EQ(sleeps, 0);
	CHECK(!fox_gps_asleep());
	CHECK(fox_gps_last_fix() == NULL);
}

static void resyncs_every_hour(void) {
	reset(fix_after_5s);
	run(2 * HOUR + 60000);
	CHECK_EQ(wakes, 2);
	CHECK_EQ(sleeps, 3);
	CHECK(fox_gps_asleep());
	CHECK_EQ(fox_gps_last_fix()->lat, 389987994 + 2);
}

/* no fix at a resync: back to sleep at the fix timeout, the old fix kept */
static void resync_without_fix_keeps_last(void) {
	reset(first_fix_only);
	run(60000);
	CHECK_EQ(sleeps, 1);
	run(HOUR);
	CHECK_EQ(wakes, 1);
	CHECK(!fox_gps_asleep());
	run(cfg.tracker_fix_timeout_s * 1000UL);
	CHECK_EQ(sleeps, 2);
	CHECK(fox_gps_asleep());
	CHECK_EQ(fox_gps_last_fix()->lat, 389987994);
}

/* a sleep command the GPS didn't take: the PPS keeps coming, so it's sent
 * again after FOX_GPS_SLEEP_CHECK_MS */
static void lost_sleep_command_is_resent(void) {
	reset(fix_after_5s);
	ignore_sleeps = 1;
	run(30000);
	CHECK_EQ(sleeps, 2);
	CHECK(asleep);
	run(30000);
	CHECK_EQ(sleeps, 2);
}

static void gives_up_after_sleep_tries(void) {
	reset(fix_after_5s);
	ignore_sleeps = 100;
	run(120000);
	CHECK_EQ(sleeps, FOX_GPS_SLEEP_TRIES);
}

static void always_on_never_sleeps(void) {
	reset(fix_after_5s);
	cfg.gps_power = GPS_POWER_ALWAYS_ON;
	run(2 * HOUR);
	CHECK_EQ(sleeps, 0);
	CHECK(fox_gps_last_fix() != NULL);
}

static void always_on_wakes_a_sleeping_gps(void) {
	reset(fix_after_5s);
	run(60000);
	CHECK(fox_gps_asleep());
	cfg.gps_power = GPS_POWER_ALWAYS_ON;
	run(200);
	CHECK_EQ(wakes, 1);
	CHECK(!fox_gps_asleep());
}

/* every test runs on a freshly provisioned board (fake_settings.h) */
void test_setup(void) {
	provision();
}

TEST_MAIN("foxgps", TEST(sleeps_after_settled_fix), TEST(stays_on_until_first_fix),
		TEST(resyncs_every_hour), TEST(resync_without_fix_keeps_last),
		TEST(lost_sleep_command_is_resent), TEST(gives_up_after_sleep_tries),
		TEST(always_on_never_sleeps), TEST(always_on_wakes_a_sleeping_gps))
