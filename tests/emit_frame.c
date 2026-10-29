/* Prints a LOG frame exactly as the firmware sends it, for the cross-language check in CI. */
#include <stdio.h>
#include "record.h"

int main(void)
{
    sample_t s = {.seq = 42, .epoch = 1793282585, .bme_temp_centi = -305, .humidity_centi = 6120,
                  .pressure_pa = 100842, .lm35_temp_centi = 2490, .flags = 0};
    char buf[96];
    if (frame_log(&s, buf, sizeof buf) < 0) return 1;
    fputs(buf, stdout);
    return 0;
}
