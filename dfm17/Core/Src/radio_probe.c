/**
  ******************************************************************************
  * @file    radio_probe.c
  * @brief   Temporary diagnostic: why the Si4063 DIV_CLK never reaches OSC_IN
  ******************************************************************************
  * Runs once at boot (called from initHw when RADIO_CLOCK_PROBE is defined),
  * before switchToRadioClock(), while SYSCLK is still the HSI PLL. Results go to
  * the radio_probe struct in RAM, read back over SWD.
  *
  * For each variant of the Si4063 clock-output setup it records:
  *   - PD0 transitions over 10000 reads with PD01_REMAP on and HSE off (PD0 is
  *     only readable as a GPIO with the remap enabled)
  *   - how long HSE bypass takes to report HSERDY, 0xFFFF if not within 20 ms
  *   - GLOBAL_CLK_CFG and the GPIO2 config byte read back from the Si4063
  ******************************************************************************
  */

#include "main.h"
#include "spi.h"
#include "si4063.h"

#define PROBE_VARIANTS	7
#define CMD_REQUEST_DEVICE_STATE	0x33

struct {
	uint32_t magic;							/* 0x50524f42 "PROB" when done */
	uint16_t part;
	uint8_t xo_tune, clk_cfg, global_config;
	uint8_t gpio_cfg[7];					/* GPIO0..3, NIRQ, SDO, GEN_CONFIG */
	uint8_t state;
	uint8_t pad;
	uint16_t pd0_transitions[PROBE_VARIANTS];
	uint16_t pd0_ones[PROBE_VARIANTS];
	uint16_t hse_ready_ms[PROBE_VARIANTS];
	uint8_t clk_cfg_after[PROBE_VARIANTS];
	uint8_t gpio2_after[PROBE_VARIANTS];
	uint8_t state_after[PROBE_VARIANTS];
} volatile radio_probe;

/* GPIO_PIN_CFG with all zeros changes nothing and returns the current config */
static void read_gpio_cfg(volatile uint8_t *resp) {
	si4060_get_cts(0);
	spi_select();
	spi_write(CMD_GPIO_PIN_CFG);
	for (int i = 0; i < 7; i++)
		spi_write(0x00);
	spi_deselect();
	si4060_get_cts(1);
	for (int i = 0; i < 7; i++)
		resp[i] = spi_read();
	spi_deselect();
}

static uint8_t read_state(void) {
	uint8_t state;
	si4060_get_cts(0);
	spi_select();
	spi_write(CMD_REQUEST_DEVICE_STATE);
	spi_deselect();
	si4060_get_cts(1);
	state = spi_read();
	spi_read();
	spi_deselect();
	return state;
}

static void sample_pd0(uint16_t *transitions, uint16_t *ones) {
	__HAL_RCC_AFIO_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_AFIO_REMAP_PD01_ENABLE();		/* PD0 as GPIO input (reset: floating) */
	uint32_t prev = GPIOD->IDR & 1;
	uint16_t t = 0, o = 0;
	for (int i = 0; i < 10000; i++) {
		uint32_t cur = GPIOD->IDR & 1;
		t += cur != prev;
		o += cur;
		prev = cur;
	}
	__HAL_AFIO_REMAP_PD01_DISABLE();
	*transitions = t;
	*ones = o;
}

static uint16_t hse_ready(void) {
	uint16_t ms = 0xFFFF;
	CLEAR_BIT(RCC->CR, RCC_CR_HSEON);
	while (READ_BIT(RCC->CR, RCC_CR_HSERDY))
		;
	SET_BIT(RCC->CR, RCC_CR_HSEBYP);
	SET_BIT(RCC->CR, RCC_CR_HSEON);
	for (uint16_t i = 0; i <= 20; i++) {
		if (READ_BIT(RCC->CR, RCC_CR_HSERDY)) {
			ms = i;
			break;
		}
		HAL_Delay(1);
	}
	CLEAR_BIT(RCC->CR, RCC_CR_HSEON);
	while (READ_BIT(RCC->CR, RCC_CR_HSERDY))
		;
	CLEAR_BIT(RCC->CR, RCC_CR_HSEBYP);
	return ms;
}

static void measure(int v) {
	volatile uint8_t gpio[7];
	HAL_Delay(5);
	sample_pd0((uint16_t *)&radio_probe.pd0_transitions[v], (uint16_t *)&radio_probe.pd0_ones[v]);
	radio_probe.hse_ready_ms[v] = hse_ready();
	radio_probe.clk_cfg_after[v] = si4060_get_property_8(PROP_GLOBAL, GLOBAL_CLK_CFG);
	read_gpio_cfg(gpio);
	radio_probe.gpio2_after[v] = gpio[2];
	radio_probe.state_after[v] = read_state();
}

void radio_clock_probe(void) {
	radio_probe.part = si4060_part_info();
	radio_probe.xo_tune = si4060_get_property_8(PROP_GLOBAL, GLOBAL_XO_TUNE);
	radio_probe.clk_cfg = si4060_get_property_8(PROP_GLOBAL, GLOBAL_CLK_CFG);
	radio_probe.global_config = si4060_get_property_8(PROP_GLOBAL, GLOBAL_CONFIG);
	read_gpio_cfg(radio_probe.gpio_cfg);
	radio_probe.state = read_state();

	/* 0: as left by initRadio() */
	measure(0);

	/* 1: RS41ng order and GPIO bytes: CLK_CFG first, then GPIO0/1 unused,
	 * GPIO2 DIV_CLK, GPIO3 input, GEN_CONFIG 0x03 */
	si4060_set_property_8(PROP_GLOBAL, GLOBAL_CLK_CFG, DIV_CLK_EN | DIV_CLK_SEL_2);
	si4060_gpio_pin_cfg(0x00, 0x00, GPIO_MODE_DIV_CLK, GPIO_MODE_INPUTPIN, 0x03);
	measure(1);

	/* 2: weakest drive strength */
	si4060_gpio_pin_cfg(0x00, 0x00, GPIO_MODE_DIV_CLK, GPIO_MODE_INPUTPIN, 0x60);
	measure(2);

	/* 3: divide by 30 (853 kHz), easier for a slow input to follow */
	si4060_set_property_8(PROP_GLOBAL, GLOBAL_CLK_CFG, DIV_CLK_EN | DIV_CLK_SEL_30);
	si4060_gpio_pin_cfg(0x00, 0x00, GPIO_MODE_DIV_CLK, GPIO_MODE_INPUTPIN, 0x00);
	measure(3);

	/* 4: GPIO2 driven static high (mode 3 = DRIVE1): is PD0 wired to GPIO2? */
	si4060_gpio_pin_cfg(0x00, 0x00, 0x03, GPIO_MODE_INPUTPIN, 0x00);
	measure(4);

	/* 5: back to DIV_CLK / 2, synthesizer running (TX_TUNE) */
	si4060_set_property_8(PROP_GLOBAL, GLOBAL_CLK_CFG, DIV_CLK_EN | DIV_CLK_SEL_2);
	si4060_gpio_pin_cfg(0x00, 0x00, GPIO_MODE_DIV_CLK, GPIO_MODE_INPUTPIN, 0x00);
	si4060_change_state(STATE_TX_TUNE);
	measure(5);

	/* 6: restore the normal configuration, READY */
	si4060_change_state(STATE_READY);
	si4060_gpio_pin_cfg(GPIO_MODE_TX_DATA_CLK, GPIO_MODE_EN_PA, GPIO_MODE_DIV_CLK,
			GPIO_MODE_INPUTPIN, DRV_STRENGTH_HIGH);
	measure(6);

	radio_probe.magic = 0x50524f42;
}
