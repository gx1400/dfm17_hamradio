/**
  ******************************************************************************
  * @file    analog.c
  * @brief   Supply voltage/current and temperature from ADC1
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 Derek Rowland <gx1400@gmail.com>
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
  * ADC1 is driven through its registers: the project's CubeMX configuration has
  * no ADC, so there is no HAL ADC driver or MX_ADC1_Init(). If ADC1 is ever
  * enabled in CubeMX, move this onto the HAL instead.
  *
  * Channels: PA0 = ADC_IN0 (supply input via divider), PB1 = ADC_IN9 (supply
  * current monitor), PC2 = ADC_IN12 (USB VBUS via divider), 16 = temperature
  * sensor, 17 = VREFINT. Every reading is
  * scaled by VDDA measured from VREFINT, so it doesn't assume a 3.3 V rail.
  ******************************************************************************
  */

#include "analog.h"
#include "main.h"

#define CH_VIN			0
#define CH_IIN			9
#define CH_USB			12
#define CH_TEMP			16
#define CH_VREFINT		17

#define VREFINT_MV		1200	/* typical, 1.16-1.24 V on the STM32F100 */
#define TEMP_V25_MV		1410	/* sensor voltage at 25 C, typical */
#define TEMP_SLOPE_UV	4300	/* uV per C, typical */
#define SAMPLES			16

analog_reading analog_last;

void analog_init(void) {
	/* PCLK2 24 MHz / 2 = 12 MHz, the STM32F100 maximum */
	MODIFY_REG(RCC->CFGR, RCC_CFGR_ADCPRE, RCC_CFGR_ADCPRE_DIV2);
	SET_BIT(RCC->APB2ENR, RCC_APB2ENR_ADC1EN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN);
	/* PA0, PB1 and PC2 analog (CNF/MODE 0000) */
	SET_BIT(RCC->APB2ENR, RCC_APB2ENR_IOPCEN);
	MODIFY_REG(GPIOA->CRL, 0xFUL << 0, 0);
	MODIFY_REG(GPIOB->CRL, 0xFUL << 4, 0);
	MODIFY_REG(GPIOC->CRL, 0xFUL << 8, 0);

	/* longest sample time (239.5 cycles) for the high-impedance inputs */
	ADC1->SMPR1 = 0x00FFFFFF;
	ADC1->SMPR2 = 0x3FFFFFFF;
	/* software-triggered single conversions, temp sensor and VREFINT on */
	ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_TSVREFE | ADC_CR2_EXTTRIG | ADC_CR2_EXTSEL;
	HAL_Delay(2);
	SET_BIT(ADC1->CR2, ADC_CR2_RSTCAL);
	while (READ_BIT(ADC1->CR2, ADC_CR2_RSTCAL))
		;
	SET_BIT(ADC1->CR2, ADC_CR2_CAL);
	while (READ_BIT(ADC1->CR2, ADC_CR2_CAL))
		;
}

/* average of SAMPLES conversions, raw 12-bit */
static uint32_t read_raw(uint8_t ch) {
	uint32_t sum = 0;
	ADC1->SQR1 = 0;
	ADC1->SQR3 = ch;
	for (int i = 0; i < SAMPLES; i++) {
		SET_BIT(ADC1->CR2, ADC_CR2_SWSTART);
		while (!READ_BIT(ADC1->SR, ADC_SR_EOC))
			;
		sum += ADC1->DR;
	}
	return sum / SAMPLES;
}

/*
 * analog_read
 *
 * fills r with the supply voltage and current, VDDA and the MCU temperature
 */
void analog_read(analog_reading *r) {
	uint32_t vref = read_raw(CH_VREFINT);
	uint32_t vin = read_raw(CH_VIN);
	uint32_t iin = read_raw(CH_IIN);
	uint32_t temp = read_raw(CH_TEMP);

	analog_convert(r, vref, vin, iin, temp, read_raw(CH_USB));
}

/*
 * analog_convert
 *
 * converts raw 12-bit averages to the values in r, scaling by VDDA measured
 * from VREFINT
 */
void analog_convert(analog_reading *r, uint32_t vref, uint32_t vin_raw,
		uint32_t iin_raw, uint32_t temp_raw, uint32_t usb_raw) {
	if (vref == 0)
		vref = 1;
	uint32_t vdda = VREFINT_MV * 4095UL / vref;
	r->vref_raw = vref;
	r->vin_raw = vin_raw;
	r->iin_raw = iin_raw;
	uint32_t vin_pin = r->vin_raw * vdda / 4095;
	uint32_t iin_pin = r->iin_raw * vdda / 4095;
	int32_t temp_pin = temp_raw * vdda / 4095;

	r->vdda_mv = vdda;
	r->vin_mv = (uint16_t)(vin_pin * ANALOG_VIN_DIVIDER);
	r->iin_ma = (uint16_t)(iin_pin * ANALOG_I_MA_PER_V / 1000.0f);
	r->temp_c = (int16_t)(25 + (TEMP_V25_MV - temp_pin) * 1000 / TEMP_SLOPE_UV);

	uint32_t usb_pin = usb_raw * vdda / 4095;
	r->usb_raw = usb_raw;
	r->usb_mv = (uint16_t)(usb_pin * ANALOG_USB_DIVIDER);
	r->on_usb = usb_pin > ANALOG_USB_PRESENT_MV;
}
