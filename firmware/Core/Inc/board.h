#ifndef BOARD_H
#define BOARD_H

#include "stm32f4xx_hal.h"

/* WeAct Black Pill, STM32F411CEU6, 25 MHz HSE, 32.768 kHz LSE.
 *
 *   PB6  I2C1 SCL  -> BME280 SCK      PA5  SPI1 SCK  -> microSD CLK
 *   PB7  I2C1 SDA  -> BME280 SDI      PA6  SPI1 MISO -> microSD DO
 *   PA0  ADC1 IN0  -> LM35 VOUT       PA7  SPI1 MOSI -> microSD DI
 *   PA9  USART1 TX -> ESP32 RX        PA4  GPIO      -> microSD CS
 *   PA10 USART1 RX <- ESP32 TX        PC13 GPIO      -> on-board LED (active low)
 */

#define LED_PORT  GPIOC
#define LED_PIN   GPIO_PIN_13
#define SD_CS_PORT GPIOA
#define SD_CS_PIN  GPIO_PIN_4
#define LM35_ADC_CHANNEL ADC_CHANNEL_0

#define UPLINK_BAUD 115200u

/* VREFINT calibration value, measured by ST at VDDA = 3.3 V. */
#define VREFINT_CAL_PTR ((const uint16_t *)0x1FFF7A2AU)
#define VREFINT_CAL_MV  3300u

extern I2C_HandleTypeDef hi2c1;
extern ADC_HandleTypeDef hadc1;
extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart1;
extern RTC_HandleTypeDef hrtc;
extern IWDG_HandleTypeDef hiwdg;

void Error_Handler(void);

#endif
