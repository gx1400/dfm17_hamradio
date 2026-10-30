/*
 * test_ppscap: the TIM4 input-capture PPS timestamps (ppscap.c), against a
 * fake TIM4 (fakehal). Issue #12.
 */
#include "test.h"
#include "fake_hal.h"
#include "ppscap.h"
#include "ppsdisc.h"

/* a capture at 16-bit counter value v, as the hardware would latch it */
static void edge(uint16_t v) {
	fake_tim4.CCR3 = v;
	fake_tim4.SR |= TIM_SR_CC3IF;
}

static uint8_t read(uint32_t *c) {
	uint8_t r = pps_capture_read(c);
	fake_tim4.SR &= ~TIM_SR_CC3IF; /* reading CCR3 clears it */
	return r;
}

static void init_sets_up_tim4(void) {
	fake_rcc.APB1ENR = 0;
	pps_capture_init();
	CHECK(fake_rcc.APB1ENR & RCC_APB1ENR_TIM4EN);
	CHECK_EQ(fake_tim4.PSC, 0);
	CHECK_EQ(fake_tim4.ARR, 0xFFFF);
	CHECK_EQ(fake_tim4.CCMR2 & 0x03, TIM_CCMR2_CC3S_0); /* CC3 = input, TI3 */
	CHECK(fake_tim4.CCER & TIM_CCER_CC3E);
	CHECK(!(fake_tim4.CCER & TIM_CCER_CC3P)); /* rising edge */
	CHECK(fake_tim4.CR1 & TIM_CR1_CEN);
	/* only CH3: PB6/PB7 (CH1/CH2) stay the I2C bus */
	CHECK_EQ(fake_tim4.CCER & ~(TIM_CCER_CC3E | TIM_CCER_CC3P), 0);
}

static void interval_arithmetic(void) {
	/* 24 000 000 = 366 * 65536 + 13824 */
	CHECK_EQ(pps_capture_interval(0, 13824), 24000000u);
	CHECK_EQ(pps_capture_interval(1000, 1000 + 13824), 24000000u);
	/* the 16-bit counter wraps between the two edges */
	CHECK_EQ(pps_capture_interval(60000, (uint16_t)(60000 + 13824)), 24000000u);
	/* slow and fast TCXO, a few ppm either way */
	CHECK_EQ(pps_capture_interval(5, (uint16_t)(5 + 13824 - 30)), 23999970u);
	CHECK_EQ(pps_capture_interval(5, (uint16_t)(5 + 13824 + 240)), 24000240u);
	CHECK_EQ(pps_capture_interval(65535, (uint16_t)(65535 + 13824 - 240)), 23999760u);
}

static void reads_intervals(void) {
	uint32_t c = 0;
	pps_capture_init();
	edge(40000);
	CHECK_EQ(read(&c), 0); /* first edge: nothing to compare */
	edge((uint16_t)(40000 + 13824 - 25));
	CHECK_EQ(read(&c), 1);
	CHECK_EQ(c, 23999975u);
	edge((uint16_t)(40000 + 2 * 13824 - 50));
	CHECK_EQ(read(&c), 1);
	CHECK_EQ(c, 23999975u);
}

static void missed_and_overcaptured_edges(void) {
	uint32_t c = 0, missing = pps_cap_missing, over = pps_cap_overcaptures;
	pps_capture_init();
	edge(100);
	read(&c);

	/* the interrupt found no capture (GPS asleep, glitch): start over */
	CHECK_EQ(read(&c), 0);
	CHECK_EQ(pps_cap_missing, missing + 1);
	edge(200);
	CHECK_EQ(read(&c), 0); /* no previous capture to use */
	edge((uint16_t)(200 + 13824));
	CHECK_EQ(read(&c), 1);

	/* two edges before a read: CC3OF, interval unknown, flag cleared */
	edge(1000);
	fake_tim4.SR |= TIM_SR_CC3OF;
	CHECK_EQ(read(&c), 0);
	CHECK_EQ(pps_cap_overcaptures, over + 1);
	CHECK(!(fake_tim4.SR & TIM_SR_CC3OF));
	edge((uint16_t)(1000 + 13824));
	CHECK_EQ(read(&c), 1); /* and it recovers on the next edge */
	CHECK_EQ(c, 24000000u);
}

static void log_wraps(void) {
	uint32_t head = pps_log_head;
	for (uint32_t i = 0; i < PPS_LOG + 3; i++)
		pps_log_add(i, 100 + i);
	CHECK_EQ(pps_log_head, head + PPS_LOG + 3);
	uint32_t last = (pps_log_head - 1) % PPS_LOG;
	CHECK_EQ(pps_log[last].dwt, PPS_LOG + 2);
	CHECK_EQ(pps_log[last].capture, 100 + PPS_LOG + 2);
}

TEST_MAIN("ppscap", TEST(init_sets_up_tim4), TEST(interval_arithmetic), TEST(reads_intervals),
		TEST(missed_and_overcaptured_edges), TEST(log_wraps), )
