#include "bme280.h"

static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t  s16le(const uint8_t *p) { return (int16_t)u16le(p); }

void bme280_parse_calib(const uint8_t c1[26], const uint8_t c2[7], bme280_calib_t *c)
{
    c->t1 = u16le(&c1[0]);  c->t2 = s16le(&c1[2]);  c->t3 = s16le(&c1[4]);
    c->p1 = u16le(&c1[6]);  c->p2 = s16le(&c1[8]);  c->p3 = s16le(&c1[10]);
    c->p4 = s16le(&c1[12]); c->p5 = s16le(&c1[14]); c->p6 = s16le(&c1[16]);
    c->p7 = s16le(&c1[18]); c->p8 = s16le(&c1[20]); c->p9 = s16le(&c1[22]);
    c->h1 = c1[25];
    c->h2 = s16le(&c2[0]);
    c->h3 = c2[2];
    /* H4 and H5 are 12-bit values sharing the nibbles of 0xE5. */
    c->h4 = (int16_t)((int8_t)c2[3] * 16 + (c2[4] & 0x0F));
    c->h5 = (int16_t)((int8_t)c2[5] * 16 + (c2[4] >> 4));
    c->h6 = (int8_t)c2[6];
}

void bme280_parse_raw(const uint8_t d[8], bme280_raw_t *r)
{
    r->adc_p = (int32_t)(((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4));
    r->adc_t = (int32_t)(((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4));
    r->adc_h = (int32_t)(((uint32_t)d[6] << 8) | d[7]);
}

int32_t bme280_temp_centi(const bme280_calib_t *c, int32_t adc_t, int32_t *t_fine)
{
    int32_t v1 = ((((adc_t >> 3) - ((int32_t)c->t1 << 1))) * (int32_t)c->t2) >> 11;
    int32_t v2 = (((((adc_t >> 4) - (int32_t)c->t1) * ((adc_t >> 4) - (int32_t)c->t1)) >> 12) * (int32_t)c->t3) >> 14;
    *t_fine = v1 + v2;
    return (*t_fine * 5 + 128) >> 8;
}

uint32_t bme280_pressure_pa(const bme280_calib_t *c, int32_t adc_p, int32_t t_fine)
{
    int64_t v1 = (int64_t)t_fine - 128000;
    int64_t v2 = v1 * v1 * (int64_t)c->p6;
    v2 += v1 * (int64_t)c->p5 * ((int64_t)1 << 17);
    v2 += (int64_t)c->p4 * ((int64_t)1 << 35);
    v1 = ((v1 * v1 * (int64_t)c->p3) >> 8) + v1 * (int64_t)c->p2 * ((int64_t)1 << 12);
    v1 = (((int64_t)1 << 47) + v1) * (int64_t)c->p1 >> 33;
    if (v1 == 0) {
        return 0;
    }
    int64_t p = 1048576 - adc_p;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = ((int64_t)c->p9 * (p >> 13) * (p >> 13)) >> 25;
    v2 = ((int64_t)c->p8 * p) >> 19;
    p = ((p + v1 + v2) >> 8) + (int64_t)c->p7 * 16;
    return (uint32_t)(p >> 8); /* Q24.8 -> Pa */
}

uint32_t bme280_humidity_centi(const bme280_calib_t *c, int32_t adc_h, int32_t t_fine)
{
    int32_t v = t_fine - 76800;
    v = (((((adc_h << 14) - (int32_t)c->h4 * (1 << 20) - ((int32_t)c->h5 * v)) + 16384) >> 15) *
         (((((((v * (int32_t)c->h6) >> 10) * (((v * (int32_t)c->h3) >> 11) + 32768)) >> 10) + 2097152) *
               (int32_t)c->h2 + 8192) >> 14));
    v -= (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)c->h1) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;
    uint32_t q22_10 = (uint32_t)(v >> 12); /* %RH * 1024 */
    return (q22_10 * 100u + 512u) >> 10;
}

int bme280_init(bme280_t *dev)
{
    uint8_t id = 0;
    if (dev->bus.read(dev->bus.ctx, BME280_REG_ID, &id, 1) != 0 || id != BME280_CHIP_ID) {
        return -1;
    }
    if (dev->bus.write(dev->bus.ctx, BME280_REG_RESET, 0xB6) != 0) {
        return -1;
    }
    dev->bus.delay_ms(5);

    uint8_t c1[26], c2[7];
    if (dev->bus.read(dev->bus.ctx, BME280_REG_CALIB00, c1, sizeof c1) != 0 ||
        dev->bus.read(dev->bus.ctx, BME280_REG_CALIB26, c2, sizeof c2) != 0) {
        return -1;
    }
    bme280_parse_calib(c1, c2, &dev->calib);

    /* Weather-station settings from the datasheet: x1 oversampling, filter off, forced mode. */
    if (dev->bus.write(dev->bus.ctx, BME280_REG_CTRL_HUM, 0x01) != 0 ||
        dev->bus.write(dev->bus.ctx, BME280_REG_CONFIG, 0x00) != 0) {
        return -1;
    }
    return 0;
}

int bme280_measure(bme280_t *dev, int32_t *temp_centi, uint32_t *pressure_pa, uint32_t *humidity_centi)
{
    /* osrs_t=1, osrs_p=1, mode=forced. ctrl_hum only takes effect after a ctrl_meas write. */
    if (dev->bus.write(dev->bus.ctx, BME280_REG_CTRL_MEAS, (1 << 5) | (1 << 2) | 0x01) != 0) {
        return -1;
    }
    uint8_t status = 0;
    for (int i = 0; i < 20; i++) {
        dev->bus.delay_ms(2);
        if (dev->bus.read(dev->bus.ctx, BME280_REG_STATUS, &status, 1) != 0) {
            return -1;
        }
        if ((status & 0x08) == 0) {
            break;
        }
    }
    if (status & 0x08) {
        return -1;
    }

    uint8_t d[8];
    if (dev->bus.read(dev->bus.ctx, BME280_REG_DATA, d, sizeof d) != 0) {
        return -1;
    }
    bme280_raw_t raw;
    bme280_parse_raw(d, &raw);
    if (raw.adc_t == 0x80000) { /* "skipped" value: no conversion happened */
        return -1;
    }
    int32_t t_fine;
    *temp_centi = bme280_temp_centi(&dev->calib, raw.adc_t, &t_fine);
    *pressure_pa = bme280_pressure_pa(&dev->calib, raw.adc_p, t_fine);
    *humidity_centi = bme280_humidity_centi(&dev->calib, raw.adc_h, t_fine);
    return 0;
}
