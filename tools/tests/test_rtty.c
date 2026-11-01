/*
 * test_rtty.c - RTTY framing and ITA2/ASCII encoding (rtty.c): the bit stream
 * handed to rtty_hw_bits() is decoded back to text the way a receiver does
 */
#include "test.h"
#include "rtty.h"

/* ---- fakes: the half-bit stream ------------------------------------------- */

static uint8_t stream[40000]; /* one entry per half bit, 1 = mark */
static int nstream, begins, ends;
static rtty_params_t began;

void rtty_hw_begin(const rtty_params_t *p) {
	begins++;
	began = *p;
}

void rtty_hw_bits(uint8_t mark, uint16_t halves) {
	for (uint16_t i = 0; i < halves && nstream < (int)sizeof(stream); i++)
		stream[nstream++] = mark;
}

void rtty_hw_end(void) {
	ends++;
}

static void reset(void) {
	nstream = begins = ends = 0;
}

static const rtty_params_t ham = {4545, 170, 5, 3, 0, 0};	 /* ITA2 45.45/170 1.5 stop */
static const rtty_params_t ukhas7 = {5000, 425, 7, 4, 0, 0}; /* ASCII 50/425 7N2 */
static const rtty_params_t ukhas8 = {5000, 425, 8, 4, 0, 0}; /* ASCII 50/425 8N2 */

/* a receiver: idle on mark, a space half bit starts a character; each bit is
 * read in its first half. Counts characters whose stop bit isn't mark for
 * the full stop length. Returns the codes. */
static int frames(uint8_t *codes, int max, uint8_t bits, uint8_t stop_halves, int *bad_stops) {
	int n = 0, i = 0;
	*bad_stops = 0;
	while (i < nstream && n < max) {
		if (stream[i]) {
			i++;
			continue;
		}
		uint8_t code = 0;
		for (uint8_t b = 0; b < bits; b++)
			code |= (uint8_t)(stream[i + 2 + 2 * b] << b);
		int stop = i + 2 + 2 * bits;
		for (int k = 0; k < stop_halves; k++)
			if (stop + k >= nstream || !stream[stop + k])
				(*bad_stops)++;
		codes[n++] = code;
		i = stop + stop_halves;
	}
	return n;
}

/* ITA2 back to text, unshifting on space like fldigi */
static void ita2_text(char *out, size_t max, const uint8_t *codes, int n) {
	static const char ltrs[] = "\0E\nA SIU\rDRJNFCKTZLWHYPQOBG\0MXV\0";
	static const char figs[] = "\0"
							   "3\n- \a87\r$4',!:(5\")2#6019?&\0./;\0";
	int f = 0;
	size_t k = 0;
	for (int i = 0; i < n && k + 1 < max; i++) {
		if (codes[i] == RTTY_ITA2_LTRS)
			f = 0;
		else if (codes[i] == RTTY_ITA2_FIGS)
			f = 1;
		else {
			out[k++] = f ? figs[codes[i]] : ltrs[codes[i]];
			if (codes[i] == 4)
				f = 0;
		}
	}
	out[k] = 0;
}

/* ---- tests -------------------------------------------------------------------- */

static void params_checked(void) {
	rtty_params_t p = ham;
	CHECK(rtty_params_ok(&ham));
	CHECK(rtty_params_ok(&ukhas7));
	CHECK(rtty_params_ok(&ukhas8));
	p.baud_x100 = 999;
	CHECK(!rtty_params_ok(&p));
	p = ham;
	p.baud_x100 = 30001;
	CHECK(!rtty_params_ok(&p));
	p = ham;
	p.shift_hz = 49;
	CHECK(!rtty_params_ok(&p));
	p = ham;
	p.shift_hz = 1001;
	CHECK(!rtty_params_ok(&p));
	p = ham;
	p.bits = 6;
	CHECK(!rtty_params_ok(&p));
	p = ham;
	p.stop_halves = 1;
	CHECK(!rtty_params_ok(&p));
	p = ham;
	p.stop_halves = 5;
	CHECK(!rtty_params_ok(&p));
}

static void ita2_shifts(void) {
	uint8_t shift = RTTY_SHIFT_NONE, codes[2];
	CHECK_EQ(rtty_ita2('A', &shift, codes), 2);
	CHECK_EQ(codes[0], RTTY_ITA2_LTRS);
	CHECK_EQ(codes[1], 0x03);
	CHECK_EQ(rtty_ita2('e', &shift, codes), 1); /* lower case, already letters */
	CHECK_EQ(codes[0], 0x01);
	CHECK_EQ(rtty_ita2('5', &shift, codes), 2);
	CHECK_EQ(codes[0], RTTY_ITA2_FIGS);
	CHECK_EQ(codes[1], 0x10);
	CHECK_EQ(rtty_ita2('/', &shift, codes), 1);
	CHECK_EQ(codes[0], 0x1D);
	/* a space in figures: the receiver may have unshifted, so figures again */
	CHECK_EQ(rtty_ita2(' ', &shift, codes), 1);
	CHECK_EQ(codes[0], 0x04);
	CHECK_EQ(shift, RTTY_SHIFT_NONE);
	CHECK_EQ(rtty_ita2('1', &shift, codes), 2);
	CHECK_EQ(codes[0], RTTY_ITA2_FIGS);
	/* CR and LF keep the shift */
	CHECK_EQ(rtty_ita2('\r', &shift, codes), 1);
	CHECK_EQ(codes[0], 0x08);
	CHECK_EQ(shift, RTTY_SHIFT_FIGS);
	/* a space in letters keeps letters */
	shift = RTTY_SHIFT_LTRS;
	CHECK_EQ(rtty_ita2(' ', &shift, codes), 1);
	CHECK_EQ(shift, RTTY_SHIFT_LTRS);
	/* nothing to send */
	CHECK_EQ(rtty_ita2('#', &shift, codes), 0);
	CHECK_EQ(rtty_ita2('@', &shift, codes), 0);
	CHECK_EQ(rtty_ita2(0, &shift, codes), 0);
}

static void halves_rounded(void) {
	CHECK_EQ(rtty_halves(1000, 4545), 91); /* 90.9 */
	CHECK_EQ(rtty_halves(1000, 5000), 100);
	CHECK_EQ(rtty_halves(300, 4545), 27); /* 27.27 */
	CHECK_EQ(rtty_halves(0, 4545), 0);
}

/* the steps si4060_set_deviation() programs: it rounds down (si4063.c) */
static uint32_t radio_steps(uint32_t dev_hz) {
	return (uint32_t)(((uint64_t)dev_hz * 8 << 19) / (2 * 25600000ULL));
}

static void deviation_nearest_step(void) {
	/* on air, asking for shift / 2 sent 170 Hz as 146 Hz (6 steps) */
	CHECK_EQ(radio_steps(170 / 2), 6);
	CHECK_EQ(radio_steps(rtty_deviation_hz(170)), 7);  /* 170.9 Hz */
	CHECK_EQ(radio_steps(rtty_deviation_hz(425)), 17); /* 415.0 Hz; 18 would be 439.5 */
	CHECK_EQ(radio_steps(rtty_deviation_hz(450)), 18); /* 439.5 Hz */
	for (uint16_t shift = 50; shift <= 1000; shift++) {
		double step = 2.0 * 25600000 / (1 << 19) / 8;
		double sent = 2 * step * radio_steps(rtty_deviation_hz(shift));
		CHECK(sent - shift <= step && shift - sent <= step);
	}
}

static void ita2_round_trip(void) {
	const char *msg = "\r\nRYRYRY test 123 DE NZ0P/1\r\n";
	uint8_t codes[200];
	char text[200];
	int bad;
	reset();
	rtty_send(msg, &ham);
	CHECK_EQ(begins, 1);
	CHECK_EQ(ends, 1);
	CHECK_EQ(began.shift_hz, 170);
	/* the lead-in is mark */
	for (int i = 0; i < rtty_halves(RTTY_LEAD_MS, 4545); i++)
		CHECK(stream[i] == 1);
	int n = frames(codes, 200, 5, 3, &bad);
	CHECK_EQ(bad, 0);
	CHECK(n > 0);
	CHECK_EQ(codes[0], RTTY_ITA2_LTRS); /* sets the receiver's shift */
	ita2_text(text, sizeof(text), codes, n);
	CHECK_STR(text, "\r\nRYRYRY TEST 123 DE NZ0P/1\r\n");
	/* and ends on mark */
	for (int i = nstream - rtty_halves(RTTY_TAIL_MS, 4545); i < nstream; i++)
		CHECK(stream[i] == 1);
}

static void ita2_figures_after_space_resent(void) {
	uint8_t codes[64];
	int bad;
	reset();
	rtty_send("1 2", &ham);
	int n = frames(codes, 64, 5, 3, &bad);
	/* LTRS, FIGS 1, space, FIGS 2 */
	CHECK_EQ(n, 6);
	CHECK_EQ(codes[1], RTTY_ITA2_FIGS);
	CHECK_EQ(codes[3], 0x04);
	CHECK_EQ(codes[4], RTTY_ITA2_FIGS);
}

static void ascii_round_trip(void) {
	const char *msg = "$$NZ0P,1,12:00:00,39.1,-94.5*1A2B\n";
	uint8_t codes[200];
	int bad;
	reset();
	rtty_send(msg, &ukhas8);
	int n = frames(codes, 200, 8, 4, &bad);
	CHECK_EQ(bad, 0);
	CHECK_EQ(n, (int)strlen(msg));
	CHECK_MEM(codes, msg, strlen(msg));
}

static void ascii7_masks_high_bit(void) {
	uint8_t codes[8];
	int bad;
	reset();
	const char text[] = {(char)0xC1, 'B', 0}; /* 'A' with the high bit set */
	rtty_send(text, &ukhas7);
	int n = frames(codes, 8, 7, 4, &bad);
	CHECK_EQ(bad, 0);
	CHECK_EQ(n, 2);
	CHECK_EQ(codes[0], 'A');
	CHECK_EQ(codes[1], 'B');
}

static void character_length(void) {
	/* ITA2 with 1.5 stop bits is 7.5 bits a character: 15 half bits */
	reset();
	rtty_send("", &ham);
	int empty = nstream;
	reset();
	rtty_send("E", &ham);
	CHECK_EQ(nstream - empty, 15);
	/* ASCII 8N2: 11 bits */
	reset();
	rtty_send("", &ukhas8);
	empty = nstream;
	reset();
	rtty_send("E", &ukhas8);
	CHECK_EQ(nstream - empty, 22);
}

static void bad_params_send_nothing(void) {
	rtty_params_t p = ham;
	p.bits = 6;
	reset();
	rtty_send("TEST", &p);
	CHECK_EQ(begins, 0);
	CHECK_EQ(nstream, 0);
}

TEST_MAIN("rtty", TEST(params_checked), TEST(ita2_shifts), TEST(halves_rounded),
		TEST(deviation_nearest_step), TEST(ita2_round_trip), TEST(ita2_figures_after_space_resent),
		TEST(ascii_round_trip), TEST(ascii7_masks_high_bit), TEST(character_length),
		TEST(bad_params_send_nothing))
