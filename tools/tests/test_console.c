/*
 * test_console.c - the serial console (console.c): USB gating, line
 * assembly, echo, the commands, config mode, and moving the settings and
 * calibration blocks as hex. The hardware side (console_hw.c), the mailbox
 * (settings_command) and the GPS are fakes that record what they're asked.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "fake_hal.h"
#include "fake_settings.h"
#include "console.h"
#include "analog.h"
#include "cal.h"

/* ---- fakes --------------------------------------------------------------- */

static char out[4096];
static size_t out_len;
static int hw_on, resets, wakes, sleeps, tracker_starts, paused;
static int cmds[16], ncmds;
static uint8_t fake_result = RESULT_OK;

static void put_out(const char *s, size_t n) {
	if (out_len + n >= sizeof(out))
		n = sizeof(out) - 1 - out_len;
	memcpy(out + out_len, s, n);
	out_len += n;
	out[out_len] = 0;
}
void console_hw_enable(uint8_t on) {
	hw_on = on;
}
void console_hw_send(const char *s, uint16_t n) {
	put_out(s, n);
}
void console_hw_vprintf(const char *fmt, va_list ap) {
	char buf[512];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	put_out(buf, (size_t)n);
}
void console_hw_ver(void) {
	console_printf("ok ver\r\n");
}
void console_hw_status(void) {
	console_printf("ok status\r\n");
}
void console_hw_reset(void) {
	resets++;
}
uint8_t settings_command(uint8_t cmd) {
	if (ncmds < 16)
		cmds[ncmds++] = cmd;
	return fake_result;
}
settings_t settings_edit;
cal_t cal_edit;
volatile uint32_t settings_repairs = 2;
volatile uint32_t si4060_tx_refused = 3;
analog_reading analog_last;
const char fw_version[] = "DFMFW test tracker";
void gpsWake(void) {
	wakes++;
}
void gpsSleep(void) {
	sleeps++;
}
void tracker_start(void) {
	tracker_starts++;
}
void diag_set_paused(uint8_t on) {
	paused = on;
}
uint8_t diag_paused(void) {
	return (uint8_t)paused;
}

extern volatile uint32_t console_dropped;

static void clear(void) {
	out_len = 0;
	out[0] = 0;
	ncmds = 0;
}

/* type a line (CR added) and let the main loop run it; returns the output */
static const char *type(const char *s) {
	clear();
	while (*s)
		console_rx((uint8_t)*s++);
	console_rx('\r');
	console_poll();
	return out;
}

static void to_hex(char *hex, const void *data, int n) {
	const uint8_t *p = data;
	for (int i = 0; i < n; i++)
		sprintf(hex + 2 * i, "%02x", p[i]);
}

void test_setup(void) {
	provision();
	cal_defaults(&cal);
	analog_last.on_usb = 1;
	fake_result = RESULT_OK;
	console_init(0); /* every test starts from battery: off, run mode */
	console_init(1);
	resets = wakes = sleeps = tracker_starts = paused = 0;
	clear();
}

/* ---- tests ----------------------------------------------------------------- */

static void off_on_battery(void) {
	console_init(0);
	CHECK_EQ(hw_on, 0);
	CHECK_EQ(console_enabled(), 0);
	CHECK_EQ(console_log_on(), 0);
	CHECK_EQ(console_output_on(), 0);
	CHECK_STR(type("help"), ""); /* nothing echoed or run */
	console_printf("ok\r\n");
	CHECK_EQ(out_len, 0);
}

static void on_with_usb_and_banner(void) {
	console_init(0);
	clear();
	console_init(1);
	CHECK_EQ(hw_on, 1);
	CHECK(strstr(out, "DFMFW test tracker") != NULL);
	CHECK(strstr(out, "> ") != NULL);
	CHECK_EQ(console_log_on(), 1);
}

/* two readings in a row switch it; one glitch doesn't */
static void follows_usb_after_two_readings(void) {
	console_usb(0);
	CHECK_EQ(console_enabled(), 1);
	console_usb(1);
	console_usb(0);
	CHECK_EQ(console_enabled(), 1);
	console_usb(0);
	CHECK_EQ(console_enabled(), 0);
	CHECK_EQ(hw_on, 0);
	console_usb(1);
	console_usb(1);
	CHECK_EQ(console_enabled(), 1);
}

static void echo_backspace_and_prompt(void) {
	clear();
	console_rx('h');
	console_rx('x');
	console_poll();
	CHECK_STR(out, "hx");
	clear();
	console_rx(0x7F);
	console_poll();
	CHECK_STR(out, "\b \b");
	clear();
	const char *s = "elp\r";
	while (*s)
		console_rx((uint8_t)*s++);
	console_poll();
	CHECK(strncmp(out, "elp\r\nok help ver status", 23) == 0);
	CHECK(strcmp(out + out_len - 2, "> ") == 0);
}

/* the end of a line arrives between two main loop passes: all of it is
 * echoed before the reply (found on board 1: "gps off" echoed as "gps o") */
static void echo_catches_up_at_the_end(void) {
	clear();
	console_rx('f');
	console_rx('o');
	console_poll();
	clear();
	const char *s = "x stop\r";
	while (*s)
		console_rx((uint8_t)*s++);
	console_poll();
	CHECK(strncmp(out, "x stop\r\nok\r\n", 12) == 0);
	CHECK_EQ(paused, 1);
}

static void bad_lines(void) {
	CHECK(strstr(type("frob"), "err unknown") != NULL);
	char longline[CONSOLE_LINE_MAX + 10];
	memset(longline, 'a', sizeof(longline) - 1);
	longline[sizeof(longline) - 1] = 0;
	CHECK(strstr(type(longline), "err long") != NULL);
	/* control characters are dropped, empty lines ignored */
	uint32_t dropped = console_dropped;
	clear();
	console_rx('\r');
	console_rx(0x01);
	console_poll();
	CHECK_STR(out, "");
	CHECK_EQ(console_dropped, dropped + 1);
}

/* while a line waits for the main loop, more input is dropped, not mixed in */
static void line_pending_drops_input(void) {
	uint32_t dropped = console_dropped;
	clear();
	const char *s = "fox stop\rxyz";
	while (*s)
		console_rx((uint8_t)*s++);
	CHECK_EQ(console_dropped, dropped + 3);
	console_poll();
	CHECK_EQ(paused, 1);
}

static void fox_and_log(void) {
	CHECK(strstr(type("fox stop"), "ok") != NULL);
	CHECK_EQ(paused, 1);
	type("fox start");
	CHECK_EQ(paused, 0);
	CHECK(strstr(type("fox"), "err usage") != NULL);

	CHECK(strstr(type("log off"), "ok") != NULL);
	CHECK_EQ(console_log_on(), 0);
	CHECK_EQ(console_output_on(), 0);				/* diagnostics dropped... */
	CHECK(strstr(type("help"), "ok help") != NULL); /* ...replies still sent */
	type("log on");
	CHECK_EQ(console_log_on(), 1);
	CHECK(strstr(type("log maybe"), "err usage") != NULL);
}

static void config_mode_gates_changes(void) {
	CHECK(strstr(type("cfg save"), "err config") != NULL);
	CHECK(strstr(type("cal defaults"), "err config") != NULL);
	CHECK(strstr(type("gps off"), "err config") != NULL);
	CHECK_EQ(ncmds, 0);

	CHECK(strstr(type("config"), "ok 0") != NULL);
	CHECK_EQ(console_config_mode(), 1);
	CHECK(strstr(type("config"), "ok 1") != NULL);

	type("cfg save");
	CHECK_EQ(ncmds, 1);
	CHECK_EQ(cmds[0], CMD_SAVE);
	type("cfg reload");
	CHECK_EQ(cmds[0], CMD_RELOAD);
	type("cfg defaults");
	CHECK_EQ(cmds[0], CMD_DEFAULTS);
	type("cal save");
	CHECK_EQ(cmds[0], CMD_CAL_SAVE);
	type("cal reload");
	CHECK_EQ(cmds[0], CMD_CAL_RELOAD);
	type("cal defaults");
	CHECK_EQ(cmds[0], CMD_CAL_DEFAULTS);
	fake_result = RESULT_EEPROM;
	CHECK(strstr(type("cfg save"), "err eeprom") != NULL);
	CHECK(strstr(type("cfg frob"), "err usage") != NULL);

	CHECK(strstr(type("run"), "ok") != NULL);
	CHECK_EQ(console_config_mode(), 0);
}

static void cfg_get_format(void) {
	char want[2 * SETTINGS_SIZE + 40], hex[2 * SETTINGS_SIZE + 1];
	to_hex(hex, &cfg, SETTINGS_SIZE);
	settings_source = SETTINGS_FROM_EEPROM;
	settings_error = SETTINGS_OK;
	snprintf(want, sizeof(want), "cfg get\r\nok %s 1 0 2 3\r\n> ", hex);
	CHECK_STR(type("cfg get"), want);

	to_hex(hex, &cal, CAL_SIZE);
	snprintf(want, sizeof(want), "ok %s 0 0 2 3", hex);
	CHECK(strstr(type("cal get"), want) != NULL);
}

static void cfg_put_checks_the_block(void) {
	char line[CONSOLE_LINE_MAX];
	settings_t s;
	provision_as(&s, "KE0ABC");
	type("config");

	strcpy(line, "cfg put ");
	to_hex(line + 8, &s, SETTINGS_SIZE);
	CHECK(strstr(type(line), "ok") != NULL);
	CHECK_EQ(ncmds, 1);
	CHECK_EQ(cmds[0], CMD_APPLY);
	CHECK_MEM(&settings_edit, &s, sizeof(s));

	/* the firmware refused it: why */
	fake_result = RESULT_INVALID;
	settings_error = SETTINGS_ERR_FREQ;
	CHECK(strstr(type(line), "err invalid 9") != NULL);
	fake_result = RESULT_OK;

	/* a damaged line: one digit changed, so the CRC fails; nothing applied */
	line[8 + 10] = line[8 + 10] == '0' ? '1' : '0';
	CHECK(strstr(type(line), "err crc") != NULL);
	CHECK_EQ(ncmds, 0);

	line[8 + 10] = 'g';
	CHECK(strstr(type(line), "err hex") != NULL);
	CHECK(strstr(type("cfg put 0011"), "err length") != NULL);
	CHECK_EQ(ncmds, 0);
}

static void cal_put(void) {
	char line[CONSOLE_LINE_MAX];
	cal_t c;
	cal_defaults(&c);
	c.ch[CAL_VIN].a_q16 = 131020;
	cal_seal(&c);
	type("config");
	strcpy(line, "cal put ");
	to_hex(line + 8, &c, CAL_SIZE);
	CHECK(strstr(type(line), "ok") != NULL);
	CHECK_EQ(cmds[0], CMD_CAL_APPLY);
	CHECK_MEM(&cal_edit, &c, sizeof(c));
}

/* gps on/off in config mode; run (or losing USB) wakes a GPS it stopped */
static void gps_and_back_to_run(void) {
	type("config");
	type("gps off");
	CHECK_EQ(sleeps, 1);
	type("gps on");
	CHECK_EQ(wakes, 1);
	type("gps off");
	type("run");
	CHECK_EQ(wakes, 2);
	CHECK_EQ(tracker_starts, 1);
	CHECK(strstr(type("gps"), "err config") != NULL);

	/* USB goes away in config mode: back to run mode, GPS on again */
	type("config");
	type("gps off");
	console_usb(0);
	console_usb(0);
	CHECK_EQ(console_config_mode(), 0);
	CHECK_EQ(wakes, 3);
	CHECK_EQ(tracker_starts, 2);
}

static void reset_only_on_usb(void) {
	analog_last.on_usb = 0;
	CHECK(strstr(type("reset"), "err battery") != NULL);
	CHECK_EQ(resets, 0);
	analog_last.on_usb = 1;
	CHECK(strstr(type("reset"), "ok") != NULL);
	CHECK_EQ(resets, 1);
}

static void analog_ver_status(void) {
	analog_last = (analog_reading){.vin_raw = 2934,
			.iin_raw = 57,
			.vref_raw = 1486,
			.vdda_mv = 3306,
			.vin_mv = 4942,
			.iin_ma = 50,
			.temp_c = -5,
			.usb_raw = 3020,
			.usb_mv = 4923,
			.on_usb = 1};
	CHECK(strstr(type("analog"), "ok 2934 57 1486 3306 4942 50 -5 3020 4923 1") != NULL);
	CHECK(strstr(type("ver"), "ok ver") != NULL);
	CHECK(strstr(type("status"), "ok status") != NULL);
}

TEST_MAIN("console", TEST(off_on_battery), TEST(on_with_usb_and_banner),
		TEST(follows_usb_after_two_readings), TEST(echo_backspace_and_prompt),
		TEST(echo_catches_up_at_the_end), TEST(bad_lines), TEST(line_pending_drops_input),
		TEST(fox_and_log), TEST(config_mode_gates_changes), TEST(cfg_get_format),
		TEST(cfg_put_checks_the_block), TEST(cal_put), TEST(gps_and_back_to_run),
		TEST(reset_only_on_usb), TEST(analog_ver_status))
