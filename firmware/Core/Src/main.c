#include "app.h"
#include "board.h"
#include "task.h"

I2C_HandleTypeDef hi2c1;
ADC_HandleTypeDef hadc1;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart1;
RTC_HandleTypeDef hrtc;
IWDG_HandleTypeDef hiwdg;

QueueHandle_t log_queue;
QueueHandle_t uplink_queue;
EventGroupHandle_t alive;
app_stats_t stats;

static bool lse_ok;

/* HSE 25 MHz -> PLL -> 100 MHz SYSCLK, APB1 50 MHz, APB2 100 MHz. */
static void clock_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    HAL_PWR_EnableBkUpAccess();

    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    osc.HSEState = RCC_HSE_ON;
    osc.LSIState = RCC_LSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 25;
    osc.PLL.PLLN = 200;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) {
        Error_Handler();
    }

    /* The LSE crystal doesn't start on every Black Pill; fall back to LSI so the RTC still runs. */
    RCC_OscInitTypeDef lse = {0};
    lse.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    lse.LSEState = RCC_LSE_ON;
    lse.PLL.PLLState = RCC_PLL_NONE;
    lse_ok = HAL_RCC_OscConfig(&lse) == HAL_OK;

    RCC_PeriphCLKInitTypeDef periph = {0};
    periph.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    periph.RTCClockSelection = lse_ok ? RCC_RTCCLKSOURCE_LSE : RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK) {
        Error_Handler();
    }
}

static void gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);

    GPIO_InitTypeDef g = {.Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW};
    g.Pin = LED_PIN;
    HAL_GPIO_Init(LED_PORT, &g);
    g.Pin = SD_CS_PIN;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SD_CS_PORT, &g);
}

static void i2c_init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
}

static void adc_init(void)
{
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4; /* 25 MHz */
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }
}

static void spi_init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256; /* 390 kHz for card init */
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        Error_Handler();
    }
}

static void uart_init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = UPLINK_BAUD;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
}

static void rtc_init(void)
{
    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = lse_ok ? 255 : 249; /* 32768 Hz or 32 kHz down to 1 Hz */
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        Error_Handler();
    }
}

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *h)
{
    if (h->Instance != I2C1) return;
    GPIO_InitTypeDef g = {.Pin = GPIO_PIN_6 | GPIO_PIN_7, .Mode = GPIO_MODE_AF_OD, .Pull = GPIO_PULLUP,
                          .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = GPIO_AF4_I2C1};
    HAL_GPIO_Init(GPIOB, &g);
    __HAL_RCC_I2C1_CLK_ENABLE();
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *h)
{
    if (h->Instance != ADC1) return;
    __HAL_RCC_ADC1_CLK_ENABLE();
    GPIO_InitTypeDef g = {.Pin = GPIO_PIN_0, .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL};
    HAL_GPIO_Init(GPIOA, &g);
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *h)
{
    if (h->Instance != SPI1) return;
    __HAL_RCC_SPI1_CLK_ENABLE();
    GPIO_InitTypeDef g = {.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP,
                          .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = GPIO_AF5_SPI1};
    HAL_GPIO_Init(GPIOA, &g);
}

void HAL_UART_MspInit(UART_HandleTypeDef *h)
{
    if (h->Instance != USART1) return;
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef g = {.Pin = GPIO_PIN_9 | GPIO_PIN_10, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP,
                          .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = GPIO_AF7_USART1};
    HAL_GPIO_Init(GPIOA, &g);
    /* Above configMAX_SYSCALL priority (numerically) so the ISR may use FreeRTOS FromISR calls. */
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void HAL_RTC_MspInit(RTC_HandleTypeDef *h)
{
    (void)h;
    __HAL_RCC_RTC_ENABLE();
}

int main(void)
{
    HAL_Init();
    clock_init();

    stats.watchdog_reset = __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET;
    __HAL_RCC_CLEAR_RESET_FLAGS();

    gpio_init();
    i2c_init();
    adc_init();
    spi_init();
    uart_init();
    rtc_init();
    rtc_time_init();

    log_queue = xQueueCreate(LOG_QUEUE_LEN, sizeof(sample_t));
    uplink_queue = xQueueCreate(UPLINK_QUEUE_LEN, sizeof(sample_t));
    alive = xEventGroupCreate();
    configASSERT(log_queue && uplink_queue && alive);

    /* The supervisor outranks the workers so a stuck task can't starve the watchdog check. */
    configASSERT(xTaskCreate(supervisor_task, "super", 256, NULL, 5, NULL) == pdPASS);
    configASSERT(xTaskCreate(sensors_task, "sensors", 512, NULL, 4, NULL) == pdPASS);
    configASSERT(xTaskCreate(uplink_task, "uplink", 512, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(logger_task, "logger", 1280, NULL, 2, NULL) == pdPASS);

    vTaskStartScheduler();
    Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;) {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        for (volatile uint32_t i = 0; i < 2000000; i++) {
        }
    }
}

void vAssertCalled(const char *file, int line)
{
    (void)file;
    (void)line;
    Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    Error_Handler();
}

void vApplicationMallocFailedHook(void)
{
    Error_Handler();
}
