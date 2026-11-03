/*
 * test_hunt.c - the fox hunt start (hunt.c, #72): boot, a set UTC time (with
 * a late boot and no GPS time), the button, the hunt clock, GPS time to Unix
 * seconds, and the settings checks
 */
#include "test.h"
#include "hunt.h"
#include "fake_settings.h"

/* 2026-11-04 14:00:00 UTC */
#define T0 1793800800UL

static GNSS_StateHandle at(
		uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s, uint32_t tick) {
	GNSS_StateHandle g = {0};
	g.year = y;
	g.month = mo;
	g.day = d;
	g.hour = h;
	g.min = mi;
	g.sec = s;
	g.valid = GNSS_VALID_DATE | GNSS_VALID_TIME;
	g.pvtTick = tick;
	return g;
}

static settings_t with(uint8_t mode, uint32_t utc) {
	settings_t s;
	provision_as(&s, "W1AW");
	s.hunt_start_mode = mode;
	s.hunt_start_utc = utc;
	return s;
}

static void unix_time(void) {
	CHECK_EQ(hunt_unix_from_date(1970, 1, 1, 0, 0, 0), 0);
	CHECK_EQ(hunt_unix_from_date(2000, 3, 1, 0, 0, 0), 951868800UL); /* after a leap day */
	CHECK_EQ(hunt_unix_from_date(2026, 11, 4, 14, 0, 0), T0);
	CHECK_EQ(hunt_unix_from_date(2028, 2, 29, 23, 59, 59), 1835481599UL);
	CHECK_EQ(hunt_unix_from_date(2026, 13, 1, 0, 0, 0), 0); /* not a date */
	/* from NAV-PVT, moved on by the ticks since it was parsed */
	GNSS_StateHandle g = at(2026, 11, 4, 14, 0, 0, 5000);
	CHECK_EQ(hunt_gnss_unix(&g, 5000), T0);
	CHECK_EQ(hunt_gnss_unix(&g, 65000), T0 + 60);
	g.valid = GNSS_VALID_DATE; /* no valid time yet */
	CHECK_EQ(hunt_gnss_unix(&g, 5000), 0);
}

static void boot_starts_at_once(void) {
	settings_t s = with(HUNT_START_BOOT, 0);
	GNSS_StateHandle g = {0};
	hunt_init(1000);
	CHECK_EQ(hunt_started(), 0);
	CHECK_EQ(hunt_poll(&s, &g, 1200, 0), 1);
	CHECK_EQ(hunt_elapsed_ms(61000), 60000); /* from boot */
	CHECK_EQ(hunt_state_now(), HUNT_STARTED);
	/* erased is boot too */
	s.hunt_start_mode = HUNT_START_DEFAULT;
	hunt_init(0);
	CHECK_EQ(hunt_poll(&s, &g, 10, 0), 1);
	CHECK_EQ(hunt_starts_paused(&s), 0);
}

static void time_waits_then_starts(void) {
	settings_t s = with(HUNT_START_TIME, T0);
	hunt_init(0);
	/* no GPS time yet */
	GNSS_StateHandle none = {0};
	CHECK_EQ(hunt_poll(&s, &none, 1000, 0), 0);
	CHECK_EQ(hunt_state_now(), HUNT_WAIT_GPS);
	CHECK_EQ(hunt_elapsed_ms(1000), 0);
	/* 10 minutes early */
	GNSS_StateHandle g = at(2026, 11, 4, 13, 50, 0, 2000);
	CHECK_EQ(hunt_poll(&s, &g, 2000, 0), 0);
	CHECK_EQ(hunt_state_now(), HUNT_WAIT_TIME);
	CHECK_EQ(hunt_poll(&s, &g, 2000 + 599000, 0), 0);
	/* on time, from the same NAV-PVT moved on by the tick */
	CHECK_EQ(hunt_poll(&s, &g, 2000 + 600000, 0), 1);
	CHECK_EQ(hunt_elapsed_ms(2000 + 600000 + 30000), 30000);
	/* started stays started: the GPS losing its time doesn't stop it */
	CHECK_EQ(hunt_poll(&s, &none, 10000000, 0), 1);
}

/* booted after the start: the hunt runs at once, its clock from the set time */
static void time_late_boot(void) {
	settings_t s = with(HUNT_START_TIME, T0);
	hunt_init(0);
	GNSS_StateHandle g = at(2026, 11, 4, 15, 30, 0, 5000); /* 90 min late */
	CHECK_EQ(hunt_poll(&s, &g, 5000, 0), 1);
	CHECK_EQ(hunt_elapsed_ms(5000), 90UL * 60 * 1000);
	/* days late: the clock is held at 10 days, well past any shrink */
	hunt_init(0);
	g = at(2026, 12, 4, 14, 0, 0, 5000);
	CHECK_EQ(hunt_poll(&s, &g, 5000, 0), 1);
	CHECK_EQ(hunt_elapsed_ms(5000), 864000UL * 1000);
}

/* never any GPS time: start HUNT_NO_TIME_MS after boot anyway */
static void time_without_gps(void) {
	settings_t s = with(HUNT_START_TIME, T0);
	GNSS_StateHandle none = {0};
	hunt_init(100);
	CHECK_EQ(hunt_poll(&s, &none, 100 + HUNT_NO_TIME_MS - 1, 0), 0);
	CHECK_EQ(hunt_poll(&s, &none, 100 + HUNT_NO_TIME_MS, 0), 1);
	CHECK_EQ(hunt_elapsed_ms(100 + HUNT_NO_TIME_MS + 5), 5);
}

static void button_starts_on_resume(void) {
	settings_t s = with(HUNT_START_BUTTON, 0);
	GNSS_StateHandle g = {0};
	CHECK_EQ(hunt_starts_paused(&s), 1);
	hunt_init(0);
	CHECK_EQ(hunt_poll(&s, &g, 1000, 1), 0); /* paused */
	CHECK_EQ(hunt_state_now(), HUNT_WAIT_BUTTON);
	CHECK_EQ(hunt_poll(&s, &g, 50000, 0), 1); /* resumed */
	CHECK_EQ(hunt_elapsed_ms(80000), 30000);
	/* pausing later doesn't restart it */
	CHECK_EQ(hunt_poll(&s, &g, 90000, 1), 1);
	CHECK_EQ(hunt_elapsed_ms(90000), 40000);
}

static void settings_limits(void) {
	settings_t s;
	provision_as(&s, "W1AW");
	CHECK_EQ(s.hunt_start_mode, HUNT_START_BOOT);
	CHECK_EQ(s.cw_grid_len, 0);
	CHECK_EQ(settings_check(&s), SETTINGS_OK);

	s.hunt_start_mode = HUNT_START_TIME; /* without a time */
	settings_seal(&s);
	CHECK_EQ(settings_check(&s), SETTINGS_ERR_HUNT_START);
	s.hunt_start_utc = 0xFFFFFFFFUL;
	settings_seal(&s);
	CHECK_EQ(settings_check(&s), SETTINGS_ERR_HUNT_START);
	s.hunt_start_utc = T0;
	settings_seal(&s);
	CHECK_EQ(settings_check(&s), SETTINGS_OK);
	s.hunt_start_mode = 3;
	settings_seal(&s);
	CHECK_EQ(settings_check(&s), SETTINGS_ERR_HUNT_START);
	s.hunt_start_mode = HUNT_START_DEFAULT;

	const uint8_t ok[] = {0, 4, 6, 8, 10, 0xFF}, bad[] = {2, 5, 12};
	for (unsigned i = 0; i < sizeof(ok); i++) {
		s.cw_grid_len = ok[i];
		settings_seal(&s);
		CHECK_EQ(settings_check(&s), SETTINGS_OK);
	}
	for (unsigned i = 0; i < sizeof(bad); i++) {
		s.cw_grid_len = bad[i];
		settings_seal(&s);
		CHECK_EQ(settings_check(&s), SETTINGS_ERR_CW_GRID);
	}
	/* the locator's Morse counts in the fox timing: 10 characters at 5 WPM
	 * is 33.6 s, too long with a 10 s tone in a 30 s period */
	s.cw_grid_len = 10;
	s.cw_wpm = 5;
	settings_seal(&s);
	CHECK_EQ(settings_check(&s), SETTINGS_ERR_FOX_TIMING);
	s.fox_period_s = 60;
	settings_seal(&s);
	CHECK_EQ(settings_check(&s), SETTINGS_OK);
}

TEST_MAIN("hunt", TEST(unix_time), TEST(boot_starts_at_once), TEST(time_waits_then_starts),
		TEST(time_late_boot), TEST(time_without_gps), TEST(button_starts_on_resume),
		TEST(settings_limits))
