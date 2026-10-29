#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bme280.h"
#include "civil_time.h"
#include "lm35.h"
#include "record.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ_INT(a, b) do { long long _a = (a), _b = (b); checks++; if (_a != _b) { failures++; printf("FAIL %s:%d: %s = %lld, want %lld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b)) != 0) { failures++; printf("FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

/* Calibration and readings from the worked example in the Bosch BMP280 datasheet;
 * the BME280 uses the same temperature and pressure formulas. */
static const bme280_calib_t datasheet = {
    .t1 = 27504, .t2 = 26435, .t3 = -1000,
    .p1 = 36477, .p2 = -10685, .p3 = 3024, .p4 = 2855, .p5 = 140, .p6 = -7, .p7 = 15500, .p8 = -14600, .p9 = 6000,
    /* humidity coefficients read from a real BME280 */
    .h1 = 75, .h2 = 362, .h3 = 0, .h4 = 313, .h5 = 50, .h6 = 30,
};

/* Bosch's floating-point reference formulas, used as an independent check. */
static double ref_t_fine(const bme280_calib_t *c, int32_t adc_t)
{
    double v1 = (adc_t / 16384.0 - c->t1 / 1024.0) * c->t2;
    double v2 = (adc_t / 131072.0 - c->t1 / 8192.0) * (adc_t / 131072.0 - c->t1 / 8192.0) * c->t3;
    return v1 + v2;
}

static double ref_pressure(const bme280_calib_t *c, int32_t adc_p, double t_fine)
{
    double v1 = t_fine / 2.0 - 64000.0;
    double v2 = v1 * v1 * c->p6 / 32768.0;
    v2 = v2 + v1 * c->p5 * 2.0;
    v2 = v2 / 4.0 + c->p4 * 65536.0;
    v1 = (c->p3 * v1 * v1 / 524288.0 + c->p2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * c->p1;
    double p = 1048576.0 - adc_p;
    p = (p - v2 / 4096.0) * 6250.0 / v1;
    v1 = c->p9 * p * p / 2147483648.0;
    v2 = p * c->p8 / 32768.0;
    return p + (v1 + v2 + c->p7) / 16.0;
}

static double ref_humidity(const bme280_calib_t *c, int32_t adc_h, double t_fine)
{
    double h = t_fine - 76800.0;
    h = (adc_h - (c->h4 * 64.0 + c->h5 / 16384.0 * h)) *
        (c->h2 / 65536.0 * (1.0 + c->h6 / 67108864.0 * h * (1.0 + c->h3 / 67108864.0 * h)));
    h = h * (1.0 - c->h1 * h / 524288.0);
    return h < 0 ? 0 : h > 100 ? 100 : h;
}

static void test_bme280_datasheet_example(void)
{
    int32_t t_fine;
    CHECK_EQ_INT(bme280_temp_centi(&datasheet, 519888, &t_fine), 2508);
    CHECK_EQ_INT(t_fine, 128422);
    CHECK_EQ_INT(bme280_pressure_pa(&datasheet, 415148, t_fine), 100653);
}

static void test_bme280_matches_float_reference(void)
{
    for (int32_t adc_t = 400000; adc_t <= 600000; adc_t += 25000) {
        int32_t t_fine;
        int32_t t = bme280_temp_centi(&datasheet, adc_t, &t_fine);
        double ref_fine = ref_t_fine(&datasheet, adc_t);
        CHECK(fabs(t / 100.0 - ref_fine / 5120.0) < 0.02);

        for (int32_t adc_p = 300000; adc_p <= 500000; adc_p += 50000) {
            double ref = ref_pressure(&datasheet, adc_p, ref_fine);
            CHECK(fabs((double)bme280_pressure_pa(&datasheet, adc_p, t_fine) - ref) < 2.0);
        }
        for (int32_t adc_h = 20000; adc_h <= 40000; adc_h += 2500) {
            double ref = ref_humidity(&datasheet, adc_h, ref_fine);
            CHECK(fabs(bme280_humidity_centi(&datasheet, adc_h, t_fine) / 100.0 - ref) < 0.1);
        }
    }
}

static void test_bme280_humidity_is_clamped(void)
{
    int32_t t_fine;
    bme280_temp_centi(&datasheet, 519888, &t_fine);
    CHECK_EQ_INT(bme280_humidity_centi(&datasheet, 0, t_fine), 0);
    CHECK_EQ_INT(bme280_humidity_centi(&datasheet, 65535, t_fine), 10000);
}

static void test_bme280_parse_calib_packs_h4_h5(void)
{
    uint8_t c1[26] = {0}, c2[7] = {0x6A, 0x01, 0x00, 0x13, 0x29, 0x03, 0x1E};
    c1[0] = 0x70; c1[1] = 0x6B; /* T1 = 27504 */
    c1[2] = 0x43; c1[3] = 0x67; /* T2 = 26435 */
    c1[4] = 0x18; c1[5] = 0xFC; /* T3 = -1000 */
    c1[25] = 75;
    bme280_calib_t c;
    bme280_parse_calib(c1, c2, &c);
    CHECK_EQ_INT(c.t1, 27504);
    CHECK_EQ_INT(c.t2, 26435);
    CHECK_EQ_INT(c.t3, -1000);
    CHECK_EQ_INT(c.h1, 75);
    CHECK_EQ_INT(c.h2, 362);
    CHECK_EQ_INT(c.h4, 0x13 * 16 + 0x9);  /* 313 */
    CHECK_EQ_INT(c.h5, 0x03 * 16 + 0x2);  /* 50 */
    CHECK_EQ_INT(c.h6, 30);

    uint8_t neg[7] = {0, 0, 0, 0xFF, 0xF8, 0xFF, 0};
    bme280_parse_calib(c1, neg, &c);
    CHECK_EQ_INT(c.h4, -8);
    CHECK_EQ_INT(c.h5, -1);
}

static void test_bme280_parse_raw(void)
{
    const uint8_t d[8] = {0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00, 0x6D, 0x1F};
    bme280_raw_t r;
    bme280_parse_raw(d, &r);
    CHECK_EQ_INT(r.adc_p, 415148);
    CHECK_EQ_INT(r.adc_t, 519888);
    CHECK_EQ_INT(r.adc_h, 27935);
}

/* Fake I2C bus that answers like a BME280 holding the datasheet calibration. */
static struct {
    uint8_t regs[256];
    int busy_polls;
    int writes;
    int fail_reads;
} fake;

static int fake_read(void *ctx, uint8_t reg, uint8_t *buf, size_t len)
{
    (void)ctx;
    if (fake.fail_reads) return -1;
    if (reg == BME280_REG_STATUS) {
        buf[0] = fake.busy_polls > 0 ? 0x08 : 0x00;
        if (fake.busy_polls > 0) fake.busy_polls--;
        return 0;
    }
    memcpy(buf, &fake.regs[reg], len);
    return 0;
}
static int fake_write(void *ctx, uint8_t reg, uint8_t value) { (void)ctx; fake.regs[reg] = value; fake.writes++; return 0; }
static void fake_delay(uint32_t ms) { (void)ms; }

static void fake_reset(void)
{
    memset(&fake, 0, sizeof fake);
    fake.regs[BME280_REG_ID] = BME280_CHIP_ID;
    const uint8_t c1[26] = {0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC, 0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B, 0x27, 0x0B,
                            0x8C, 0x00, 0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6, 0x70, 0x17, 0x00, 0x4B};
    const uint8_t c2[7] = {0x6A, 0x01, 0x00, 0x13, 0x29, 0x03, 0x1E};
    memcpy(&fake.regs[BME280_REG_CALIB00], c1, sizeof c1);
    memcpy(&fake.regs[BME280_REG_CALIB26], c2, sizeof c2);
    const uint8_t d[8] = {0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00, 0x6D, 0x1F};
    memcpy(&fake.regs[BME280_REG_DATA], d, sizeof d);
}

static bme280_t fake_dev(void)
{
    bme280_t dev = {.bus = {.read = fake_read, .write = fake_write, .delay_ms = fake_delay}};
    return dev;
}

static void test_bme280_driver_forced_measurement(void)
{
    fake_reset();
    bme280_t dev = fake_dev();
    CHECK_EQ_INT(bme280_init(&dev), 0);
    CHECK_EQ_INT(dev.calib.p1, 36477);
    CHECK_EQ_INT(dev.calib.p9, 6000);
    CHECK_EQ_INT(fake.regs[BME280_REG_CTRL_HUM], 0x01);

    fake.busy_polls = 3;
    int32_t t; uint32_t p, h;
    CHECK_EQ_INT(bme280_measure(&dev, &t, &p, &h), 0);
    CHECK_EQ_INT(t, 2508);
    CHECK_EQ_INT(p, 100653);
    CHECK(h > 0 && h <= 10000);
    CHECK_EQ_INT(fake.regs[BME280_REG_CTRL_MEAS], 0x25);
}

static void test_bme280_driver_errors(void)
{
    fake_reset();
    fake.regs[BME280_REG_ID] = 0x58; /* a BMP280 */
    bme280_t dev = fake_dev();
    CHECK_EQ_INT(bme280_init(&dev), -1);

    fake_reset();
    CHECK_EQ_INT(bme280_init(&dev), 0);
    fake.busy_polls = 100;
    int32_t t; uint32_t p, h;
    CHECK_EQ_INT(bme280_measure(&dev, &t, &p, &h), -1);

    fake.busy_polls = 0;
    fake.regs[BME280_REG_DATA + 3] = 0x80; fake.regs[BME280_REG_DATA + 4] = 0; fake.regs[BME280_REG_DATA + 5] = 0;
    CHECK_EQ_INT(bme280_measure(&dev, &t, &p, &h), -1);

    fake.fail_reads = 1;
    CHECK_EQ_INT(bme280_measure(&dev, &t, &p, &h), -1);
}

static void test_lm35(void)
{
    /* VREFINT reads its calibration value exactly at 3.3 V. */
    CHECK_EQ_INT(lm35_vdda_mv(1500, 1500, 3300), 3300);
    /* A lower reading means VDDA is higher than 3.3 V. */
    CHECK_EQ_INT(lm35_vdda_mv(1450, 1500, 3300), 3414);
    CHECK_EQ_INT(lm35_vdda_mv(0, 1500, 3300), 3300);

    /* 310.31 counts at 3.3 V full scale is 250.07 mV: 25.01 degC. */
    CHECK_EQ_INT(lm35_temp_centi(310 * 16 + 5, 16, 3300), 2501);
    CHECK_EQ_INT(lm35_temp_centi(0, 16, 3300), 0);
    CHECK_EQ_INT(lm35_temp_centi(4095 * 16, 16, 3300), 33000);
    CHECK_EQ_INT(lm35_temp_centi(100, 0, 3300), 0);
}

static void test_civil_time(void)
{
    civil_t c;
    civil_from_epoch(0, &c);
    CHECK(c.year == 1970 && c.month == 1 && c.day == 1 && c.hour == 0);
    civil_from_epoch(1793282585, &c); /* 2026-10-29T14:03:05Z */
    CHECK(c.year == 2026 && c.month == 10 && c.day == 29 && c.hour == 14 && c.minute == 3 && c.second == 5);
    civil_from_epoch(1709164800, &c); /* 2024-02-29 */
    CHECK(c.year == 2024 && c.month == 2 && c.day == 29);
    for (uint32_t e = 946684800u; e < 4102444800u; e += 86400u * 37u + 3671u) {
        civil_from_epoch(e, &c);
        CHECK_EQ_INT(epoch_from_civil(&c), e);
    }
}

static void test_csv(void)
{
    sample_t s = {.seq = 7, .epoch = 1793282585, .bme_temp_centi = 2437, .humidity_centi = 6120,
                  .pressure_pa = 100842, .lm35_temp_centi = 2490, .flags = 0};
    char buf[96];
    CHECK(csv_format(&s, buf, sizeof buf) > 0);
    CHECK_STR(buf, "2026-10-29T14:03:05Z,24.37,61.20,1008.42,24.90,0\r\n");

    s.bme_temp_centi = -305;
    s.flags = SAMPLE_ADC_ERROR;
    csv_format(&s, buf, sizeof buf);
    CHECK_STR(buf, "2026-10-29T14:03:05Z,-3.05,61.20,1008.42,,2\r\n");

    s.flags = SAMPLE_BME_ERROR;
    csv_format(&s, buf, sizeof buf);
    CHECK_STR(buf, "2026-10-29T14:03:05Z,,,,24.90,1\r\n");

    CHECK_EQ_INT(csv_format(&s, buf, 10), -1);

    char name[13];
    csv_filename(1793282585, name);
    CHECK_STR(name, "20261029.CSV");
}

static void test_frames(void)
{
    sample_t s = {.seq = 42, .epoch = 1793282585, .bme_temp_centi = 2437, .humidity_centi = 6120,
                  .pressure_pa = 100842, .lm35_temp_centi = 2490, .flags = 4};
    char buf[96];
    int n = frame_log(&s, buf, sizeof buf);
    CHECK_EQ_INT(n, (int)strlen(buf));
    CHECK(strncmp(buf, "$LOG,42,1793282585,2437,6120,100842,2490,4*", 43) == 0);
    CHECK(buf[n - 2] == '\r' && buf[n - 1] == '\n');
    const char *body = buf + 1;
    const char *star = strchr(buf, '*');
    char hex[3];
    snprintf(hex, sizeof hex, "%02X", frame_checksum(body, (size_t)(star - body)));
    CHECK(strncmp(star + 1, hex, 2) == 0);
    CHECK_EQ_INT(frame_log(&s, buf, 20), -1);

    char line[32];
    uint32_t v = 0;
    snprintf(line, sizeof line, "$TIME,1793282585*%02X\r\n", frame_checksum("TIME,1793282585", 15));
    CHECK_EQ_INT(frame_parse(line, &v), FRAME_TIME);
    CHECK_EQ_INT(v, 1793282585u);

    snprintf(line, sizeof line, "$ACK,42*%02X", frame_checksum("ACK,42", 6));
    CHECK_EQ_INT(frame_parse(line, &v), FRAME_ACK);
    CHECK_EQ_INT(v, 42);

    CHECK_EQ_INT(frame_parse("\r\n", &v), FRAME_NONE);
    CHECK_EQ_INT(frame_parse("$TIME,1793282585*00", &v), FRAME_BAD);
    CHECK_EQ_INT(frame_parse("TIME,1*00", &v), FRAME_BAD);
    CHECK_EQ_INT(frame_parse("$TIME,1793282585", &v), FRAME_BAD);

    snprintf(line, sizeof line, "$TIME,12a*%02X", frame_checksum("TIME,12a", 8));
    CHECK_EQ_INT(frame_parse(line, &v), FRAME_BAD);
    snprintf(line, sizeof line, "$TIME,*%02X", frame_checksum("TIME,", 5));
    CHECK_EQ_INT(frame_parse(line, &v), FRAME_BAD);
    snprintf(line, sizeof line, "$TIME,4294967296*%02X", frame_checksum("TIME,4294967296", 15));
    CHECK_EQ_INT(frame_parse(line, &v), FRAME_BAD);
    snprintf(line, sizeof line, "$PING,1*%02X", frame_checksum("PING,1", 6));
    CHECK_EQ_INT(frame_parse(line, &v), FRAME_BAD);
}

int main(void)
{
    test_bme280_datasheet_example();
    test_bme280_matches_float_reference();
    test_bme280_humidity_is_clamped();
    test_bme280_parse_calib_packs_h4_h5();
    test_bme280_parse_raw();
    test_bme280_driver_forced_measurement();
    test_bme280_driver_errors();
    test_lm35();
    test_civil_time();
    test_csv();
    test_frames();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
