/* host stub: replaces Core/Inc/analog.h; aprs_harness.c supplies analog_read() */
#ifndef STUB_ANALOG_H_
#define STUB_ANALOG_H_
#include <inttypes.h>
typedef struct {
	uint16_t vin_raw, iin_raw, vref_raw;
	uint16_t vdda_mv;
	uint16_t vin_mv;
	uint16_t iin_ma;
	int16_t temp_c;
	uint16_t usb_raw;
	uint16_t usb_mv;
	uint8_t on_usb;
} analog_reading;
void analog_read(analog_reading *r);
#endif
