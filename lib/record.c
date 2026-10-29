#include "record.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "civil_time.h"

/* Writes v (scaled by 100) as "-12.34" without floating point. */
static int fixed2(char *buf, size_t size, int32_t v)
{
    const char *sign = v < 0 ? "-" : "";
    uint32_t a = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    return snprintf(buf, size, "%s%lu.%02lu", sign, (unsigned long)(a / 100u), (unsigned long)(a % 100u));
}

int csv_format(const sample_t *s, char *buf, size_t size)
{
    civil_t t;
    civil_from_epoch(s->epoch, &t);
    char bme[16] = "", rh[16] = "", hpa[16] = "", lm[16] = "";
    if (!(s->flags & SAMPLE_BME_ERROR)) {
        fixed2(bme, sizeof bme, s->bme_temp_centi);
        fixed2(rh, sizeof rh, (int32_t)s->humidity_centi);
        fixed2(hpa, sizeof hpa, (int32_t)s->pressure_pa); /* Pa / 100 = hPa */
    }
    if (!(s->flags & SAMPLE_ADC_ERROR)) {
        fixed2(lm, sizeof lm, s->lm35_temp_centi);
    }
    int n = snprintf(buf, size, "%04u-%02u-%02uT%02u:%02u:%02uZ,%s,%s,%s,%s,%u\r\n",
                     t.year, t.month, t.day, t.hour, t.minute, t.second, bme, rh, hpa, lm, s->flags);
    return (n < 0 || (size_t)n >= size) ? -1 : n;
}

void csv_filename(uint32_t epoch, char out[13])
{
    civil_t t;
    civil_from_epoch(epoch, &t);
    snprintf(out, 13, "%04u%02u%02u.CSV", t.year % 10000u, t.month % 100u, t.day % 100u);
}

uint8_t frame_checksum(const char *body, size_t len)
{
    uint8_t c = 0;
    for (size_t i = 0; i < len; i++) {
        c ^= (uint8_t)body[i];
    }
    return c;
}

int frame_log(const sample_t *s, char *buf, size_t size)
{
    if (size < 8) {
        return -1;
    }
    int n = snprintf(buf + 1, size - 1, "LOG,%lu,%lu,%ld,%lu,%lu,%ld,%u",
                     (unsigned long)s->seq, (unsigned long)s->epoch, (long)s->bme_temp_centi,
                     (unsigned long)s->humidity_centi, (unsigned long)s->pressure_pa,
                     (long)s->lm35_temp_centi, s->flags);
    if (n < 0 || (size_t)n + 6 > size) {
        return -1;
    }
    buf[0] = '$';
    uint8_t c = frame_checksum(buf + 1, (size_t)n);
    snprintf(buf + 1 + n, size - 1 - (size_t)n, "*%02X\r\n", c);
    return n + 6;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

frame_kind_t frame_parse(const char *line, uint32_t *value)
{
    size_t len = strcspn(line, "\r\n");
    if (len == 0) {
        return FRAME_NONE;
    }
    if (line[0] != '$' || len < 5) {
        return FRAME_BAD;
    }
    const char *star = memchr(line, '*', len);
    if (!star || (size_t)(star - line) + 3 != len) {
        return FRAME_BAD;
    }
    int hi = hexval(star[1]), lo = hexval(star[2]);
    size_t body_len = (size_t)(star - line) - 1;
    if (hi < 0 || lo < 0 || frame_checksum(line + 1, body_len) != (uint8_t)(hi << 4 | lo)) {
        return FRAME_BAD;
    }

    frame_kind_t kind;
    const char *arg;
    if (body_len > 5 && strncmp(line + 1, "TIME,", 5) == 0) {
        kind = FRAME_TIME;
        arg = line + 6;
    } else if (body_len > 4 && strncmp(line + 1, "ACK,", 4) == 0) {
        kind = FRAME_ACK;
        arg = line + 5;
    } else {
        return FRAME_BAD;
    }
    if (arg == star) {
        return FRAME_BAD;
    }
    uint32_t v = 0;
    for (const char *p = arg; p < star; p++) {
        uint32_t d = (uint32_t)(*p - '0');
        if (*p < '0' || *p > '9' || v > (0xFFFFFFFFu - d) / 10u) {
            return FRAME_BAD;
        }
        v = v * 10u + d;
    }
    *value = v;
    return kind;
}
