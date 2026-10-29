/*
 * stm32f1xx_hal.h - a stand-in for the STM32 HAL, for building firmware
 * sources on a PC (tools/tests).
 *
 * It provides the types, register blocks and macros the sources use. The
 * register blocks are plain memory, so code that writes a register and reads
 * it back sees its own value; status bits that firmware busy-waits on are set
 * by the tests (see fake_hal.c). Functions are declared here and implemented
 * by fake_hal.c or by the individual test.
 */
#ifndef FAKE_STM32F1XX_HAL_H_
#define FAKE_STM32F1XX_HAL_H_

#include <stdint.h>
#include <stddef.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;

#define GPIO_PIN_0	0x0001U
#define GPIO_PIN_1	0x0002U
#define GPIO_PIN_2	0x0004U
#define GPIO_PIN_3	0x0008U
#define GPIO_PIN_4	0x0010U
#define GPIO_PIN_5	0x0020U
#define GPIO_PIN_6	0x0040U
#define GPIO_PIN_7	0x0080U
#define GPIO_PIN_8	0x0100U
#define GPIO_PIN_9	0x0200U
#define GPIO_PIN_10	0x0400U
#define GPIO_PIN_11	0x0800U
#define GPIO_PIN_12	0x1000U

typedef struct { volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR; } GPIO_TypeDef;
typedef struct { volatile uint32_t CR1, CR2, SR, DR, CRCPR, RXCRCR, TXCRCR; } SPI_TypeDef;
typedef struct {
	volatile uint32_t SR, CR1, CR2, SMPR1, SMPR2, JOFR[4], HTR, LTR;
	volatile uint32_t SQR1, SQR2, SQR3, JSQR, JDR[4], DR;
} ADC_TypeDef;
typedef struct { volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR, AHBENR, APB2ENR; } RCC_TypeDef;
typedef struct { volatile uint32_t CTRL, CYCCNT; } DWT_Type;
typedef struct { volatile uint32_t DEMCR; } CoreDebug_Type;

extern GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc;
extern SPI_TypeDef fake_spi1;
extern ADC_TypeDef fake_adc1;
extern RCC_TypeDef fake_rcc;
extern DWT_Type fake_dwt;
extern CoreDebug_Type fake_coredebug;

#define GPIOA		(&fake_gpioa)
#define GPIOB		(&fake_gpiob)
#define GPIOC		(&fake_gpioc)
#define SPI1		(&fake_spi1)
#define ADC1		(&fake_adc1)
#define RCC			(&fake_rcc)
#define DWT			(&fake_dwt)
#define CoreDebug	(&fake_coredebug)

#define SET_BIT(REG, BIT)		((REG) |= (BIT))
#define CLEAR_BIT(REG, BIT)		((REG) &= ~(BIT))
#define READ_BIT(REG, BIT)		((REG) & (BIT))
#define WRITE_REG(REG, VAL)		((REG) = (VAL))
#define READ_REG(REG)			((REG))
#define MODIFY_REG(REG, CLEARMASK, SETMASK)	WRITE_REG((REG), (((READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))

#define SPI_SR_RXNE					0x0001U
#define SPI_SR_TXE					0x0002U
#define SPI_SR_BSY					0x0080U
#define SPI_CR1_SPE					0x0040U
#define SPI_CR1_BR					0x0038U
#define SPI_BAUDRATEPRESCALER_4		0x0008U
#define SPI_BAUDRATEPRESCALER_32	0x0020U

#define ADC_SR_EOC					0x0002U
#define ADC_CR2_ADON				0x00000001U
#define ADC_CR2_CAL					0x00000004U
#define ADC_CR2_RSTCAL				0x00000008U
#define ADC_CR2_EXTSEL				0x000E0000U
#define ADC_CR2_EXTTRIG				0x00100000U
#define ADC_CR2_SWSTART				0x00400000U
#define ADC_CR2_TSVREFE				0x00800000U
#define RCC_CFGR_SWS				0x0000000CU
#define RCC_CFGR_SWS_HSI			0x00000000U
#define RCC_CFGR_SWS_PLL			0x00000008U
#define RCC_CFGR_PLLSRC				0x00010000U
#define RCC_CFGR_ADCPRE				0x0000C000U
#define RCC_CFGR_ADCPRE_DIV2		0x00000000U
#define RCC_APB2ENR_IOPAEN			0x00000004U
#define RCC_APB2ENR_IOPBEN			0x00000008U
#define RCC_APB2ENR_IOPCEN			0x00000010U
#define RCC_APB2ENR_ADC1EN			0x00000200U
#define CoreDebug_DEMCR_TRCENA_Msk	0x01000000U
#define DWT_CTRL_CYCCNTENA_Msk		0x00000001U

typedef enum { EXTI9_5_IRQn = 23 } IRQn_Type;

typedef struct { uint32_t Instance; } DMA_HandleTypeDef;
typedef struct {
	void *Instance;
	volatile uint32_t ErrorCode;
	DMA_HandleTypeDef *hdmarx;
} UART_HandleTypeDef;
typedef struct { void *Instance; } TIM_HandleTypeDef;
typedef struct { void *Instance; } SPI_HandleTypeDef;

#define __HAL_DMA_GET_COUNTER(h)	(0U)

static inline void __disable_irq(void) {}
static inline void __enable_irq(void) {}
static inline uint32_t ITM_SendChar(uint32_t ch) { return ch; }

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *huart, uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *huart);

#endif /* FAKE_STM32F1XX_HAL_H_ */
