#include "civil_time.h"

/* Howard Hinnant's days-from-civil algorithm, valid for every date this logger will see. */

void civil_from_epoch(uint32_t epoch, civil_t *out)
{
    int32_t days = (int32_t)(epoch / 86400u);
    uint32_t secs = epoch % 86400u;

    int32_t z = days + 719468;
    int32_t era = z / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t y = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    uint32_t m = mp < 10 ? mp + 3 : mp - 9;

    out->year = (uint16_t)(y + (m <= 2));
    out->month = (uint8_t)m;
    out->day = (uint8_t)d;
    out->hour = (uint8_t)(secs / 3600u);
    out->minute = (uint8_t)((secs / 60u) % 60u);
    out->second = (uint8_t)(secs % 60u);
}

uint32_t epoch_from_civil(const civil_t *c)
{
    int32_t y = (int32_t)c->year - (c->month <= 2);
    int32_t era = y / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t mp = c->month > 2 ? c->month - 3u : c->month + 9u;
    uint32_t doy = (153 * mp + 2) / 5 + c->day - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int32_t days = era * 146097 + (int32_t)doe - 719468;
    return (uint32_t)days * 86400u + c->hour * 3600u + c->minute * 60u + c->second;
}
