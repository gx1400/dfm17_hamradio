/*
 * test_aprs.c - APRS packet building and AX.25/HDLC encoding (aprs.c)
 */
#include <math.h>
#include "test.h"
#include "fake_hal.h"
#include "aprs.h"
#include "analog.h"

/* ---- fakes for what aprs.c uses ------------------------------------------ */

volatile uint16_t aprs_bit, aprs_tick, aprs_baud_tick;
volatile uint8_t ppsLockStatus;
static analog_reading fake_analog = { .vin_mv = 4672, .iin_ma = 63, .temp_c = 30,
		.usb_mv = 5020, .on_usb = 1 };

void analog_read(analog_reading *r) { *r = fake_analog; }
void si4060_setup(uint8_t mod_type) { (void)mod_type; }
void si4060_start_tx(uint8_t channel) { (void)channel; }
void si4060_stop_tx(void) {}
void si4060_set_deviation(uint32_t dev_hz) { (void)dev_hz; }
void si4060_set_offset(uint16_t offset) { (void)offset; }
int32_t si4060_offset_units(int32_t hz) { return hz; }
void si4060_spi_fast(uint8_t on) { (void)on; }
uint8_t si4060_set_offset_fast(int16_t offset) { (void)offset; return 1; }
void startAprsTickTimer(void) {}
void stopAprsTickTimer(void) {}
void toggleSiGPIO3(void) {}
void deassertSiGPIO3(void) {}
void ledOnGreen(void) {}
void ledOffGreen(void) {}

/* aprs.c internals without a prototype in aprs.h */
extern char aprs_buf[];
extern uint8_t aprs_len;
extern const unsigned char aprs_header[];
extern volatile uint16_t fcs;
extern volatile uint8_t finished;
void aprs_init(void);
uint8_t get_next_bit(void);
void base91_encode_tlm(char *buf, uint16_t value);

/* ---- helpers --------------------------------------------------------------- */

/* CRC-16/X.25, the AX.25 FCS, written independently of aprs.c */
static uint16_t crc_x25(const uint8_t *p, size_t n) {
	uint16_t crc = 0xFFFF;
	for (size_t i = 0; i < n; i++)
		for (int b = 0; b < 8; b++)
			crc = ((crc ^ (p[i] >> b)) & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
	return crc ^ 0xFFFF;
}

static uint32_t base91_4(const char *p) {
	return (uint32_t)(p[0] - 33) * 91 * 91 * 91 + (uint32_t)(p[1] - 33) * 91 * 91
			+ (uint32_t)(p[2] - 33) * 91 + (uint32_t)(p[3] - 33);
}

static uint16_t base91_2(const char *p) {
	return (uint16_t)((p[0] - 33) * 91 + (p[1] - 33));
}

static GNSS_StateHandle fix_3d(void) {
	GNSS_StateHandle g = {0};
	g.fixType = Fix3D;
	g.numSV = 9;
	g.lat = 389987994;			/* 38.9987994 N */
	g.lon = -946819494;			/* 94.6819494 W */
	g.hMSL = 309000;			/* 309 m */
	g.day = 29;
	g.hour = 8;
	g.min = 54;
	return g;
}

static void status_text(char *out) {
	memcpy(out, aprs_buf, aprs_len);
	out[aprs_len] = 0;
}

/*
 * decodes the line bits from get_next_bit() like a receiver: NRZI, then the
 * first frame between flags, bit-unstuffed, LSB first. returns the length.
 */
static size_t decode_frame(uint8_t *out, size_t max) {
	static uint8_t bits[8192];
	size_t n = 0;
	uint8_t prev = 0;

	aprs_init();
	while (!finished && n < sizeof(bits)) {
		uint8_t line = get_next_bit();
		bits[n++] = line == prev;		/* NRZI: no change = 1 */
		prev = line;
	}

	size_t i = 0, ones = 0, len = 0, nb = 0;
	uint8_t byte = 0;
	/* skip to the end of the opening flags */
	while (i + 8 <= n) {
		int flag = !bits[i] && bits[i + 1] && bits[i + 2] && bits[i + 3] && bits[i + 4]
				&& bits[i + 5] && bits[i + 6] && !bits[i + 7];
		if (!flag) {
			if (len == 0 && nb == 0 && i > 0)
				break;
			i++;
			continue;
		}
		i += 8;
	}
	for (; i < n; i++) {
		if (ones == 5) {			/* stuffed zero, or a flag */
			if (bits[i])
				break;
			ones = 0;
			continue;
		}
		ones = bits[i] ? ones + 1 : 0;
		byte |= bits[i] << nb;
		if (++nb == 8) {
			if (len < max)
				out[len++] = byte;
			byte = 0;
			nb = 0;
		}
	}
	/* the closing flag's first 6 bits never complete a byte */
	return len;
}

/* ---- tests ----------------------------------------------------------------- */

static void crc_reference_vector(void) {
	/* the standard check value of CRC-16/X.25 */
	CHECK_EQ(crc_x25((const uint8_t *)"123456789", 9), 0x906E);
}

static void base91_telemetry_values(void) {
	char b[2];
	base91_encode_tlm(b, 0);
	CHECK(b[0] == '!' && b[1] == '!');
	base91_encode_tlm(b, 90);
	CHECK(b[0] == '!' && b[1] == '{');
	base91_encode_tlm(b, 91);
	CHECK(b[0] == '"' && b[1] == '!');
	base91_encode_tlm(b, 8280);
	CHECK(b[0] == '{' && b[1] == '{');
}

static void position_packet_fields(void) {
	GNSS_StateHandle g = fix_3d();
	ppsLockStatus = 1;

	CHECK_EQ(aprs_prepare_buffer(&g, 0), 1);
	CHECK_EQ(aprs_len, APRS_POS_LEN);
	CHECK(memcmp(aprs_buf, "/290854z/", 9) == 0);
	CHECK_NEAR(90.0 - base91_4(&aprs_buf[APRS_LAT_START]) / 380926.0, 38.9987994, 1e-4);
	CHECK_NEAR(base91_4(&aprs_buf[APRS_LON_START]) / 190463.0 - 180.0, -94.6819494, 1e-4);
	CHECK_NEAR(pow(1.002, base91_2(&aprs_buf[APRS_ALT_START])) / 3.28084, 309.0, 1.0);
	CHECK_EQ(aprs_buf[APRS_ALT_START + 2], '1');	/* compression type */
	CHECK_EQ(aprs_buf[APRS_SEQ_START - 1], '|');
	CHECK_EQ(aprs_buf[APRS_POS_LEN - 1], '|');
	CHECK_EQ(base91_2(&aprs_buf[APRS_TEMP_START]), 30 + APRS_TLM_TEMP_OFFSET);
	CHECK_EQ(base91_2(&aprs_buf[APRS_VOLT_START]), 4672);
	CHECK_EQ(base91_2(&aprs_buf[APRS_VSOL_START]), 63);
	CHECK_EQ(base91_2(&aprs_buf[APRS_USB_START]), 5020);
}

static void usb_channel_zero_on_battery(void) {
	GNSS_StateHandle g = fix_3d();
	analog_reading saved = fake_analog;
	fake_analog.on_usb = 0;
	fake_analog.usb_mv = 120;		/* leakage, below the presence threshold */
	ppsLockStatus = 1;
	aprs_prepare_buffer(&g, 0);
	CHECK_EQ(base91_2(&aprs_buf[APRS_USB_START]), 0);
	fake_analog = saved;
}

static void telemetry_sequence(void) {
	GNSS_StateHandle g = fix_3d();
	ppsLockStatus = 1;

	aprs_prepare_buffer(&g, 0);
	uint16_t s1 = base91_2(&aprs_buf[APRS_SEQ_START]);
	aprs_prepare_buffer(&g, 0);
	CHECK_EQ(base91_2(&aprs_buf[APRS_SEQ_START]), (s1 + 1) % 8281);
	/* a backlog fix carries sequence 0 and doesn't advance the counter */
	aprs_prepare_buffer(&g, 1);
	CHECK_EQ(base91_2(&aprs_buf[APRS_SEQ_START]), 0);
	aprs_prepare_buffer(&g, 0);
	CHECK_EQ(base91_2(&aprs_buf[APRS_SEQ_START]), (s1 + 2) % 8281);
}

static void low_altitude_clamped(void) {
	GNSS_StateHandle g = fix_3d();
	g.hMSL = -50000;			/* below sea level */
	ppsLockStatus = 1;
	aprs_prepare_buffer(&g, 0);
	CHECK_EQ(base91_2(&aprs_buf[APRS_ALT_START]), 0);	/* 1 ft */
}

static void no_pps_sends_status(void) {
	GNSS_StateHandle g = fix_3d();
	char s[APRS_BUF_SIZE + 1];
	ppsLockStatus = 0;

	CHECK_EQ(aprs_prepare_buffer(&g, 0), 0);
	status_text(s);
	CHECK_STR(s, ">nofix nopps 4672mV 63mA 30C USB");
}

static void no_fix_status_shows_gps_state(void) {
	GNSS_StateHandle g = fix_3d();
	char s[APRS_BUF_SIZE + 1];
	ppsLockStatus = 1;
	g.fixType = DeadReckoning;
	g.numSV = 3;

	CHECK_EQ(aprs_prepare_buffer(&g, 0), 0);
	status_text(s);
	CHECK_STR(s, ">nofix 1D 3sv 4672mV 63mA 30C USB");
	/* a TH-D72 shows 20 characters: the GPS state must be among them */
	CHECK(memcmp(s + 1, "nofix 1D 3sv", 12) == 0);
}

static void status_negative_temperature(void) {
	GNSS_StateHandle g = {0};
	char s[APRS_BUF_SIZE + 1];
	analog_reading saved = fake_analog;
	fake_analog.temp_c = -12;
	fake_analog.iin_ma = 0;
	fake_analog.on_usb = 0;
	ppsLockStatus = 0;

	aprs_prepare_buffer(&g, 0);
	status_text(s);
	CHECK_STR(s, ">nofix nopps 4672mV 0mA -12C BAT");
	fake_analog = saved;
}

static void fix_2d_is_a_position(void) {
	GNSS_StateHandle g = fix_3d();
	g.fixType = Fix2D;
	ppsLockStatus = 1;
	CHECK_EQ(aprs_prepare_buffer(&g, 0), 1);
}

static void prepare_fix_trusts_caller(void) {
	GNSS_StateHandle g = fix_3d();
	ppsLockStatus = 0;			/* the GPS may be asleep by now */
	CHECK_EQ(aprs_prepare_fix(&g, 1, 0), 1);
	CHECK_EQ(aprs_len, APRS_POS_LEN);
	CHECK_EQ(aprs_prepare_fix(&g, 0, 0), 0);
	CHECK_EQ(aprs_buf[0], '>');
}

static void fcs_matches_frame(void) {
	GNSS_StateHandle g = fix_3d();
	uint8_t frame[APRS_HEADER_LEN + APRS_BUF_SIZE];
	ppsLockStatus = 1;

	aprs_prepare_buffer(&g, 0);
	memcpy(frame, aprs_header, APRS_HEADER_LEN);
	memcpy(frame + APRS_HEADER_LEN, aprs_buf, aprs_len);
	CHECK_EQ(fcs, crc_x25(frame, APRS_HEADER_LEN + aprs_len));
}

static void header_addresses(void) {
	char call[7];
	for (int i = 0; i < 6; i++)
		call[i] = aprs_header[i] >> 1;
	call[6] = 0;
	CHECK_STR(call, "APZDFM");
	for (int i = 0; i < 6; i++)
		call[i] = aprs_header[7 + i] >> 1;
	CHECK_STR(call, "NZ0P  ");
	CHECK_EQ((aprs_header[13] >> 1) & 0x0F, SRC_SSID);
	/* the last address (WIDE1-1) ends the address field */
	CHECK_EQ(aprs_header[20] & 1, 1);
	CHECK_EQ(aprs_header[13] & 1, 0);
}

/* the full line coding: NRZI, flags, bit stuffing, FCS low byte first */
static void line_bits_decode_to_frame(int position) {
	GNSS_StateHandle g = fix_3d();
	uint8_t want[APRS_HEADER_LEN + APRS_BUF_SIZE + 2], got[256];
	ppsLockStatus = position;

	aprs_prepare_buffer(&g, 0);
	size_t n = APRS_HEADER_LEN + aprs_len;
	memcpy(want, aprs_header, APRS_HEADER_LEN);
	memcpy(want + APRS_HEADER_LEN, aprs_buf, aprs_len);
	want[n] = fcs & 0xFF;
	want[n + 1] = fcs >> 8;

	size_t len = decode_frame(got, sizeof(got));
	CHECK_EQ(len, n + 2);
	CHECK_MEM(got, want, n + 2);
	CHECK_EQ(crc_x25(got, n), got[n] | (got[n + 1] << 8));
}

static void line_bits_position(void) { line_bits_decode_to_frame(1); }
static void line_bits_status(void) { line_bits_decode_to_frame(0); }

TEST_MAIN("aprs",
	TEST(crc_reference_vector),
	TEST(base91_telemetry_values),
	TEST(position_packet_fields),
	TEST(usb_channel_zero_on_battery),
	TEST(telemetry_sequence),
	TEST(low_altitude_clamped),
	TEST(no_pps_sends_status),
	TEST(no_fix_status_shows_gps_state),
	TEST(status_negative_temperature),
	TEST(fix_2d_is_a_position),
	TEST(prepare_fix_trusts_caller),
	TEST(fcs_matches_frame),
	TEST(header_addresses),
	TEST(line_bits_position),
	TEST(line_bits_status))
