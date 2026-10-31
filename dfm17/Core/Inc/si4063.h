/**
  ******************************************************************************
  * @file    si4063.h
  * @brief   This file contains all the function prototypes for
  *          the si4063.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 Derek Rowland <gx1400@gmail.com>
  * All rights reserved.
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  ******************************************************************************
  */

#ifndef INC_SI4063_H_
#define INC_SI4063_H_

#include "main.h"

/*
 * si4060 software library
 *
 * Stefan Biereigel
 *
 */

#define USE_TCXO				/* TCXO connected to XOUT pin */
#define XO_FREQ					25600000UL
//#define XO_FREQ					16367600UL

#define RF_FREQ_HZ_2M_RTTY		144700000.0f
#define RF_FREQ_HZ_2M_EU		144800000.0f
#define RF_FREQ_HZ_2M_US		144390000.0f
#define RF_FREQ_HZ_2M_JP		144660000.0f
#define RF_FREQ_HZ_2M_CN		144640000.0f
#define RF_FREQ_HZ_2M_BRAZIL	145570000.0f
#define RF_FREQ_HZ_2M_AUS		145175000.0f
#define RF_FREQ_HZ_2M_NZ		144575000.0f
#define RF_FREQ_HZ_2M_THAI		145525000.0f
/* DFM-17 transmit frequency, deviation and PA level: the settings (cfg,
 * settings.h), default 446.175 MHz. The DFM-17 RF output stage was designed
 * for 400-406 MHz */
extern volatile uint32_t si4060_tx_refused;

#define RF_RTTY_DEV_HZ			200.0f
#define RF_APRS_DEV_HZ			1300.0f
#define RF_MOD_APRS_SR			4400

#define F_INT_70CM				(2 * XO_FREQ / 8)
#define F_INT_2M				(2 * XO_FREQ / 24)
#define OUTDIV_70CM				8
#define OUTDIV_2M				24

#define FDIV_INTE_2M(freq)	((RF_FREQ_HZ_2M_ ## freq / F_INT_2M) - 1)
#define FDIV_FRAC_2M(freq)	(((RF_FREQ_HZ_2M_ ## freq - F_INT_2M * (int)FDIV_INTE_2M(freq))*((uint32_t)1 << 19)) / F_INT_2M)
#define FDEV_RTTY			((((uint32_t)1 << 19) * OUTDIV_2M * RF_RTTY_DEV_HZ)/(2*XO_FREQ))
#define FDEV_APRS		((((uint32_t)1 << 19) * OUTDIV_2M * RF_APRS_DEV_HZ)/(2*XO_FREQ))


/* longest wait for CTS from the Si4063 (POWER_UP needs several ms) */
#define SI_TIMEOUT_MS		50

/* function prototypes */

void si4060_config_dfm17(void);
void si4060_set_cw(uint8_t on);
void si4060_set_frequency(uint32_t freq_hz);
void si4060_set_deviation(uint32_t dev_hz);
void si4060_set_tx_power(uint8_t level);

#if APRS_AFSK_SINE /* the sine_test build (aprs.h) */
void si4060_set_offset(uint16_t offset);
int32_t si4060_offset_units(int32_t hz);
void si4060_spi_fast(uint8_t on);
uint8_t si4060_set_offset_fast(int16_t offset);
#endif
void si4060_start_tx(uint8_t channel);
void si4060_stop_tx(void);
void si4060_shutdown(void);
void si4060_wakeup(void);
void si4060_reset(void);
uint16_t si4060_part_info(void);
uint8_t si4060_get_cts(uint8_t read_response);
uint8_t si4060_read_cmd_buf(uint8_t deselect);
void si4060_power_up(void);
void si4060_change_state(uint8_t state);
void si4060_set_property_8(uint8_t group, uint8_t prop, uint8_t val);
uint8_t si4060_get_property_8(uint8_t group, uint8_t prop);
void si4060_set_property_16(uint8_t group, uint8_t prop, uint16_t val);
void si4060_set_property_16_nocts(uint8_t group, uint8_t prop, uint16_t val);
void si4060_set_property_24(uint8_t group, uint8_t prop, uint32_t val);
void si4060_set_property_32(uint8_t group, uint8_t prop, uint32_t val);
void si4060_setup(uint8_t mod_type);
void si4060_set_filter(void);
void si4060_gpio_pin_cfg(uint8_t gpio0, uint8_t gpio1, uint8_t gpio2, uint8_t gpio3, uint8_t drvstrength);


/* ===== command definitions ===== */
#define CMD_NOP							0x00
#define CMD_PART_INFO					0x01
#define CMD_POWER_UP					0x02
#define CMD_SET_PROPERTY				0x11
#define CMD_GET_PROPERTY				0x12
#define CMD_GPIO_PIN_CFG				0x13
#define CMD_START_TX					0x31
#define CMD_CHANGE_STATE				0x34
#define CMD_READ_CMD_BUF				0x44

/* ===== device states ===== */
#define STATE_NOCHANGE					0x00
#define STATE_SLEEP						0x01
#define STATE_SPI_ACTIVE				0x02
#define STATE_READY						0x03
#define STATE_TX_TUNE					0x05
#define STATE_TX						0x07

/* ===== property group definitions ===== */
#define PROP_GLOBAL						0x00
#define PROP_INT_CTL					0x01
#define PROP_FRR_CTL					0x02
#define PROP_PREAMBLE					0x10
#define PROP_SYNC						0x11
#define PROP_PKT						0x12
#define PROP_MODEM						0x20
#define PROP_PA							0x22
#define PROP_SYNTH						0x23
#define PROP_FREQ_CONTROL				0x40

/* ===== property definitions ===== */
/* global properties */
#define GLOBAL_XO_TUNE					0x00
#define GLOBAL_CLK_CFG					0x01
#define GLOBAL_CONFIG					0x03

/* PROP_INT_CTL / PROP_FRR_CTL properties */
#define INT_CTL_ENABLE					0x00
#define FRR_CTL_A_MODE					0x00

/* preamble properties */
#define PREAMBLE_TX_LENGTH				0x00

/* sync properties */
#define SYNC_CONFIG						0x11

/* modem properties */
#define MODEM_MOD_TYPE					0x00
#define MODEM_DATA_RATE					0x03
#define MODEM_TX_NCO_MOD				0x06
#define MODEM_FREQ_DEV					0x0a
#define MODEM_FREQ_OFFSET				0x0d
#define MODEM_TX_FILTER_COEFF_8 		0x0f
#define MODEM_TX_FILTER_COEFF_7 		0x10
#define MODEM_TX_FILTER_COEFF_6 		0x11
#define MODEM_TX_FILTER_COEFF_5 		0x12
#define MODEM_TX_FILTER_COEFF_4 		0x13
#define MODEM_TX_FILTER_COEFF_3 		0x14
#define MODEM_TX_FILTER_COEFF_2 		0x15
#define MODEM_TX_FILTER_COEFF_1 		0x16
#define MODEM_TX_FILTER_COEFF_0 		0x17

#define MODEM_CLKGEN_BAND				0x51

/* PA properties */
#define PA_BIAS_CLKDUTY					0x02
#define PA_PWR_LVL						0x01

/* synthesizer properties */
#define SYNTH_PFDCP_CPFF				0x00
#define SYNTH_PFDCP_CPINT				0x01
#define SYNTH_VCO_KV					0x02
#define SYNTH_LPFILT3					0x03
#define SYNTH_LPFILT2					0x04
#define SYNTH_LPFILT1					0x05
#define SYNTH_LPFILT0					0x06
#define SYNTH_VCO_KVCAL					0x07

/* frequency control properties */
/* INTE shall be decreased by 1, because FRAC shall be between 1 and 2 */
#define FREQ_CONTROL_INTE				0x00

/* FRAC shall be added to 2**19, to ensure MSB is set! */
#define FREQ_CONTROL_FRAC				0x01
#define FREQ_CONTROL_CHANNEL_STEP_SIZE	0x04
#define FREQ_CONTROL_W_SIZE				0x06

/* ===== command arguments ===== */
/* POWER_UP arguments */
/* byte 1 */
#define PATCH 							(0x01 << 7) /* set patch mode */
#define FUNC							0x01 		/* power on device */

/* byte 2 */
#define TCXO							0x01		/* select if TCXO (1) or crystal (0) is used */

/* GLOBAL_CLK_CFG arguments */
#define DIV_CLK_EN						0x40	/* enable divided clock output */
#define DIV_CLK_SEL_1					0x00	/* divide clock / 1 */
#define DIV_CLK_SEL_2					0x08	/* divide clock / 2 */
#define DIV_CLK_SEL_3					0x10	/* divide clock / 3 */
#define DIV_CLK_SEL_7_5					0x18	/* divide clock / 7.5 */
#define DIV_CLK_SEL_10					0x20	/* divide clock / 10 */
#define DIV_CLK_SEL_15					0x28	/* divide clock / 15 */
#define DIV_CLK_SEL_30					0x30	/* divide clock / 30 */
#define CLK_32K_SEL_XTAL				0x02	/* internal crystal oscillator */
#define CLK_32K_SEL_RC					0x01	/* internal rc oscillator*/
#define CLK_32K_SEL_OFF					0x00	/* 32kHz clock disabled */

/* GPIO_PIN_CFG arguments */
/* bytes 1 .. 6 */
#define PULL_CTL 						0x40 	/* enable or disable pull-up resistor */

/* bytes 1 .. 4 */
#define GPIO_MODE_DONOTHING				0x00	/* pin behaviour is not changed */
#define GPIO_MODE_TRISTATE				0x01	/* input and output drivers are disabled */
#define GPIO_MODE_DRIVE0				0x02	/* CMOS output "low" */
#define GPIO_MODE_DRIVE1				0x03	/* CMOS output "high" */
#define GPIO_MODE_INPUTPIN		        0x04	/* GPIO is input, for TXDATA etc, function is not configured here */
#define GPIO_MODE_32K_CLK				0x05	/* outputs the 32kHz CLK when selected in CLK32_CLK_SEL */
#define GPIO_MODE_BOOT_CLK				0x06	/* outputs boot clock when SPI_ACTIVE */
#define GPIO_MODE_DIV_CLK				0x07	/* outputs divided xtal clk */
#define GPIO_MODE_CTS					0x08	/* output, '1' when device is ready to accept new command */
#define GPIO_MODE_INV_CNT				0x09	/* output, inverted CTS */
#define GPIO_MODE_CMD_OVERLAP			0x0a	/* output, '1' if a command was issued while not ready */
#define GPIO_MODE_SDO					0x0b	/* output, serial data out for SPI */
#define GPIO_MODE_POR					0x0c	/* output, '0' while in POR state */
#define GPIO_MODE_CAL_WUT				0x0d	/* output, '1' on expiration of wake up timer */
#define GPIO_MODE_WUT					0x0e	/* wake up timer output */
#define GPIO_MODE_EN_PA					0x0f	/* output, '1' when PA is enabled */
#define GPIO_MODE_TX_DATA_CLK			0x10	/* data clock output, for TX direct sync mode */
#define GPIO_MODE_TX_DATA				0x11	/* data output from TX FIFO, for debugging purposes */
#define GPIO_MODE_IN_SLEEP				0x12	/* output, '0' when in sleep state */
#define GPIO_MODE_TX_STATE				0x13	/* output, '1' when in TX state */
#define GPIO_MODE_TX_FIFO_EMPTY			0x14	/* output, '1' when FIFO is empty */
#define GPIO_MODE_LOW_BATT				0x15	/* output, '1' if low battery is detected */

/* byte 5 omitted - no IRQ support */
#define NIRQ_MODE_DONOTHING				0x00

/* byte 6 omitted - no SDO reconfiguration support */
#define SDO_MODE_DONOTHING				0x00

/* byte 7 */
#define DRV_STRENGTH_HIGH				(0x00 << 5)
#define DRV_STRENGTH_MED_HIGH			(0x01 << 5)
#define DRV_STRENGTH_MED_LOW			(0x02 << 5)
#define DRV_STRENGTH_LOW				(0x03 << 5)

/* START_TX arguments */
/* byte 2 */
#define START_TX_TXC_STATE_NOCHANGE		(0x00 << 4)
#define START_TX_TXC_STATE_SLEEP		(0x01 << 4)
#define START_TX_TXC_STATE_SPI_ACTIVE	(0x02 << 4)
#define START_TX_TXC_STATE_READY		(0x03 << 4)
#define START_TX_RETRANSMIT_0			(0x00 << 2)	/* send data that has been written to the TX FIFO */
#define START_TX_START_IMM				(0x00 << 0)	/* start transmission immediately */

/* ===== property values ===== */
/* GLOBAL_CONFIG values */
#define GLOBAL_RESERVED			(0x01 << 6) /* shall be set to 1 */
#define POWER_MODE_LOW_POWER	0x00		/* default */
#define POWER_MODE_HIGH_PERF	0x01
#define SEQUENCER_MODE_FAST		(0x00 << 5)	/* default */
#define SEQUENCER_MODE_GUARANT	(0x01 << 5)

/* SYNC_CONFIG values */
#define SYNC_XMIT				(0x00 << 7)	/* default */
#define SYNC_NO_XMIT			(0x01 << 7)

/* MODEM_MOD_TYPE values */
#define MOD_TYPE_CW				0x00
#define MOD_TYPE_OOK			0x01
#define MOD_TYPE_2FSK			0x02		/* default */
#define MOD_TYPE_2GFSK			0x03
#define MOD_TYPE_4FSK			0x04
#define MOD_TYPE_4GFSK			0x05
#define MOD_SOURCE_PACKET		(0x00 << 3)	/* default */
#define MOD_SOURCE_DIRECT		(0x01 << 3)
#define MOD_SOURCE_PSEUDO		(0x02 << 3)
#define MOD_GPIO_0				(0x00 << 5) /* default */
#define MOD_GPIO_1				(0x01 << 5)
#define MOD_GPIO_2				(0x02 << 5)
#define MOD_GPIO_3				(0x03 << 5)
#define MOD_DIRECT_MODE_SYNC	(0x00 << 7) /* default */
#define MOD_DIRECT_MODE_ASYNC	(0x01 << 7)

/* MODEM_TX_NCO_MOD values */
#define MOD_TX_OSR_10		(0x00UL << 26)
#define MOD_TX_OSR_20		(0x02UL << 26)
#define MOD_TX_OSR_40		(0x01UL << 26)

/* MODEM_CLKGEN_BAND values */
#define SY_SEL_0			(0x00 << 3) 	/* low power */
#define SY_SEL_1			(0x01 << 3) 	/* default */
#define FVCO_DIV_4			0x00 			/* default */
#define FVCO_DIV_6			0x01
#define FVCO_DIV_8			0x02 			/* for 70cm ISM band */
#define FVCO_DIV_12			0x03
#define FVCO_DIV_16			0x04
#define FVCO_DIV_24			0x05
#define FVCO_DIV_24_2		0x06
#define FVCO_DIV_24_3		0x07

/* PA_BIAS_CLKDUTY values */
#define PA_BIAS_CLKDUTY_SIN_25	(0x03 << 6) /* for si4060  */


#endif /* INC_SI4063_H_ */
