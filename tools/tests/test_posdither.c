/*
 * test_posdither.c - position dithering (posdither.c, #72): every offset
 * within R, uniform over the disc, the longitude scale and wrap, the shrinking
 * radius, the altitude, and off meaning untouched
 */
#include <math.h>
#include <stdlib.h>
#include "test.h"
#include "posdither.h"
#include "fake_settings.h"

#define M_PER_DEG 111320.0

static settings_t on(uint8_t mode, uint16_t r) {
	settings_t s;
	settings_defaults(&s);
	s.pos_dither_mode = mode;
	s.pos_dither_m = r;
	return s;
}

/* the distance in m between two positions in 1e-7 deg (flat-earth, fine here) */
static double metres(int32_t lat0, int32_t lon0, int32_t lat1, int32_t lon1) {
	double north = ((double)lat1 - lat0) / 1e7 * M_PER_DEG;
	double dlon = ((double)lon1 - lon0) / 1e7; /* across the date line: no int overflow */
	if (dlon > 180)
		dlon -= 360;
	if (dlon < -180)
		dlon += 360;
	double east = dlon * M_PER_DEG * cos(lat0 / 1e7 * M_PI / 180);
	return sqrt(north * north + east * east);
}

static void off_leaves_position(void) {
	settings_t s = on(POS_DITHER_OFF, 500);
	int32_t lat = 389987994, lon = -946819494, alt = 309000;
	CHECK_EQ(posdither_apply(&s, 1000, &lat, &lon, &alt), 0);
	CHECK(lat == 389987994 && lon == -946819494 && alt == 309000);
	s.pos_dither_mode = POS_DITHER_DEFAULT; /* erased */
	CHECK_EQ(posdither_apply(&s, 1000, &lat, &lon, &alt), 0);
	CHECK_EQ(lat, 389987994);
	/* on with R = 0 and no altitude: the real position */
	s = on(POS_DITHER_FIXED, 0);
	CHECK_EQ(posdither_apply(&s, 1000, &lat, &lon, &alt), 1);
	CHECK(lat == 389987994 && lon == -946819494 && alt == 309000);
}

/* every draw within R, a new one each time, and uniform over the area: equal
 * rings of r^2 hold equal shares */
static void within_radius_and_uniform(void) {
	settings_t s = on(POS_DITHER_FIXED, 500);
	enum { N = 20000, RINGS = 5 };
	int ring[RINGS] = {0};
	double max = 0;
	int same = 0;
	int32_t prev_lat = 0;
	posdither_seed(12345);
	for (int i = 0; i < N; i++) {
		int32_t lat = 389987994, lon = -946819494, alt = 309000;
		posdither_apply(&s, 1000, &lat, &lon, &alt);
		double d = metres(389987994, -946819494, lat, lon);
		if (d > max)
			max = d;
		int k = (int)(d * d / (500.0 * 500.0) * RINGS);
		ring[k < RINGS ? k : RINGS - 1]++;
		same += lat == prev_lat;
		prev_lat = lat;
		CHECK_EQ(alt, 309000); /* no altitude dither set */
	}
	CHECK(max <= 500.5); /* rounding to 1e-7 deg is about 1 cm */
	CHECK(max > 490);
	CHECK(same < N / 100);
	for (int k = 0; k < RINGS; k++)
		CHECK(abs(ring[k] - N / RINGS) < N / RINGS / 10); /* each within 10 % */
}

/* the longitude offset is scaled by 1/cos(latitude) */
static void longitude_scale(void) {
	CHECK_EQ(posdither_cos_q16(0), 65535);
	CHECK(abs((int)posdither_cos_q16(450000000) - 46340) <= 1);
	CHECK(abs((int)posdither_cos_q16(-700000000) - 22414) <= 1);
	CHECK_EQ(posdither_cos_q16(895000000), posdither_cos_q16(890000000)); /* held */
	/* half way between whole degrees: interpolated */
	uint32_t c = posdither_cos_q16(455000000);
	CHECK(c < posdither_cos_q16(450000000) && c > posdither_cos_q16(460000000));

	settings_t s = on(POS_DITHER_FIXED, 1000);
	const int32_t lats[] = {0, 450000000, 700000000};
	for (unsigned j = 0; j < 3; j++) {
		double max = 0;
		posdither_seed(99 + j);
		for (int i = 0; i < 4000; i++) {
			int32_t lat = lats[j], lon = 100000000, alt = 0;
			posdither_apply(&s, 0, &lat, &lon, &alt);
			double d = metres(lats[j], 100000000, lat, lon);
			if (d > max)
				max = d;
		}
		CHECK(max <= 1000.5);
		CHECK(max > 980);
	}
}

/* longitude wraps at +-180 deg, latitude stays within +-90 deg */
static void wraps_at_date_line(void) {
	settings_t s = on(POS_DITHER_FIXED, 2000);
	int wrapped = 0;
	posdither_seed(7);
	for (int i = 0; i < 2000; i++) {
		int32_t lat = 0, lon = 1799999000, alt = 0; /* 179.9999 E */
		posdither_apply(&s, 0, &lat, &lon, &alt);
		CHECK(lon > -1800000000 && lon <= 1800000000);
		wrapped += lon < 0;
		CHECK(metres(0, 1799999000, lat, lon) <= 2000.5);
	}
	CHECK(wrapped > 0);
	for (int i = 0; i < 2000; i++) {
		int32_t lat = 899990000, lon = 0, alt = 0; /* 89.999 N */
		posdither_apply(&s, 0, &lat, &lon, &alt);
		CHECK(lat <= 900000000);
	}
}

/* shrinking: R0 at the start, linear, R_min at and after the end */
static void shrinking_radius(void) {
	settings_t s = on(POS_DITHER_SHRINK, 1000);
	s.pos_dither_min_m = 100;
	s.pos_dither_minutes = 60;
	CHECK_EQ(posdither_radius_m(&s, 0), 1000);
	CHECK_EQ(posdither_radius_m(&s, 30 * 60000UL), 550);
	CHECK_EQ(posdither_radius_m(&s, 60 * 60000UL), 100);
	CHECK_EQ(posdither_radius_m(&s, 600 * 60000UL), 100);
	s.pos_dither_min_m = 0;
	CHECK_EQ(posdither_radius_m(&s, 60 * 60000UL), 0);
	/* at the end with R_min 0 the report is the real position */
	int32_t lat = 389987994, lon = -946819494, alt = 309000;
	posdither_apply(&s, 61 * 60000UL, &lat, &lon, &alt);
	CHECK(lat == 389987994 && lon == -946819494);
	/* fixed ignores the time */
	s.pos_dither_mode = POS_DITHER_FIXED;
	CHECK_EQ(posdither_radius_m(&s, 600 * 60000UL), 1000);
	s.pos_dither_mode = POS_DITHER_OFF;
	CHECK_EQ(posdither_radius_m(&s, 0), 0);
}

/* the altitude moves by up to +-pos_dither_alt_m, both ways */
static void altitude(void) {
	settings_t s = on(POS_DITHER_FIXED, 0);
	s.pos_dither_alt_m = 50;
	int up = 0, down = 0;
	for (int i = 0; i < 2000; i++) {
		int32_t lat = 0, lon = 0, alt = 309000;
		posdither_apply(&s, 0, &lat, &lon, &alt);
		CHECK(alt >= 309000 - 50000 && alt <= 309000 + 50000);
		up += alt > 309000;
		down += alt < 309000;
	}
	CHECK(up > 800 && down > 800);
}

/* a different seed gives different offsets; the same seed the same ones */
static void seeding(void) {
	settings_t s = on(POS_DITHER_FIXED, 500);
	int32_t a[2][8];
	for (int k = 0; k < 2; k++) {
		posdither_seed(k ? 2 : 1);
		for (int i = 0; i < 8; i++) {
			int32_t lat = 0, lon = 0, alt = 0;
			posdither_apply(&s, 0, &lat, &lon, &alt);
			a[k][i] = lat;
		}
	}
	CHECK(memcmp(a[0], a[1], sizeof(a[0])) != 0);
	posdither_seed(1);
	uint32_t r1 = posdither_rand();
	posdither_seed(1);
	CHECK_EQ(posdither_rand(), r1);
	posdither_seed(0); /* xorshift can't run from 0 */
	CHECK(posdither_rand() != 0);
	posdither_stir(0x12345678);
	CHECK(posdither_rand() != 0);
}

/* settings_check: the limits, and the rest ignored while off */
static void settings_limits(void) {
	settings_t s;
	provision_as(&s, "W1AW");
	CHECK_EQ(s.pos_dither_mode, POS_DITHER_OFF);
	const struct {
		uint8_t mode;
		uint16_t r, rmin, minutes, alt;
		enum settings_error want;
	} cases[] = {
			{POS_DITHER_FIXED, 500, 0, 60, 0, SETTINGS_OK},
			{POS_DITHER_FIXED, 50000, 0, 0, 1000, SETTINGS_OK},
			{POS_DITHER_FIXED, 50001, 0, 60, 0, SETTINGS_ERR_POS_DITHER},
			{POS_DITHER_FIXED, 500, 0, 60, 1001, SETTINGS_ERR_POS_DITHER},
			{POS_DITHER_SHRINK, 500, 500, 60, 0, SETTINGS_OK},
			{POS_DITHER_SHRINK, 500, 501, 60, 0, SETTINGS_ERR_POS_DITHER},
			{POS_DITHER_SHRINK, 500, 0, 0, 0, SETTINGS_ERR_POS_DITHER},
			{POS_DITHER_OFF, 0xFFFF, 0xFFFF, 0, 0xFFFF, SETTINGS_OK},
			{POS_DITHER_DEFAULT, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, SETTINGS_OK},
			{3, 500, 0, 60, 0, SETTINGS_ERR_POS_DITHER},
	};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		s.pos_dither_mode = cases[i].mode;
		s.pos_dither_m = cases[i].r;
		s.pos_dither_min_m = cases[i].rmin;
		s.pos_dither_minutes = cases[i].minutes;
		s.pos_dither_alt_m = cases[i].alt;
		settings_seal(&s);
		CHECK_EQ(settings_check(&s), cases[i].want);
	}
}

TEST_MAIN("posdither", TEST(off_leaves_position), TEST(within_radius_and_uniform),
		TEST(longitude_scale), TEST(wraps_at_date_line), TEST(shrinking_radius), TEST(altitude),
		TEST(seeding), TEST(settings_limits))
