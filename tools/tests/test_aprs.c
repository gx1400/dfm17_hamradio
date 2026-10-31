/*
 * test_aprs.c - APRS packet building and AX.25/HDLC encoding (aprs.c)
 */
#include <math.h>
#include "test.h"
#include "fake_hal.h"
#include "aprs.h"
#include "analog.h"
#include "fake_settings.h"

/* ---- fakes for what aprs.c uses ------------------------------------------ */

volatile uint16_t aprs_bit, aprs_tick, aprs_baud_tick;
volatile uint8_t ppsLockStatus;
static analog_reading fake_analog = { .vin_mv = 4672, .iin_ma = 63, .temp_c = 30,
		.usb_mv = 5020, .on_usb = 1 };

void analog_read(analog_reading *r) { *r = fake_analog; }
/* the radio and timer calls of a transmission, in order, one letter each:
 * S start_tx, P stop_tx, T/t tick timer on/off, D/d deviation 0/restored,
 * F/f fast SPI on/off, O offset 0, G/g green LED on/off */
static char calls[32];
static int ncalls, toggles, fast_offsets, fast_fail;
static int16_t offset_min, offset_max;
static uint32_t deviation;
static void call(char c) {
	if (ncalls < (int)sizeof(calls) - 1)
		calls[ncalls++] = c;
}
void si4060_setup(uint8_t mod_type) { (void)mod_type; }
/* the transmit gate (si4063.c): refuse = 1 stands for a refused key-up */
volatile uint32_t si4060_tx_refused;
static int refuse;
void si4060_start_tx(uint8_t channel) {
	(void)channel;
	call('S');
	if (refuse)
		si4060_tx_refused++;
}
void si4060_stop_tx(void) {
	call('P');
}
void si4060_set_deviation(uint32_t dev_hz) {
	deviation = dev_hz;
	call(dev_hz ? 'd' : 'D');
}
void si4060_set_offset(uint16_t offset) {
	CHECK_EQ(offset, 0);
	call('O');
}
int32_t si4060_offset_units(int32_t hz) { return hz; }
void si4060_spi_fast(uint8_t on) {
	call(on ? 'F' : 'f');
}
uint8_t si4060_set_offset_fast(int16_t offset) {
	fast_offsets++;
	if (offset < offset_min)
		offset_min = offset;
	if (offset > offset_max)
		offset_max = offset;
	return !fast_fail;
}
void startAprsTickTimer(void) {
	call('T');
}
void stopAprsTickTimer(void) {
	call('t');
}
void toggleSiGPIO3(void) {
	toggles++;
}
void deassertSiGPIO3(void) {}
void ledOnGreen(void) {
	call('G');
}
void ledOffGreen(void) {
	call('g');
}

/* aprs.c internals without a prototype in aprs.h */
extern char aprs_buf[];
extern uint8_t aprs_len;
extern unsigned char aprs_header[];
extern volatile uint16_t fcs;
extern volatile uint8_t finished;
extern volatile uint8_t aprs_afsk_sine;
extern volatile uint32_t aprs_afsk_skipped;
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
	CHECK_EQ((aprs_header[13] >> 1) & 0x0F, 12);
	/* the last address (WIDE1-1) ends the address field */
	CHECK_EQ(aprs_header[20] & 1, 1);
	CHECK_EQ(aprs_header[13] & 1, 0);
}

/* callsign, SSID, path and symbol all come from the settings */
static void station_from_settings(void) {
	GNSS_StateHandle g = fix_3d();
	char call[7] = {0};

	provision_as(&cfg, "KE0ABC");
	cfg.ssid = 9;
	cfg.path = 0x22;
	cfg.symbol_table = '\\';
	cfg.symbol_code = '>';
	settings_seal(&cfg);
	aprs_set_station();
	for (int i = 0; i < 6; i++)
		call[i] = aprs_header[7 + i] >> 1;
	CHECK_STR(call, "KE0ABC");
	CHECK_EQ((aprs_header[13] >> 1) & 0x0F, 9);
	CHECK_EQ(aprs_header[18] >> 1, '2'); /* WIDE2-2 */
	CHECK_EQ((aprs_header[20] >> 1) & 0x0F, 2);
	CHECK_EQ(aprs_header[20] & 1, 1); /* still ends the address field */

	ppsLockStatus = 1;
	aprs_prepare_buffer(&g, 0);
	CHECK_EQ(aprs_buf[APRS_SYMTAB_POS], '\\');
	CHECK_EQ(aprs_buf[APRS_SYMBOL_POS], '>');
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

/* ---- transmitting: aprs_tx_start / poll / stop, as tx_aprs() runs them ---- */

/* sends the prepared packet, standing in for the TIM15 ISR: a sample tick
 * per poll, a baud tick every APRS_MARK_TICKS (the exact rate doesn't matter
 * here), and aprs_afsk_tick() for the sine samples. returns the ticks. */
static int transmit(void) {
	int ticks = 0;
	ncalls = toggles = fast_offsets = 0;
	offset_min = offset_max = 0;
	aprs_tx_start();
	do {
		aprs_tick = 1;
		aprs_baud_tick = ticks % APRS_MARK_TICKS == 0;
		aprs_afsk_tick();
		ticks++;
	} while (aprs_tx_poll() && ticks < 200000);
	aprs_tx_stop();
	calls[ncalls] = 0;
	return ticks;
}

/* square wave: GPIO3 toggles at every sample tick, the radio is keyed once
 * and released, and the deviation and SPI are left alone */
static void tx_square_sequence(void) {
	GNSS_StateHandle g = fix_3d();
	aprs_afsk_sine = 0;
	aprs_prepare_buffer(&g, 0);
	int ticks = transmit();
	CHECK(ticks < 200000); /* finished */
	CHECK_STR(calls, "GTSPtg");
	CHECK_EQ(toggles, ticks);
	CHECK_EQ(fast_offsets, 0);
}

/* sine AFSK: deviation 0 while the TIM15 tick writes the tone as frequency
 * offsets over fast SPI, then everything is put back */
static void tx_sine_sequence(void) {
	GNSS_StateHandle g = fix_3d();
	aprs_afsk_sine = 1;
	fast_fail = 0;
	aprs_prepare_buffer(&g, 0);
	int ticks = transmit();
	CHECK(ticks < 200000);
	CHECK_STR(calls, "DGTSFfOPtdg");
	CHECK_EQ(deviation, cfg.dev_hz);
	CHECK_EQ(toggles, 0);
	/* one sample every APRS_AFSK_TICKS_PER_SAMPLE ticks, within the space
	 * tone's amplitude (offset units are Hz in this fake) */
	CHECK(fast_offsets >= ticks / APRS_AFSK_TICKS_PER_SAMPLE - 1);
	CHECK(fast_offsets <= ticks / APRS_AFSK_TICKS_PER_SAMPLE + 1);
	CHECK(offset_max > APRS_MARK_DEV_HZ * 9 / 10 && offset_max <= APRS_SPACE_DEV_HZ);
	CHECK(offset_min < -APRS_MARK_DEV_HZ * 9 / 10 && offset_min >= -APRS_SPACE_DEV_HZ);
	CHECK_EQ(aprs_afsk_skipped, 0);

	/* no sine samples outside a transmission */
	int before = fast_offsets;
	for (int i = 0; i < 10; i++)
		aprs_afsk_tick();
	CHECK_EQ(fast_offsets, before);
	aprs_afsk_sine = 0;
}

/* a sample the SPI couldn't take (radio busy) is counted, not retried */
static void tx_sine_counts_skipped_samples(void) {
	GNSS_StateHandle g = fix_3d();
	aprs_afsk_sine = 1;
	fast_fail = 1;
	aprs_prepare_buffer(&g, 0);
	transmit();
	CHECK(aprs_afsk_skipped > 0);
	CHECK_EQ(aprs_afsk_skipped, (uint32_t)fast_offsets);
	fast_fail = 0;
	aprs_afsk_sine = 0;
}

/* aprs_packets_sent counts packets that were keyed, not ones the gate refused */
static void packets_sent_counted(void) {
	GNSS_StateHandle g = fix_3d();
	uint32_t sent = aprs_packets_sent;
	aprs_prepare_buffer(&g, 0);
	transmit();
	CHECK_EQ(aprs_packets_sent, sent + 1);
	refuse = 1;
	transmit();
	refuse = 0;
	CHECK_EQ(aprs_packets_sent, sent + 1);
	transmit();
	CHECK_EQ(aprs_packets_sent, sent + 2);
}

/* #51: one request, one packet; several go APRS_SEND_NOW_GAP_MS apart */
static void send_now_paced(void) {
	uint32_t t = 100000;
	aprs_send_now = 0;
	CHECK_EQ(aprs_send_now_due(t), 0);
	aprs_send_now = 1;
	CHECK_EQ(aprs_send_now_due(t), 1);
	CHECK_EQ(aprs_send_now, 0);
	CHECK_EQ(aprs_send_now_due(t + 5000), 0);

	aprs_send_now = 3;
	t += 10000;
	CHECK_EQ(aprs_send_now_due(t), 1);
	CHECK_EQ(aprs_send_now_due(t + APRS_SEND_NOW_GAP_MS - 1), 0);
	CHECK_EQ(aprs_send_now_due(t + APRS_SEND_NOW_GAP_MS), 1);
	CHECK_EQ(aprs_send_now_due(t + 2 * APRS_SEND_NOW_GAP_MS), 1);
	CHECK_EQ(aprs_send_now, 0);
	CHECK_EQ(aprs_send_now_due(t + 10 * APRS_SEND_NOW_GAP_MS), 0);
}

/* every test runs on a board set up as NZ0P (fake_settings.h) */
void test_setup(void) {
	provision();
	aprs_set_station();
}

TEST_MAIN("aprs", TEST(crc_reference_vector), TEST(base91_telemetry_values),
		TEST(position_packet_fields), TEST(usb_channel_zero_on_battery), TEST(telemetry_sequence),
		TEST(low_altitude_clamped), TEST(no_pps_sends_status), TEST(no_fix_status_shows_gps_state),
		TEST(status_negative_temperature), TEST(fix_2d_is_a_position),
		TEST(prepare_fix_trusts_caller), TEST(fcs_matches_frame), TEST(header_addresses),
		TEST(station_from_settings), TEST(line_bits_position), TEST(line_bits_status),
		TEST(tx_square_sequence), TEST(tx_sine_sequence), TEST(tx_sine_counts_skipped_samples),
		TEST(packets_sent_counted), TEST(send_now_paced))
