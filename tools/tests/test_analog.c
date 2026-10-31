/*
 * test_analog.c - conversion of the ADC readings to supply voltage, current,
 * VDDA and temperature (analog_convert() in analog.c)
 */
#include "test.h"
#include "fake_hal.h"
#include "analog.h"

/* raw 12-bit count of a pin voltage at a given VDDA */
static uint32_t counts(double mv, double vdda_mv) {
	return (uint32_t)(mv / vdda_mv * 4095.0 + 0.5);
}

static void vdda_from_vrefint(void) {
	analog_reading r;
	/* VREFINT (1.20 V) reads 1489 counts with a 3.30 V supply */
	analog_convert(&r, counts(1200, 3300), 0, 0, counts(1410, 3300), 0);
	CHECK_NEAR(r.vdda_mv, 3300, 3);
	analog_convert(&r, counts(1200, 3000), 0, 0, counts(1410, 3000), 0);
	CHECK_NEAR(r.vdda_mv, 3000, 3);
}

static void supply_voltage_and_current(void) {
	analog_reading r;
	/* survey values on USB: 2.33 V on PA0, 54 mV on PB1 (54 mA) */
	analog_convert(&r, counts(1200, 3300), counts(2330, 3300), counts(54, 3300), counts(1410, 3300), 0);
	CHECK_NEAR(r.vin_mv, 2330 * ANALOG_VIN_DIVIDER, 6);
	CHECK_NEAR(r.iin_ma, 54 * ANALOG_I_MA_PER_V / 1000, 2);
	CHECK_EQ(r.vin_raw, counts(2330, 3300));
	CHECK_EQ(r.iin_raw, counts(54, 3300));
}

static void readings_independent_of_vdda(void) {
	/* the same pin voltages read with a different supply give the same result */
	analog_reading a, b;
	analog_convert(&a, counts(1200, 3300), counts(2330, 3300), counts(54, 3300), counts(1410, 3300), 0);
	analog_convert(&b, counts(1200, 2900), counts(2330, 2900), counts(54, 2900), counts(1410, 2900), 0);
	CHECK_NEAR(a.vin_mv, b.vin_mv, 8);
	CHECK_NEAR(a.iin_ma, b.iin_ma, 2);
}

static void temperature(void) {
	analog_reading r;
	analog_convert(&r, counts(1200, 3300), 0, 0, counts(1410, 3300), 0);
	CHECK_NEAR(r.temp_c, 25, 1);
	/* the sensor falls 4.3 mV/C */
	analog_convert(&r, counts(1200, 3300), 0, 0, counts(1410 - 43, 3300), 0);
	CHECK_NEAR(r.temp_c, 35, 1);
	analog_convert(&r, counts(1200, 3300), 0, 0, counts(1410 + 129, 3300), 0);
	CHECK_NEAR(r.temp_c, -5, 1);
}

static void vrefint_zero_does_not_divide_by_zero(void) {
	analog_reading r;
	analog_convert(&r, 0, 100, 100, 100, 0);
	CHECK_EQ(r.vref_raw, 1);
}

static void usb_presence_and_voltage(void) {
	analog_reading r;
	/* 5.0 V VBUS through the (placeholder) divider */
	uint32_t pin = (uint32_t)(5000 / ANALOG_USB_DIVIDER);
	analog_convert(&r, counts(1200, 3300), 0, 0, counts(1410, 3300), counts(pin, 3300));
	CHECK_EQ(r.on_usb, 1);
	CHECK_NEAR(r.usb_mv, 5000, 8);
	CHECK_EQ(r.usb_raw, counts(pin, 3300));
	/* on battery the pin floats near 0 through the divider */
	analog_convert(&r, counts(1200, 3300), 0, 0, counts(1410, 3300), counts(30, 3300));
	CHECK_EQ(r.on_usb, 0);
	/* the threshold is on the pin voltage, independent of the divider */
	analog_convert(&r, counts(1200, 3300), 0, 0, counts(1410, 3300),
			counts(ANALOG_USB_PRESENT_MV + 20, 3300));
	CHECK_EQ(r.on_usb, 1);
	analog_convert(&r, counts(1200, 3300), 0, 0, counts(1410, 3300),
			counts(ANALOG_USB_PRESENT_MV - 20, 3300));
	CHECK_EQ(r.on_usb, 0);
}

/* analog_read() through the fake ADC: every conversion is ready at once and
 * reads the same count, so each channel's 16-sample average is that count */
static void read_averages_each_channel(void) {
	analog_reading r;
	fake_adc1.SR = ADC_SR_EOC;
	fake_adc1.DR = 1489; /* VREFINT at 3.3 V */
	fake_adc1.CR2 = 0;
	analog_read(&r);
	CHECK(fake_adc1.CR2 & ADC_CR2_SWSTART);
	CHECK_EQ(fake_adc1.SQR3, 12); /* PC2, the last channel read */
	CHECK_EQ(fake_adc1.SQR1, 0);  /* one conversion per start */
	CHECK_EQ(r.vref_raw, 1489);
	CHECK_EQ(r.vin_raw, 1489);
	CHECK_EQ(r.iin_raw, 1489);
	CHECK_EQ(r.usb_raw, 1489);
	CHECK_NEAR(r.vdda_mv, 3300, 3);
}

TEST_MAIN("analog", TEST(vdda_from_vrefint), TEST(read_averages_each_channel),
		TEST(supply_voltage_and_current), TEST(readings_independent_of_vdda), TEST(temperature),
		TEST(vrefint_zero_does_not_divide_by_zero), TEST(usb_presence_and_voltage))
