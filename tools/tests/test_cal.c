/*
 * test_cal.c - the analog calibration block (cal.c): defaults, the checks,
 * per-channel fallback when loading, and the fixed-point scaling
 */
#include <string.h>
#include "test.h"
#include "fake_hal.h"
#include "cal.h"

/* a calibrated block: board 1's measured fits (#14, #43) */
static cal_t board1(void) {
	cal_t c;
	cal_defaults(&c);
	c.ch[CAL_VIN].a_q16 = 131020; /* 1.9992 */
	c.ch[CAL_VIN].b = 208;
	c.ch[CAL_IIN].a_q16 = 65602; /* 1.001 */
	c.ch[CAL_IIN].b = 4;
	c.measured = 0x07;
	c.date = 20261030;
	cal_seal(&c);
	return c;
}

void test_setup(void) {
	cal_defaults(&cal);
	cal_source = CAL_FROM_DEFAULTS;
	cal_error = CAL_OK;
}

static void defaults_are_valid(void) {
	cal_t c;
	cal_defaults(&c);
	CHECK_EQ(cal_check(&c), CAL_OK);
	CHECK_EQ(c.measured, 0);
	CHECK_EQ(c.date, 0xFFFFFFFFUL);
	CHECK_EQ(c.ch[CAL_VIN].a_q16, 131072); /* 2.0 */
	CHECK_EQ(c.ch[CAL_VIN].b, 208);
	CHECK_EQ(c.ch[CAL_IIN].a_q16, 65536); /* 1.0 */
	CHECK_EQ(c.ch[CAL_IIN].b, 4);
	CHECK_EQ(c.ch[CAL_USB].a_q16, 132383); /* 2.02 */
	CHECK_EQ(c.ch[CAL_USB].b, 0);
	CHECK_EQ(c.reserved1, 0xFF);
	CHECK_EQ(c.reserved[0], 0xFF);
	CHECK_EQ(c.reserved[1], 0xFF);
}

/* the same CRC as tools/config/dfmcfg.py: binascii.crc_hqx() of the first
 * 30 bytes of the defaults, computed in Python */
static void crc_matches_python(void) {
	cal_t c;
	cal_defaults(&c);
	CHECK_EQ(c.crc, 0x2535);
}

static void check_rejects_bad_blocks(void) {
	cal_t c;
	memset(&c, 0xFF, sizeof(c)); /* erased */
	CHECK_EQ(cal_check(&c), CAL_ERR_MAGIC);

	c = board1();
	c.version = 2;
	CHECK_EQ(cal_check(&c), CAL_ERR_VERSION);
	c = board1();
	c.length = 31;
	CHECK_EQ(cal_check(&c), CAL_ERR_VERSION);

	c = board1();
	c.ch[CAL_IIN].b++; /* not resealed */
	CHECK_EQ(cal_check(&c), CAL_ERR_CRC);
}

/* gain within 25% of the default, |offset| <= 500 */
static void range_limits(void) {
	cal_t c = board1();
	CHECK_EQ(cal_bad_channels(&c), 0);

	c.ch[CAL_VIN].a_q16 = 131072 + 131072 / 4; /* +25%: still in */
	c.ch[CAL_IIN].b = -500;
	c.ch[CAL_USB].a_q16 = 132383 - 132383 / 4;
	cal_seal(&c);
	CHECK_EQ(cal_bad_channels(&c), 0);
	CHECK_EQ(cal_check(&c), CAL_OK);

	c.ch[CAL_VIN].a_q16++;
	CHECK_EQ(cal_bad_channels(&c), 1 << CAL_VIN);
	c.ch[CAL_IIN].b = -501;
	CHECK_EQ(cal_bad_channels(&c), (1 << CAL_VIN) | (1 << CAL_IIN));
	c.ch[CAL_USB].a_q16 = 0;
	c.ch[CAL_IIN].b = 501;
	CHECK_EQ(cal_bad_channels(&c), 0x07);
	cal_seal(&c);
	CHECK_EQ(cal_check(&c), CAL_ERR_RANGE);
}

static void load_valid_block(void) {
	cal_t c = board1();
	CHECK_EQ(cal_load(&c), CAL_OK);
	CHECK_MEM(&cal, &c, sizeof(c));
	CHECK_EQ(cal_source, CAL_FROM_EEPROM);
	CHECK_EQ(cal_error, CAL_OK);
}

/* a channel out of range gets its default and loses its measured bit; the
 * others are used */
static void load_falls_back_per_channel(void) {
	cal_t c = board1();
	c.ch[CAL_IIN].a_q16 = 200000; /* about 3.05: nonsense for 1 V/A */
	cal_seal(&c);
	CHECK_EQ(cal_load(&c), CAL_ERR_RANGE);
	CHECK_EQ(cal_source, CAL_FROM_EEPROM);
	CHECK_EQ(cal_error, CAL_ERR_RANGE);
	CHECK_EQ(cal.ch[CAL_VIN].a_q16, 131020);
	CHECK_EQ(cal.ch[CAL_IIN].a_q16, 65536);
	CHECK_EQ(cal.ch[CAL_IIN].b, 4);
	CHECK_EQ(cal.measured, 0x07 & ~(1 << CAL_IIN));
	CHECK_EQ(cal_check(&cal), CAL_OK); /* resealed */
}

/* not a block at all: nothing changes */
static void load_ignores_invalid_block(void) {
	cal_t before = cal, c = board1();
	c.date++; /* CRC no longer matches */
	CHECK_EQ(cal_load(&c), CAL_ERR_CRC);
	CHECK_MEM(&cal, &before, sizeof(cal));
	CHECK_EQ(cal_source, CAL_FROM_DEFAULTS);
	CHECK_EQ(cal_error, CAL_OK);
}

static void scale_gain_and_offset(void) {
	/* defaults: 2.0 x + 208 */
	CHECK_EQ(cal_scale(CAL_VIN, 2330), 4868);
	CHECK_EQ(cal_scale(CAL_IIN, 54), 58);
	CHECK_EQ(cal_scale(CAL_USB, 2475), 5000); /* 2.02 x 2475 = 4999.5, rounded */
	cal_t c = board1();
	cal_load(&c);
	/* 1.9992 x 2330 + 208 = 4866.1 */
	CHECK_EQ(cal_scale(CAL_VIN, 2330), 4866);
	/* 1.001 x 100 + 4 = 104.1 */
	CHECK_EQ(cal_scale(CAL_IIN, 100), 104);
}

static void scale_held_to_range(void) {
	cal_t c;
	cal_defaults(&c);
	c.ch[CAL_IIN].b = -100;
	cal_seal(&c);
	cal_load(&c);
	CHECK_EQ(cal_scale(CAL_IIN, 50), 0); /* 50 - 100: not negative */
	CHECK_EQ(cal_scale(CAL_IIN, 0), 0);
	/* a nonsense pin voltage (VREFINT read as 1 count) doesn't overflow */
	CHECK_EQ(cal_scale(CAL_VIN, 4000000), 2 * 8191 + 208);
	c.ch[CAL_VIN].a_q16 = 131072 + 131072 / 4;
	c.ch[CAL_VIN].b = 500;
	cal_seal(&c);
	cal_load(&c);
	CHECK_EQ(cal_scale(CAL_VIN, 0xFFFFFFFFUL), (uint16_t)(2.5 * 8191 + 500 + 0.5));
}

TEST_MAIN("cal", TEST(defaults_are_valid), TEST(crc_matches_python), TEST(check_rejects_bad_blocks),
		TEST(range_limits), TEST(load_valid_block), TEST(load_falls_back_per_channel),
		TEST(load_ignores_invalid_block), TEST(scale_gain_and_offset), TEST(scale_held_to_range))
