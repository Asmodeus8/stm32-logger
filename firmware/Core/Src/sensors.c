#include "app.h"
#include "bme280.h"
#include "board.h"
#include "lm35.h"
#include "task.h"

#define LM35_SAMPLES 16u
#define LM35_MAX_CENTI 15000 /* the LM35 tops out at 150 degC; anything above is a wiring fault */

static int i2c_read(void *ctx, uint8_t reg, uint8_t *buf, size_t len)
{
    (void)ctx;
    return HAL_I2C_Mem_Read(&hi2c1, BME280_ADDR_LOW << 1, reg, I2C_MEMADD_SIZE_8BIT, buf, (uint16_t)len, 20) == HAL_OK ? 0 : -1;
}

static int i2c_write(void *ctx, uint8_t reg, uint8_t value)
{
    (void)ctx;
    return HAL_I2C_Mem_Write(&hi2c1, BME280_ADDR_LOW << 1, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 20) == HAL_OK ? 0 : -1;
}

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static int adc_read(uint32_t channel, uint16_t *out)
{
    /* 480 cycles at 25 MHz is 19 us: enough for VREFINT (10 us minimum) and the LM35's output. */
    ADC_ChannelConfTypeDef c = {.Channel = channel, .Rank = 1, .SamplingTime = ADC_SAMPLETIME_480CYCLES};
    if (HAL_ADC_ConfigChannel(&hadc1, &c) != HAL_OK || HAL_ADC_Start(&hadc1) != HAL_OK) {
        return -1;
    }
    int ok = HAL_ADC_PollForConversion(&hadc1, 5) == HAL_OK;
    *out = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return ok ? 0 : -1;
}

static int read_lm35(int32_t *centi)
{
    uint16_t vref;
    if (adc_read(ADC_CHANNEL_VREFINT, &vref) != 0) {
        return -1;
    }
    uint32_t vdda = lm35_vdda_mv(vref, *VREFINT_CAL_PTR, VREFINT_CAL_MV);

    uint32_t sum = 0;
    for (uint32_t i = 0; i < LM35_SAMPLES; i++) {
        uint16_t v;
        if (adc_read(LM35_ADC_CHANNEL, &v) != 0) {
            return -1;
        }
        sum += v;
    }
    *centi = lm35_temp_centi(sum, LM35_SAMPLES, vdda);
    return *centi > LM35_MAX_CENTI ? -1 : 0;
}

static void publish(const sample_t *s)
{
    if (xQueueSend(log_queue, s, 0) != pdPASS) {
        stats.rows_dropped++;
    }
    /* The ESP32 only needs recent readings: if it falls behind, drop the oldest. */
    if (xQueueSend(uplink_queue, s, 0) != pdPASS) {
        sample_t old;
        xQueueReceive(uplink_queue, &old, 0);
        xQueueSend(uplink_queue, s, 0);
    }
}

void sensors_task(void *arg)
{
    (void)arg;
    bme280_t bme = {.bus = {.read = i2c_read, .write = i2c_write, .delay_ms = delay_ms}};
    bool bme_ready = false;
    uint32_t seq = 0;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        sample_t s = {.seq = ++seq, .epoch = rtc_get_epoch()};
        if (!rtc_is_set()) {
            s.flags |= SAMPLE_TIME_UNSET;
        }

        if (!bme_ready) {
            bme_ready = bme280_init(&bme) == 0;
        }
        if (!bme_ready || bme280_measure(&bme, &s.bme_temp_centi, &s.pressure_pa, &s.humidity_centi) != 0) {
            /* Re-probe next time: covers a sensor that was unplugged or browned out. */
            bme_ready = false;
            s.flags |= SAMPLE_BME_ERROR;
            stats.bme_errors++;
        }

        if (read_lm35(&s.lm35_temp_centi) != 0) {
            s.flags |= SAMPLE_ADC_ERROR;
        }

        publish(&s);
        stats.samples++;
        xEventGroupSetBits(alive, ALIVE_SENSORS);
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}
