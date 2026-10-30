/*
 * test_diag.c - button debouncing and classification, LED patterns, blink
 * codes and the PPS cycle counter (diag.c), driven 1 ms at a time like SysTick
 */
#include "test.h"
#include "fake_hal.h"
#include "diag.h"
#include "ppscap.h"

/* ---- fakes ------------------------------------------------------------------ */

volatile uint8_t ppsLockStatus;
static uint8_t time_locked = 1;
uint8_t gpsTimeLocked(void) {
	return time_locked;
}
static int button;
static int green, yellow, red;
static uint32_t sample_cycles, sample_ms;

void pps_disc_sample(uint32_t cycles, uint32_t ms) {
	sample_cycles = cycles;
	sample_ms = ms;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin) {
	(void)port; (void)pin;
	return button ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
void ledOnGreen(void) { green = 1; }
void ledOffGreen(void) { green = 0; }
void ledOnYellow(void) { yellow = 1; }
void ledOffYellow(void) { yellow = 0; }
void ledOnRed(void) { red = 1; }
void ledOffRed(void) { red = 0; }

/* SysTick: one diag_tick() per millisecond */
static void ms(uint32_t n) {
	while (n--) {
		fake_tick++;
		diag_tick();
	}
}

static void press(uint32_t held) {
	button = 1;
	ms(held);
	button = 0;
	ms(100);
}

/* ---- tests (in order: the first relies on the button state after boot) ---- */

static void held_at_boot_is_ignored(void) {
	/* the button was held to power the board on */
	button = 1;
	diag_selftest();
	ms(1500);
	button = 0;
	ms(100);
	CHECK_EQ(diag_get_button(), DIAG_BUTTON_NONE);
}

static void short_medium_long(void) {
	press(300);
	CHECK_EQ(diag_get_button(), DIAG_BUTTON_SHORT);
	press(1500);
	CHECK_EQ(diag_get_button(), DIAG_BUTTON_MEDIUM);
	press(3500);
	CHECK_EQ(diag_get_button(), DIAG_BUTTON_LONG);
}

static void event_read_once(void) {
	press(300);
	CHECK_EQ(diag_get_button(), DIAG_BUTTON_SHORT);
	CHECK_EQ(diag_get_button(), DIAG_BUTTON_NONE);
}

static void glitch_ignored(void) {
	press(10);		/* shorter than the 20 ms debounce */
	CHECK_EQ(diag_get_button(), DIAG_BUTTON_NONE);
}

static void contact_bounce_is_one_press(void) {
	for (int i = 0; i < 6; i++) {	/* bouncing closed */
		button = i & 1;
		ms(2);
	}
	button = 1;
	ms(400);
	for (int i = 0; i < 6; i++) {	/* bouncing open */
		button = !(i & 1);
		ms(2);
	}
	button = 0;
	ms(100);
	CHECK_EQ(diag_get_button(), DIAG_BUTTON_SHORT);
	CHECK_EQ(diag_get_button(), DIAG_BUTTON_NONE);
}

static void hold_feedback_leds(void) {
	button = 1;
	ms(500);
	CHECK(!green);
	ms(1000);			/* 1.5 s: a release now is a medium press */
	CHECK(green && !yellow && !red);
	ms(2000);			/* 3.5 s: long press */
	CHECK(green && yellow && red);
	button = 0;
	ms(100);
	diag_get_button();
}

static int green_ms(uint32_t n) {
	int on = 0;
	while (n--) {
		ms(1);
		on += green;
	}
	return on;
}

static void heartbeat(void) {
	ms(2000 - fake_tick % 2000);	/* align to the heartbeat period */
	ms(300);			/* past the button acknowledgement */
	CHECK_EQ(green_ms(2000), 50);
	diag_set_paused(1);
	CHECK_EQ(green_ms(2000), 100);	/* double flash */
	diag_set_paused(0);
	diag_set_tx(1);
	CHECK_EQ(green_ms(2000), 2000);
	diag_set_tx(0);
}

static int red_blinks(uint32_t n) {
	int blinks = 0, prev = red;
	while (n--) {
		ms(1);
		blinks += red && !prev;
		prev = red;
	}
	return blinks;
}

static void blink_codes(void) {
	CHECK_EQ(red_blinks(6000), 0);
	diag_set_error(DIAG_ERR_GPS);
	/* code 3: three 200 ms blinks in 3 x 500 ms, then 1.5 s off */
	CHECK_EQ(red_blinks(3 * 3000), 9);
	diag_set_error(DIAG_ERR_RADIO);		/* the lowest code wins */
	CHECK_EQ(red_blinks(4 * 2000), 4);
	diag_clear_error(DIAG_ERR_RADIO);
	diag_clear_error(DIAG_ERR_GPS);
	CHECK_EQ(red_blinks(6000), 0);
}

static void yellow_fix_and_pps(void) {
	diag_set_gps_fix(1);
	ms(10);
	CHECK(yellow);
	diag_set_gps_fix(0);
	ppsLockStatus = 1;
	diag_pps();
	ms(50);
	CHECK(yellow);		/* PPS flash */
	ms(100);
	CHECK(!yellow);
	ppsLockStatus = 0;
}

static void pps_cycle_count(void) {
	pps_source = PPS_SOURCE_DWT;
	DWT->CYCCNT = 0xFFFFF000u;		/* about to wrap */
	diag_pps();
	DWT->CYCCNT = 0xFFFFF000u + 23999970u;
	ms(1000);
	diag_pps();
	CHECK_EQ(pps_cycles, 23999970u);	/* -1.25 ppm, across the wrap */
	/* handed to the discipline with the tick interval */
	CHECK_EQ(sample_cycles, 23999970u);
	CHECK_EQ(sample_ms, 1000);
}

static void leds_off_stops_patterns(void) {
	diag_set_tx(1);
	diag_leds_off();
	ms(100);
	CHECK(!green && !yellow && !red);
	diag_set_tx(0);
	diag_selftest();
}

/* with the TIM4 capture, the discipline gets the exact interval even when
 * the PPS interrupt runs late; the DWT count, read in the interrupt, doesn't */
static void pps_capture_feeds_discipline(void) {
	pps_source = PPS_SOURCE_CAPTURE;
	fake_tim4.SR = TIM_SR_CC3IF;
	fake_tim4.CCR3 = 0xFF00;
	DWT->CYCCNT = 1000;
	diag_pps(); /* first capture: no interval yet */

	fake_tim4.SR = TIM_SR_CC3IF;
	fake_tim4.CCR3 = (uint16_t)(0xFF00 + 23999970u);
	DWT->CYCCNT = 1000 + 23999970u + 300; /* interrupt 300 cycles (12.5 us) late */
	ms(1000);
	diag_pps();
	CHECK_EQ(sample_cycles, 23999970u);
	CHECK_EQ(sample_ms, 1000);
	CHECK_EQ(pps_cycles, 23999970u + 300);
	uint32_t i = (pps_log_head - 1) % PPS_LOG;
	CHECK_EQ(pps_log[i].capture, 23999970u);
	CHECK_EQ(pps_log[i].dwt, 23999970u + 300);

	/* no capture at the next interrupt: the discipline gets 0 (rejected) */
	fake_tim4.SR = 0;
	ms(1000);
	diag_pps();
	CHECK_EQ(sample_cycles, 0);
	CHECK_EQ(pps_log[(pps_log_head - 1) % PPS_LOG].capture, 0);
}

/* without locked GPS time the pulse is logged but kept from the discipline
 * (#35: the u-blox keeps pulsing from its own crystal) */
static void pps_unlocked_not_sampled(void) {
	pps_source = PPS_SOURCE_CAPTURE;
	fake_tim4.SR = TIM_SR_CC3IF;
	fake_tim4.CCR3 = 0x1000;
	diag_pps();
	ms(1000);
	fake_tim4.SR = TIM_SR_CC3IF;
	fake_tim4.CCR3 = (uint16_t)(0x1000 + 24000010u);
	sample_cycles = 1234;
	uint32_t unlocked = pps_unlocked, head = pps_log_head;
	time_locked = 0;
	diag_pps();
	CHECK_EQ(sample_cycles, 1234); /* not called */
	CHECK_EQ(pps_unlocked, unlocked + 1);
	CHECK_EQ(pps_log_head, head + 1); /* still logged */
	CHECK_EQ(pps_log[(pps_log_head - 1) % PPS_LOG].capture, 24000010u);

	/* locked again: the next interval is sampled */
	ms(1000);
	fake_tim4.SR = TIM_SR_CC3IF;
	fake_tim4.CCR3 = (uint16_t)(0x1000 + 2 * 24000010u);
	time_locked = 1;
	diag_pps();
	CHECK_EQ(sample_cycles, 24000010u);
	CHECK_EQ(sample_ms, 1000);
	CHECK_EQ(pps_unlocked, unlocked + 1);
}

TEST_MAIN("diag", TEST(held_at_boot_is_ignored), TEST(short_medium_long), TEST(event_read_once),
		TEST(glitch_ignored), TEST(contact_bounce_is_one_press), TEST(hold_feedback_leds),
		TEST(heartbeat), TEST(blink_codes), TEST(yellow_fix_and_pps), TEST(pps_cycle_count),
		TEST(pps_capture_feeds_discipline), TEST(pps_unlocked_not_sampled),
		TEST(leds_off_stops_patterns))
