/* fake_hal.h - test-side controls for fake_hal.c */
#ifndef FAKE_HAL_H_
#define FAKE_HAL_H_

#include "stm32f1xx_hal.h"

/* the HAL millisecond tick, set or advanced by tests */
extern uint32_t fake_tick;
/* added to the tick on every HAL_GetTick(), for code that busy-waits on it */
extern uint32_t fake_tick_step;
/* called by HAL_Delay(ms) before the clock advances */
extern void (*fake_delay_hook)(uint32_t ms);

#endif /* FAKE_HAL_H_ */
