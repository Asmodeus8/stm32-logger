#ifndef CIVIL_TIME_H
#define CIVIL_TIME_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} civil_t;

void civil_from_epoch(uint32_t epoch, civil_t *out);
uint32_t epoch_from_civil(const civil_t *c);

#endif
