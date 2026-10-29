#include "lm35.h"

uint32_t lm35_vdda_mv(uint16_t vrefint_raw, uint16_t vrefint_cal, uint32_t cal_vdda_mv)
{
    if (vrefint_raw == 0) {
        return cal_vdda_mv;
    }
    return (cal_vdda_mv * vrefint_cal + vrefint_raw / 2u) / vrefint_raw;
}

int32_t lm35_temp_centi(uint32_t adc_sum, uint32_t samples, uint32_t vdda_mv)
{
    if (samples == 0) {
        return 0;
    }
    /* centi-degC = mV * 10 = sum * vdda * 10 / (4095 * n), rounded. */
    uint64_t num = (uint64_t)adc_sum * vdda_mv * 10u;
    uint64_t den = (uint64_t)ADC_FULL_SCALE * samples;
    return (int32_t)((num + den / 2u) / den);
}
