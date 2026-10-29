/* host stub: replaces Core/Inc/si4063.h */
#ifndef STUB_SI4063_H_
#define STUB_SI4063_H_
#include <inttypes.h>
#define MOD_TYPE_2FSK 0x02
void si4060_setup(uint8_t mod_type);
void si4060_start_tx(uint8_t channel);
void si4060_stop_tx(void);
#endif
