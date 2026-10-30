/*
 * test_settings: settings.c and settings_store.c. The EEPROM is a 512-byte
 * array behind fake ee_read/ee_write; the radio and APRS calls are recorded.
 * Also writes build/settings_layout.txt for the layout check against
 * tools/config/dfmcfg.py (Makefile).
 */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "fake_hal.h"
#include "fake_settings.h"
#include "i2c_ee.h"

/* ---- fakes --------------------------------------------------------------- */

static uint8_t eeprom[512];
static int ee_fail_reads, ee_fail_writes;
static uint32_t radio_freq, radio_dev;
static uint8_t radio_pa;
static int station_sets;

void i2c_init(void) {
}
uint8_t ee_read(uint8_t dev, uint16_t addr, volatile uint8_t *buf, uint16_t len) {
	if (dev != EE_USER_ADDR || ee_fail_reads)
		return 0;
	for (uint16_t i = 0; i < len; i++)
		buf[i] = eeprom[(addr + i) % sizeof(eeprom)];
	return 1;
}
uint8_t ee_write(uint8_t dev, uint16_t addr, const uint8_t *buf, uint8_t len) {
	if (dev != EE_USER_ADDR || ee_fail_writes)
		return 0;
	for (uint8_t i = 0; i < len; i++)
		eeprom[(addr + i) % sizeof(eeprom)] = buf[i];
	return 1;
}
uint8_t ee_poll(uint8_t dev) {
	(void)dev;
	return 3;
}
void aprs_set_station(void) {
	station_sets++;
}
uint32_t pps_disc_freq(uint32_t f) {
	return f;
}
void si4060_set_frequency(uint32_t f) {
	radio_freq = f;
}
void si4060_set_deviation(uint32_t d) {
	radio_dev = d;
}
void si4060_set_tx_power(uint8_t p) {
	radio_pa = p;
}

extern settings_t settings_edit;
extern volatile uint8_t settings_cmd, settings_result;
extern volatile uint32_t settings_done;

enum { CMD_EDIT = 1, CMD_APPLY, CMD_SAVE, CMD_RELOAD, CMD_DEFAULTS };
enum { RESULT_OK = 1, RESULT_INVALID, RESULT_EEPROM };

static void put(const settings_t *s) {
	memcpy(eeprom + SETTINGS_EE_ADDR, s, sizeof(*s));
}

static uint8_t command(uint8_t cmd) {
	uint32_t done = settings_done;
	settings_cmd = cmd;
	settings_poll();
	CHECK_EQ(settings_done, done + 1);
	CHECK_EQ(settings_cmd, 0);
	return settings_result;
}

void test_setup(void) {
	memset(eeprom, 0xFF, sizeof(eeprom));
	ee_fail_reads = ee_fail_writes = 0;
	radio_freq = radio_dev = radio_pa = 0;
	station_sets = 0;
}

/* ---- settings.c ------------------------------------------------------------ */

static void defaults_are_valid_but_dont_transmit(void) {
	settings_t s;
	settings_defaults(&s);
	CHECK_EQ(settings_check(&s), SETTINGS_OK);
	CHECK_EQ(settings_callsign_set(&s), 0);
	CHECK_EQ(s.freq_hz, 446175000UL);
	CHECK_EQ(s.mode, MODE_TRACKER);
	cfg = s;
	CHECK_STR(settings_callsign(), "N0CALL");
	CHECK_EQ(settings_tx_allowed(), 0);
	provision();
	CHECK_EQ(settings_tx_allowed(), 1);
	CHECK_STR(settings_callsign(), "NZ0P");
}

/* the same CRC as tools/config/dfmcfg.py: Python's
 * binascii.crc_hqx(b"123456789" + bytes(53), 0xFFFF) == 0x2CBE (computed in
 * Python, not here) */
static void crc_matches_python(void) {
	settings_t s;
	memset(&s, 0, sizeof(s));
	memcpy(&s, "123456789", 9);
	CHECK_EQ(settings_crc(&s), 0x2CBE);
}

/* every field limit, one change at a time from a valid block */
static void check_rejects_bad_fields(void) {
	static const struct {
		int seal;
		enum settings_error want;
	} want[] = {
			{0, SETTINGS_ERR_MAGIC},
			{0, SETTINGS_ERR_VERSION},
			{0, SETTINGS_ERR_CRC},
			{1, SETTINGS_ERR_CALLSIGN},
			{1, SETTINGS_ERR_CALLSIGN},
			{1, SETTINGS_ERR_CALLSIGN},
			{1, SETTINGS_ERR_CALLSIGN},
			{1, SETTINGS_ERR_CALLSIGN},
			{1, SETTINGS_ERR_CALLSIGN},
			{1, SETTINGS_ERR_SSID},
			{1, SETTINGS_ERR_SYMBOL},
			{1, SETTINGS_ERR_SYMBOL},
			{1, SETTINGS_ERR_PATH},
			{1, SETTINGS_ERR_PATH},
			{1, SETTINGS_ERR_PATH},
			{1, SETTINGS_ERR_MODE},
			{1, SETTINGS_ERR_FREQ},
			{1, SETTINGS_ERR_FREQ},
			{1, SETTINGS_ERR_FREQ},
			{1, SETTINGS_ERR_FREQ},
			{1, SETTINGS_ERR_POWER},
			{1, SETTINGS_ERR_CW},
			{1, SETTINGS_ERR_CW},
			{1, SETTINGS_ERR_DEVIATION},
			{1, SETTINGS_ERR_DEVIATION},
			{1, SETTINGS_ERR_TRACKER_TIMING},
			{1, SETTINGS_ERR_TRACKER_TIMING},
			{1, SETTINGS_ERR_TRACKER_TIMING},
			{1, SETTINGS_ERR_TRACKER_TIMING},
			{1, SETTINGS_ERR_FOX_TIMING},
			{1, SETTINGS_ERR_FOX_TIMING},
			{1, SETTINGS_ERR_FOX_TIMING},
	};
	for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		settings_t ss, *s = &ss;
		provision_as(s, "NZ0P");
		switch (i) {
		case 0:
			s->magic[0] = 0xFF;
			break;
		case 1:
			s->version = 2;
			break;
		case 2:
			s->ssid = 1;
			break;
		case 3:
			memcpy(s->callsign, "NZ0P\0X", 6);
			break;
		case 4:
			memcpy(s->callsign, "nz0p\0\0", 6);
			break;
		case 5:
			memcpy(s->callsign, "NZ\0\0\0\0", 6);
			break;
		case 6:
			memcpy(s->callsign, "NZOP\0\0", 6);
			break;
		case 7:
			memcpy(s->callsign, "12345\0", 6);
			break;
		case 8:
			memcpy(s->callsign, "NZ-0P\0", 6);
			break;
		case 9:
			s->ssid = 16;
			break;
		case 10:
			s->symbol_table = 'a';
			break;
		case 11:
			s->symbol_code = ' ';
			break;
		case 12:
			s->path = 0x12;
			break;
		case 13:
			s->path = 0x31;
			break;
		case 14:
			s->path = 0x10;
			break;
		case 15:
			s->mode = 2;
			break;
		case 16:
			s->freq_hz = 419999999UL;
			break;
		case 17:
			s->freq_hz = 450000001UL;
			break;
		case 18:
			s->freq_hz = 4461750UL;
			break;
		case 19:
			s->freq_hz = 144390000UL;
			break;
		case 20:
			s->pa_level = 0x80;
			break;
		case 21:
			s->cw_wpm = 4;
			break;
		case 22:
			s->cw_wpm = 41;
			break;
		case 23:
			s->dev_hz = 999;
			break;
		case 24:
			s->dev_hz = 5001;
			break;
		case 25:
			s->tracker_period_s = 29;
			break;
		case 26:
			s->tracker_fix_timeout_s = 300;
			break;
		case 27:
			s->tracker_settle_s = 120;
			break;
		case 28:
			s->tracker_fix_timeout_s = 9;
			break;
		case 29:
			s->fox_period_s = 9;
			break;
		case 30:
			s->fox_tone_ms = 60001;
			break;
		case 31:
			s->fox_tone_ms = 23000;
			break;
		}
		if (want[i].seal)
			settings_seal(s);
		if (settings_check(s) != want[i].want) {
			printf("  case %u: got %d, want %d\n", i, settings_check(s), want[i].want);
			CHECK(0);
		}
	}
	/* edge values that are fine */
	settings_t s;
	provision_as(&s, "W1AW");
	s.freq_hz = 420000000UL;
	s.ssid = 15;
	s.path = 0x21;
	s.symbol_table = 'A';
	s.fox_tone_ms = 22000;
	settings_seal(&s);
	CHECK_EQ(settings_check(&s), SETTINGS_OK);
	CHECK_EQ(settings_callsign_set(&s), 1);
}

/* ---- settings_store.c: boot ------------------------------------------------ */

static void boot_with_erased_eeprom_uses_defaults(void) {
	settings_init();
	CHECK_EQ(settings_source, SETTINGS_FROM_DEFAULTS);
	CHECK_EQ(settings_error, SETTINGS_ERR_MAGIC);
	CHECK_STR(settings_callsign(), "N0CALL");
	CHECK_EQ(settings_tx_allowed(), 0);
	CHECK(station_sets > 0);
}

static void boot_loads_valid_block(void) {
	settings_t s;
	provision_as(&s, "NZ0P");
	s.freq_hz = 432500000UL;
	settings_seal(&s);
	put(&s);
	settings_init();
	CHECK_EQ(settings_source, SETTINGS_FROM_EEPROM);
	CHECK_EQ(settings_error, SETTINGS_OK);
	CHECK_EQ(cfg.freq_hz, 432500000UL);
	CHECK_EQ(settings_tx_allowed(), 1);
}

static void boot_ignores_corrupt_block(void) {
	settings_t s;
	provision_as(&s, "NZ0P");
	put(&s);
	eeprom[SETTINGS_EE_ADDR + 20] ^= 0x01; /* one bit flipped: the PA level */
	settings_init();
	CHECK_EQ(settings_source, SETTINGS_FROM_DEFAULTS);
	CHECK_EQ(settings_error, SETTINGS_ERR_CRC);
	CHECK_EQ(settings_tx_allowed(), 0);

	/* an EEPROM that doesn't answer */
	ee_fail_reads = 1;
	settings_init();
	CHECK_EQ(settings_error, SETTINGS_ERR_EEPROM);
	CHECK_EQ(settings_source, SETTINGS_FROM_DEFAULTS);
}

/* ---- settings_store.c: SWD mailbox ----------------------------------------- */

static void swd_edit_apply_save_reload(void) {
	settings_init();
	CHECK_EQ(command(CMD_EDIT), RESULT_OK);
	CHECK_MEM(settings_edit.callsign, "N0CALL", 6);

	/* the tool changes fields only; APPLY seals and checks */
	memset(settings_edit.callsign, 0, 6);
	memcpy(settings_edit.callsign, "NZ0P", 4);
	settings_edit.freq_hz = 432500000UL;
	settings_edit.crc = 0;
	CHECK_EQ(command(CMD_APPLY), RESULT_OK);
	CHECK_EQ(settings_source, SETTINGS_FROM_SWD);
	CHECK_EQ(settings_tx_allowed(), 1);
	CHECK_EQ(radio_freq, 432500000UL); /* retuned right away */
	CHECK_EQ(radio_dev, cfg.dev_hz);
	CHECK_EQ(radio_pa, cfg.pa_level);

	CHECK_EQ(command(CMD_SAVE), RESULT_OK);
	CHECK_EQ(settings_source, SETTINGS_FROM_EEPROM);
	CHECK_MEM(eeprom + SETTINGS_EE_ADDR, &cfg, sizeof(cfg));

	/* back to defaults (not saved), then reload the saved block */
	CHECK_EQ(command(CMD_DEFAULTS), RESULT_OK);
	CHECK_EQ(settings_tx_allowed(), 0);
	CHECK_EQ(command(CMD_RELOAD), RESULT_OK);
	CHECK_EQ(cfg.freq_hz, 432500000UL);
	CHECK_EQ(settings_tx_allowed(), 1);

	/* a reboot finds them too */
	settings_init();
	CHECK_EQ(settings_source, SETTINGS_FROM_EEPROM);
	CHECK_EQ(cfg.freq_hz, 432500000UL);
}

/* a mistyped value is refused and changes nothing */
static void swd_apply_refuses_bad_values(void) {
	settings_t before;
	provision_as(&before, "NZ0P");
	put(&before);
	settings_init();
	before = cfg;

	command(CMD_EDIT);
	settings_edit.freq_hz = 4461750UL; /* a digit short */
	CHECK_EQ(command(CMD_APPLY), RESULT_INVALID);
	CHECK_EQ(settings_error, SETTINGS_ERR_FREQ);
	CHECK_MEM(&cfg, &before, sizeof(cfg));
	CHECK_EQ(radio_freq, 0); /* radio untouched */

	command(CMD_EDIT);
	memcpy(settings_edit.callsign, "NZ0P 1", 6);
	CHECK_EQ(command(CMD_APPLY), RESULT_INVALID);
	CHECK_EQ(settings_error, SETTINGS_ERR_CALLSIGN);
	CHECK_MEM(&cfg, &before, sizeof(cfg));
	CHECK_EQ(settings_tx_allowed(), 1); /* still running on the old ones */

	/* unknown command */
	CHECK_EQ(command(42), 4);
}

static void save_failure_reported(void) {
	settings_init();
	ee_fail_writes = 1;
	CHECK_EQ(command(CMD_SAVE), RESULT_EEPROM);
	CHECK_EQ(settings_source, SETTINGS_FROM_DEFAULTS);
	ee_fail_writes = 0;
	ee_fail_reads = 1;
	CHECK_EQ(command(CMD_RELOAD), RESULT_EEPROM);
	CHECK_EQ(settings_error, SETTINGS_ERR_EEPROM);
}

/* live settings changed outside settings_apply are put back */
static void runtime_check_restores_settings(void) {
	settings_t s;
	provision_as(&s, "NZ0P");
	put(&s);
	settings_init();
	uint32_t repairs = settings_repairs;

	settings_poll();
	CHECK_EQ(settings_repairs, repairs); /* nothing wrong, nothing done */

	cfg.freq_hz = 460000000UL; /* e.g. a stray SWD write */
	settings_poll();
	CHECK_EQ(settings_repairs, repairs + 1);
	CHECK_EQ(cfg.freq_hz, 446175000UL);
	CHECK_EQ(radio_freq, 446175000UL);

	/* even a valid-looking, re-sealed change is put back: only APPLY changes cfg */
	cfg.pa_level = 0x7F;
	settings_seal(&cfg);
	settings_poll();
	CHECK_EQ(settings_repairs, repairs + 2);
	CHECK_EQ(cfg.pa_level, 0x20);
	CHECK_EQ(settings_check(&cfg), SETTINGS_OK);
}

/* field offsets for the layout check against tools/config/dfmcfg.py */
static void write_layout(void) {
	FILE *f = fopen("build/settings_layout.txt", "w");
	CHECK(f != NULL);
	if (!f)
		return;
#define FIELD(name)                                                                                \
	fprintf(f, "%s %zu %zu\n", #name, offsetof(settings_t, name), sizeof(((settings_t *)0)->name))
	FIELD(magic);
	FIELD(version);
	FIELD(length);
	FIELD(callsign);
	FIELD(ssid);
	FIELD(symbol_table);
	FIELD(symbol_code);
	FIELD(path);
	FIELD(mode);
	FIELD(flags);
	FIELD(freq_hz);
	FIELD(pa_level);
	FIELD(cw_wpm);
	FIELD(dev_hz);
	FIELD(tracker_period_s);
	FIELD(tracker_fix_timeout_s);
	FIELD(tracker_settle_s);
	FIELD(fox_period_s);
	FIELD(fox_tone_ms);
	FIELD(fox_aprs_every);
	FIELD(reserved);
	FIELD(crc);
#undef FIELD
	fclose(f);
}

TEST_MAIN("settings", TEST(defaults_are_valid_but_dont_transmit), TEST(crc_matches_python),
		TEST(check_rejects_bad_fields), TEST(boot_with_erased_eeprom_uses_defaults),
		TEST(boot_loads_valid_block), TEST(boot_ignores_corrupt_block),
		TEST(swd_edit_apply_save_reload), TEST(swd_apply_refuses_bad_values),
		TEST(save_failure_reported), TEST(runtime_check_restores_settings), TEST(write_layout), )
