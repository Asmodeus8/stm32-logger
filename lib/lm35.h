#ifndef LM35_H
#define LM35_H

#include <stdint.h>

#define ADC_FULL_SCALE 4095u

/* Actual VDDA in mV, from the internal reference and its factory calibration value. */
uint32_t lm35_vdda_mv(uint16_t vrefint_raw, uint16_t vrefint_cal, uint32_t cal_vdda_mv);

/* LM35 output is 10 mV per degC, so 0.01 degC equals 0.1 mV. */
int32_t lm35_temp_centi(uint32_t adc_sum, uint32_t samples, uint32_t vdda_mv);

#endif
