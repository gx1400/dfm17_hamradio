/*
 * test_cw.c - Morse as a keyed tone: the code table, timing, word gaps and
 * text with punctuation, decoded back from the tone timeline (cw.c)
 */
#include "test.h"
#include "fake_hal.h"
#include "cw.h"
#include "tim.h"

/* ---- fakes ------------------------------------------------------------------ */

volatile uint16_t tone_half_ticks;
static int deasserts;

void deassertSiGPIO3(void) {
	deasserts++;
}

/* the tone on/off timeline seen at each HAL_Delay(): runs of (tone, ms) */
typedef struct {
	int tone;
	uint32_t ms;
} run_t;
static run_t runs[1024];
static int nruns;

static void record(uint32_t ms) {
	int tone = tone_half_ticks != 0;
	if (nruns && runs[nruns - 1].tone == tone)
		runs[nruns - 1].ms += ms;
	else if (nruns < 1024)
		runs[nruns++] = (run_t){tone, ms};
}

static void reset(void) {
	nruns = 0;
	deasserts = 0;
	tone_half_ticks = 0;
	fake_tick = 0;
	fake_delay_hook = record;
}

/* the timeline back into text: element >= 2 dits is a dash, a gap >= 2 dits
 * ends a letter, >= 5 dits a word */
static void decode(char *out, size_t max, uint32_t dit) {
	static const char *const table[] = {
			"A.-",
			"B-...",
			"C-.-.",
			"D-..",
			"E.",
			"F..-.",
			"G--.",
			"H....",
			"I..",
			"J.---",
			"K-.-",
			"L.-..",
			"M--",
			"N-.",
			"O---",
			"P.--.",
			"Q--.-",
			"R.-.",
			"S...",
			"T-",
			"U..-",
			"V...-",
			"W.--",
			"X-..-",
			"Y-.--",
			"Z--..",
			"0-----",
			"1.----",
			"2..---",
			"3...--",
			"4....-",
			"5.....",
			"6-....",
			"7--...",
			"8---..",
			"9----.",
			"/-..-.",
			"?..--..",
			"..-.-.-",
			",--..--",
			"=-...-",
			"--....-",
	};
	char code[8];
	size_t nc = 0, n = 0;

	for (int i = 0; i < nruns && n + 2 < max; i++) {
		if (runs[i].tone) {
			if (nc < sizeof(code) - 1)
				code[nc++] = runs[i].ms >= 2 * dit ? '-' : '.';
			continue;
		}
		if (runs[i].ms < 2 * dit && i != nruns - 1)
			continue;
		code[nc] = 0;
		for (size_t k = 0; nc && k < sizeof(table) / sizeof(table[0]); k++)
			if (strcmp(table[k] + 1, code) == 0)
				out[n++] = table[k][0];
		nc = 0;
		if (runs[i].ms >= 5 * dit && i != nruns - 1)
			out[n++] = ' ';
	}
	out[n] = 0;
}

/* ---- tests -------------------------------------------------------------------- */

static void table_covers_letters_digits(void) {
	for (char c = 'A'; c <= 'Z'; c++)
		CHECK(cw_morse(c) != NULL);
	for (char c = '0'; c <= '9'; c++)
		CHECK(cw_morse(c) != NULL);
	CHECK_STR(cw_morse('a'), cw_morse('A'));
	CHECK_STR(cw_morse('z'), "--..");
	CHECK_STR(cw_morse('/'), "-..-.");
	CHECK_STR(cw_morse('?'), "..--..");
	CHECK(cw_morse('#') == NULL);
	CHECK(cw_morse(' ') == NULL);
}

static void text_decodes(void) {
	char text[64];
	reset();
	cw_send("VVV test DE NZ0P/1?", 15);
	decode(text, sizeof(text), 80);
	CHECK_STR(text, "VVV TEST DE NZ0P/1?");
}

static void unknown_characters_skipped(void) {
	char text[32];
	reset();
	cw_send("A#B", 20);
	decode(text, sizeof(text), 60);
	CHECK_STR(text, "AB");
}

static void timing_follows_wpm(void) {
	const uint32_t dit = 1200 / 20;
	reset();
	cw_send("PARIS ", 20);
	/* PARIS plus a word gap is 50 dits by definition */
	CHECK_EQ(fake_tick, 50 * dit);
	for (int i = 0; i < nruns; i++)
		if (runs[i].tone)
			CHECK(runs[i].ms == dit || runs[i].ms == 3 * dit);
}

static void tone_off_parks_gpio(void) {
	reset();
	cw_tone(1);
	CHECK_EQ(tone_half_ticks, CW_TONE_HALF_TICKS);
	cw_tone(0);
	CHECK_EQ(tone_half_ticks, 0);
	CHECK_EQ(deasserts, 1);
}

static void zero_wpm_does_not_divide_by_zero(void) {
	reset();
	cw_send("E", 0);
	CHECK_EQ(fake_tick, 1200 * 4); /* dit, gap, letter gap at 1 WPM */
}

TEST_MAIN("cw", TEST(table_covers_letters_digits), TEST(text_decodes),
		TEST(unknown_characters_skipped), TEST(timing_follows_wpm), TEST(tone_off_parks_gpio),
		TEST(zero_wpm_does_not_divide_by_zero))
