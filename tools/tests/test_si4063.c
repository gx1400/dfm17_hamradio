/*
 * test_si4063.c - Si4063 driver: synthesizer and modulation settings, TX
 * commands (si4063.c), checked from the SPI bytes it sends
 */
#include "test.h"
#include "fake_hal.h"
#include "si4063.h"
#include "spi.h"
#include "fake_settings.h"

/* ---- fake SPI: logs transactions, decodes SET_PROPERTY ---------------------- */

#define MAX_TX 512
static uint8_t txn[MAX_TX][16];
static uint8_t txn_len[MAX_TX];
static int ntxn;
static int selected;
static uint8_t cts_byte = 0xFF;		/* what READ_CMD_BUFF returns */
static uint8_t props[256][256];		/* [group][property] last value written */

void spi_select(void) {
	selected = 1;
	if (ntxn < MAX_TX)
		txn_len[ntxn] = 0;
}

void spi_deselect(void) {
	if (!selected)
		return;
	selected = 0;
	if (ntxn >= MAX_TX)
		return;
	uint8_t *t = txn[ntxn];
	if (txn_len[ntxn] >= 4 && t[0] == CMD_SET_PROPERTY)
		for (int i = 0; i < t[2] && 4 + i < txn_len[ntxn]; i++)
			props[t[1]][(uint8_t)(t[3] + i)] = t[4 + i];
	ntxn++;
}

uint8_t spi_write(uint8_t data) {
	if (selected && ntxn < MAX_TX && txn_len[ntxn] < 16)
		txn[ntxn][txn_len[ntxn]++] = data;
	return 0;
}

uint8_t spi_read(void) {
	return cts_byte;
}

void delay_us(uint16_t us) { (void)us; }

static uint8_t powered_off;
uint8_t diag_powered_off(void) {
	return powered_off;
}

/* ---- helpers ------------------------------------------------------------------ */

static void reset(void) {
	ntxn = 0;
	selected = 0;
	cts_byte = 0xFF;
	memset(props, 0, sizeof(props));
	fake_tick = 0;
	fake_tick_step = 0;
}

static uint32_t prop24(uint8_t group, uint8_t prop) {
	return props[group][prop] << 16 | props[group][prop + 1] << 8 | props[group][prop + 2];
}

static const int outdiv_of_band[] = {4, 6, 8, 12, 16, 24};

/* the carrier the PLL settings produce: (INTE + FRAC / 2^19) * 2 * XO / outdiv */
static double synthesized_hz(int *outdiv) {
	int band = props[PROP_MODEM][MODEM_CLKGEN_BAND] & 0x07;
	*outdiv = outdiv_of_band[band];
	uint8_t inte = props[PROP_FREQ_CONTROL][FREQ_CONTROL_INTE];
	uint32_t frac = prop24(PROP_FREQ_CONTROL, FREQ_CONTROL_FRAC);
	return (inte + frac / 524288.0) * 2.0 * XO_FREQ / *outdiv;
}

/* transactions other than CTS polls (READ_CMD_BUFF) */
static int find_cmd(uint8_t cmd) {
	for (int i = ntxn - 1; i >= 0; i--)
		if (txn_len[i] && txn[i][0] == cmd)
			return i;
	return -1;
}

/* ---- tests -------------------------------------------------------------------- */

static void frequency_across_70cm(void) {
	for (uint32_t f = 420000000; f <= 450000000; f += 250000) {
		int outdiv;
		reset();
		si4060_set_frequency(f);
		double got = synthesized_hz(&outdiv);
		double step = 2.0 * XO_FREQ / outdiv / 524288.0;	/* 12.2 Hz */
		CHECK_EQ(outdiv, 8);
		CHECK_EQ(props[PROP_MODEM][MODEM_CLKGEN_BAND] & SY_SEL_1, SY_SEL_1);
		/* rounded down to the synthesizer step */
		CHECK(got <= f && f - got < step);
		/* FRAC must stay in [2^19, 2^20) */
		uint32_t frac = prop24(PROP_FREQ_CONTROL, FREQ_CONTROL_FRAC);
		CHECK(frac >= 524288 && frac < 1048576);
	}
}

static void frequency_default_channel(void) {
	int outdiv;
	reset();
	si4060_set_frequency(cfg.freq_hz);
	CHECK_EQ(props[PROP_MODEM][MODEM_CLKGEN_BAND], SY_SEL_1 | FVCO_DIV_8);
	CHECK_NEAR(synthesized_hz(&outdiv), 446175000.0, 12.3);
}

static void band_selection(void) {
	static const struct { uint32_t f; int outdiv; } cases[] = {
		{144390000, 24}, {200000000, 16}, {300000000, 12},
		{433920000, 8}, {600000000, 6}, {868000000, 4},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int outdiv;
		reset();
		si4060_set_frequency(cases[i].f);
		double got = synthesized_hz(&outdiv);
		CHECK_EQ(outdiv, cases[i].outdiv);
		CHECK_NEAR(got, cases[i].f, 2.0 * XO_FREQ / outdiv / 524288.0);
	}
}

static void deviation_scales_with_band(void) {
	reset();
	si4060_set_frequency(446175000);
	si4060_set_deviation(2600);
	/* FREQ_DEV = dev * outdiv * 2^19 / (2 * XO) = 212.99 -> 212 */
	CHECK_EQ(prop24(PROP_MODEM, MODEM_FREQ_DEV), 212);

	si4060_set_frequency(144390000);	/* outdiv 24: 3x the units */
	si4060_set_deviation(2600);
	CHECK_EQ(prop24(PROP_MODEM, MODEM_FREQ_DEV), 638);
	si4060_set_frequency(446175000);
}

static void offset_units(void) {
	reset();
	si4060_set_frequency(446175000);
	CHECK_EQ(si4060_offset_units(2800), 229);
	CHECK_EQ(si4060_offset_units(-2800), -229);
	CHECK_EQ(si4060_offset_units(0), 0);
	/* the sine AFSK's largest offset fits MODEM_FREQ_OFFSET (16-bit signed) */
	CHECK(si4060_offset_units(25000) < 32767);
}

static void modulation_modes(void) {
	reset();
	si4060_set_cw(0);
	CHECK_EQ(props[PROP_MODEM][MODEM_MOD_TYPE],
			MOD_TYPE_2FSK | MOD_SOURCE_DIRECT | MOD_GPIO_3 | MOD_DIRECT_MODE_ASYNC);
	si4060_set_cw(1);
	CHECK_EQ(props[PROP_MODEM][MODEM_MOD_TYPE],
			MOD_TYPE_CW | MOD_SOURCE_DIRECT | MOD_GPIO_3 | MOD_DIRECT_MODE_ASYNC);
}

static void dfm17_config(void) {
	int outdiv;
	reset();
	si4060_config_dfm17();
	CHECK_EQ(props[PROP_MODEM][MODEM_MOD_TYPE] & 0x07, MOD_TYPE_2FSK);
	CHECK_NEAR(synthesized_hz(&outdiv), cfg.freq_hz, 12.3);
	CHECK_EQ(prop24(PROP_MODEM, MODEM_FREQ_DEV), 212);
	CHECK_EQ(props[PROP_PA][PA_PWR_LVL], cfg.pa_level);
	CHECK_EQ(props[PROP_MODEM][MODEM_FREQ_OFFSET] << 8 | props[PROP_MODEM][MODEM_FREQ_OFFSET + 1], 0);
}

static void tx_power(void) {
	reset();
	si4060_set_tx_power(0x7F);
	CHECK_EQ(props[PROP_PA][PA_PWR_LVL], 0x7F);
}

static void start_and_stop_tx(void) {
	reset();
	si4060_start_tx(0);
	int i = find_cmd(CMD_START_TX);
	CHECK(i >= 0);
	if (i >= 0) {
		CHECK_EQ(txn_len[i], 5);
		/* back to READY after TX: keeps the TCXO and the MCU's clock running */
		CHECK_EQ(txn[i][2] & 0xF0, START_TX_TXC_STATE_READY);
	}
	si4060_stop_tx();
	i = find_cmd(CMD_CHANGE_STATE);
	CHECK(i >= 0);
	if (i >= 0)
		CHECK_EQ(txn[i][1], STATE_READY);
}

/* nothing goes on the air without a callsign set up and valid settings */
static void tx_gate(void) {
	uint32_t refused = si4060_tx_refused;

	/* the built-in defaults: N0CALL */
	reset();
	settings_defaults(&cfg);
	si4060_start_tx(0);
	CHECK(find_cmd(CMD_START_TX) < 0);
	CHECK_EQ(si4060_tx_refused, refused + 1);

	/* live settings changed without a new CRC (e.g. a stray SWD write) */
	reset();
	provision();
	cfg.freq_hz = 446100000UL;
	si4060_start_tx(0);
	CHECK(find_cmd(CMD_START_TX) < 0);

	/* sealed, but out of band */
	reset();
	provision();
	cfg.freq_hz = 460000000UL;
	settings_seal(&cfg);
	si4060_start_tx(0);
	CHECK(find_cmd(CMD_START_TX) < 0);
	CHECK_EQ(si4060_tx_refused, refused + 3);

	/* set up properly: transmits */
	reset();
	provision();
	si4060_start_tx(0);
	CHECK(find_cmd(CMD_START_TX) >= 0);
	CHECK_EQ(si4060_tx_refused, refused + 3);

	/* set up, but powering off (#44) */
	reset();
	provision();
	powered_off = 1;
	si4060_start_tx(0);
	powered_off = 0;
	CHECK(find_cmd(CMD_START_TX) < 0);
	CHECK_EQ(si4060_tx_refused, refused + 4);
}

static void cts_timeout(void) {
	reset();
	cts_byte = 0x00;			/* radio never ready */
	fake_tick_step = 1;
	CHECK_EQ(si4060_get_cts(0), 1);
	CHECK(fake_tick > SI_TIMEOUT_MS);
	CHECK(fake_tick < 10 * SI_TIMEOUT_MS);
	cts_byte = 0xFF;
	fake_tick_step = 0;
	CHECK_EQ(si4060_get_cts(0), 0);
}

static void fast_spi_for_afsk(void) {
	reset();
	SPI1->CR1 = SPI_CR1_SPE | SPI_BAUDRATEPRESCALER_32;
	si4060_spi_fast(1);
	CHECK_EQ(SPI1->CR1 & SPI_CR1_BR, SPI_BAUDRATEPRESCALER_4);
	CHECK(SPI1->CR1 & SPI_CR1_SPE);
	CHECK_EQ(si4060_set_offset_fast(-229), 1);
	si4060_spi_fast(0);
	CHECK_EQ(SPI1->CR1 & SPI_CR1_BR, SPI_BAUDRATEPRESCALER_32);
	CHECK(SPI1->CR1 & SPI_CR1_SPE);
}

/* every test runs on a board set up as NZ0P (fake_settings.h) */
void test_setup(void) {
	provision();
}

TEST_MAIN("si4063", TEST(frequency_across_70cm), TEST(frequency_default_channel),
		TEST(band_selection), TEST(deviation_scales_with_band), TEST(offset_units),
		TEST(modulation_modes), TEST(dfm17_config), TEST(tx_power), TEST(start_and_stop_tx),
		TEST(tx_gate), TEST(cts_timeout), TEST(fast_spi_for_afsk))
