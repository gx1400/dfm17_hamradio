/* host stub: replaces Core/Inc/si4063.h */
#ifndef STUB_SI4063_H_
#define STUB_SI4063_H_
#include <inttypes.h>
#define MOD_TYPE_2FSK 0x02
void si4060_setup(uint8_t mod_type);
void si4060_start_tx(uint8_t channel);
void si4060_stop_tx(void);
#define RF_DEV_HZ_DFM 2600UL
void si4060_set_deviation(uint32_t dev_hz);
void si4060_set_offset(uint16_t offset);
int32_t si4060_offset_units(int32_t hz);
void si4060_spi_fast(uint8_t on);
uint8_t si4060_set_offset_fast(int16_t offset);
#endif
