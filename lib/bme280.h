#ifndef BME280_H
#define BME280_H

#include <stdint.h>
#include <stddef.h>

#define BME280_ADDR_LOW   0x76
#define BME280_ADDR_HIGH  0x77
#define BME280_CHIP_ID    0x60

#define BME280_REG_CALIB00   0x88  /* 26 bytes: T1..P9, H1 */
#define BME280_REG_ID        0xD0
#define BME280_REG_RESET     0xE0
#define BME280_REG_CALIB26   0xE1  /* 7 bytes: H2..H6 */
#define BME280_REG_CTRL_HUM  0xF2
#define BME280_REG_STATUS    0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG    0xF5
#define BME280_REG_DATA      0xF7  /* 8 bytes: press, temp, hum */

typedef struct {
    uint16_t t1; int16_t t2, t3;
    uint16_t p1; int16_t p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t  h1; int16_t h2; uint8_t h3; int16_t h4, h5; int8_t h6;
} bme280_calib_t;

typedef struct {
    int32_t  adc_t, adc_p, adc_h;
} bme280_raw_t;

/* Register access supplied by the platform (I2C on the target, a fake in tests). */
typedef struct {
    int (*read)(void *ctx, uint8_t reg, uint8_t *buf, size_t len);
    int (*write)(void *ctx, uint8_t reg, uint8_t value);
    void (*delay_ms)(uint32_t ms);
    void *ctx;
} bme280_bus_t;

typedef struct {
    bme280_bus_t bus;
    bme280_calib_t calib;
} bme280_t;

void bme280_parse_calib(const uint8_t c1[26], const uint8_t c2[7], bme280_calib_t *out);
void bme280_parse_raw(const uint8_t data[8], bme280_raw_t *out);

/* Bosch reference integer compensation.
 * Temperature in 0.01 degC, pressure in Pa, humidity in 0.01 %RH. */
int32_t  bme280_temp_centi(const bme280_calib_t *c, int32_t adc_t, int32_t *t_fine);
uint32_t bme280_pressure_pa(const bme280_calib_t *c, int32_t adc_p, int32_t t_fine);
uint32_t bme280_humidity_centi(const bme280_calib_t *c, int32_t adc_h, int32_t t_fine);

/* Device level: probe, load calibration, and take one forced-mode measurement. */
int bme280_init(bme280_t *dev);
int bme280_measure(bme280_t *dev, int32_t *temp_centi, uint32_t *pressure_pa, uint32_t *humidity_centi);

#endif
