/*
 * fake_hal.c - register blocks and a millisecond clock for host tests.
 *
 * HAL_Delay() advances the clock instead of waiting, and calls
 * fake_delay_hook (if set) first, so tests can watch what the code under test
 * does during a delay.
 */
#include "fake_hal.h"

GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc;
SPI_TypeDef fake_spi1 = { .SR = SPI_SR_TXE | SPI_SR_RXNE };
ADC_TypeDef fake_adc1;
RCC_TypeDef fake_rcc;
DWT_Type fake_dwt;
CoreDebug_Type fake_coredebug;

uint32_t fake_tick;
uint32_t fake_tick_step;
void (*fake_delay_hook)(uint32_t ms);

uint32_t HAL_GetTick(void) {
	uint32_t t = fake_tick;
	fake_tick += fake_tick_step;
	return t;
}

void HAL_Delay(uint32_t ms) {
	if (fake_delay_hook)
		fake_delay_hook(ms);
	fake_tick += ms;
}
