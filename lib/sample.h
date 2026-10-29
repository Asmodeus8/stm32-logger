#ifndef SAMPLE_H
#define SAMPLE_H

#include <stdint.h>

/* Fixed-point so the firmware never needs printf("%f"). */
typedef struct {
    uint32_t seq;
    uint32_t epoch;          /* seconds since 1970-01-01 UTC */
    int32_t  bme_temp_centi; /* 0.01 degC */
    uint32_t humidity_centi; /* 0.01 %RH */
    uint32_t pressure_pa;
    int32_t  lm35_temp_centi;
    uint8_t  flags;
} sample_t;

enum {
    SAMPLE_BME_ERROR  = 1u << 0,
    SAMPLE_ADC_ERROR  = 1u << 1,
    SAMPLE_TIME_UNSET = 1u << 2,
};

#endif
