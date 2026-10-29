#include "app.h"
#include "board.h"
#include "task.h"

/* LSI (~32 kHz) / 64 = 500 Hz, 2000 counts: the board resets 4 s after the last refresh. */
static void watchdog_start(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    hiwdg.Init.Reload = 2000;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        Error_Handler();
    }
}

static void led(bool on)
{
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void supervisor_task(void *arg)
{
    (void)arg;
    watchdog_start();

    for (;;) {
        /* Every task must check in within 3 s. If one hangs, the refresh stops and the IWDG resets the MCU. */
        EventBits_t bits = xEventGroupWaitBits(alive, ALIVE_ALL, pdTRUE, pdTRUE, pdMS_TO_TICKS(3000));
        if ((bits & ALIVE_ALL) == ALIVE_ALL) {
            HAL_IWDG_Refresh(&hiwdg);
        }

        /* One blink a second when logging; a double blink when the SD card isn't usable. */
        led(true);
        vTaskDelay(pdMS_TO_TICKS(60));
        led(false);
        if (!stats.sd_ready) {
            vTaskDelay(pdMS_TO_TICKS(120));
            led(true);
            vTaskDelay(pdMS_TO_TICKS(60));
            led(false);
        }
        vTaskDelay(pdMS_TO_TICKS(stats.sd_ready ? 940 : 760));
    }
}
