/*
 * test_ukhas.c - UKHAS sentences (ukhas.c): fields, number formatting, and
 * the checksum against CRC-16/CCITT-FALSE computed outside the firmware
 * (Python binascii.crc_hqx(text, 0xFFFF))
 */
#include "test.h"
#include "ukhas.h"

static void typical_fix(void) {
	char buf[UKHAS_MAX];
	ukhas_data_t d = {12, 1, 23, 45, 390997000, -945786000, 250, 7, 30, 4790};
	uint8_t n = ukhas_sentence(buf, sizeof(buf), "NZ0P", &d);
	CHECK_STR(buf, "$$NZ0P,12,01:23:45,39.09970,-94.57860,250,7,30,4790*F6E9\n");
	CHECK_EQ(n, strlen(buf));
}

static void no_fix_zeros(void) {
	char buf[UKHAS_MAX];
	ukhas_data_t d = {0, 0, 0, 0, 0, 0, 0, 0, -5, 0};
	ukhas_sentence(buf, sizeof(buf), "NZ0P", &d);
	CHECK_STR(buf, "$$NZ0P,0,00:00:00,0.00000,0.00000,0,0,-5,0*F528\n");
}

static void southern_eastern_negative_alt(void) {
	char buf[UKHAS_MAX];
	ukhas_data_t d = {65535, 23, 59, 59, -338688000, 1512093000, -12, 12, -40, 65535};
	ukhas_sentence(buf, sizeof(buf), "KE0ABC", &d);
	CHECK_STR(buf, "$$KE0ABC,65535,23:59:59,-33.86880,151.20930,-12,12,-40,65535*EB22\n");
}

static void small_degrees_keep_sign(void) {
	char buf[UKHAS_MAX];
	ukhas_data_t d = {1, 12, 0, 0, -1000, 100, 30000, 9, 25, 3000};
	ukhas_sentence(buf, sizeof(buf), "NZ0P", &d);
	CHECK_STR(buf, "$$NZ0P,1,12:00:00,-0.00010,0.00001,30000,9,25,3000*D8F9\n");
	/* rounds to 0: no "-0.00000" */
	d.lat_e7 = -49;
	ukhas_sentence(buf, sizeof(buf), "NZ0P", &d);
	CHECK(strstr(buf, ",0.00000,") != NULL);
	/* rounds half away from zero */
	d.lat_e7 = -150;
	ukhas_sentence(buf, sizeof(buf), "NZ0P", &d);
	CHECK(strstr(buf, ",-0.00002,") != NULL);
}

static void extremes_fit(void) {
	char buf[UKHAS_MAX];
	ukhas_data_t d = {65535, 23, 59, 59, -900000000, -1800000000, INT32_MIN, 255, -32768, 65535};
	uint8_t n = ukhas_sentence(buf, sizeof(buf), "LONGCALL9", &d);
	CHECK(n > 0);
	CHECK(strstr(buf, ",-90.00000,-180.00000,-2147483648,255,-32768,65535*") != NULL);
	CHECK(n < UKHAS_MAX);
}

static void too_small_buffer(void) {
	char buf[20] = "untouched";
	ukhas_data_t d = {12, 1, 23, 45, 390997000, -945786000, 250, 7, 30, 4790};
	CHECK_EQ(ukhas_sentence(buf, sizeof(buf), "NZ0P", &d), 0);
	CHECK_STR(buf, "untouched");
	/* exactly enough: the sentence and its NUL */
	char exact[58];
	CHECK_EQ(ukhas_sentence(exact, sizeof(exact), "NZ0P", &d), 57);
	CHECK_EQ(ukhas_sentence(exact, 57, "NZ0P", &d), 0);
}

TEST_MAIN("ukhas", TEST(typical_fix), TEST(no_fix_zeros), TEST(southern_eastern_negative_alt),
		TEST(small_degrees_keep_sign), TEST(extremes_fit), TEST(too_small_buffer))
