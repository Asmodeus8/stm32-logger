#include <string.h>

#include "app.h"
#include "board.h"
#include "record.h"
#include "stream_buffer.h"
#include "task.h"

static StreamBufferHandle_t rx;
static uint8_t rx_byte;

/* USART1 interrupt: hand each received byte to the uplink task. */
void uplink_uart_rx_isr(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)
{
    if (h->Instance != USART1) return;
    BaseType_t woken = pdFALSE;
    xStreamBufferSendFromISR(rx, &rx_byte, 1, &woken);
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    portYIELD_FROM_ISR(woken);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    if (h->Instance != USART1) return;
    /* Overrun or framing error (ESP32 rebooting, loose wire): clear it and keep listening. */
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

static void handle_line(const char *line)
{
    uint32_t value;
    switch (frame_parse(line, &value)) {
    case FRAME_TIME:
        rtc_set_epoch(value);
        stats.time_syncs++;
        break;
    case FRAME_ACK:
        stats.frames_acked++;
        break;
    default:
        break;
    }
}

void uplink_task(void *arg)
{
    (void)arg;
    rx = xStreamBufferCreate(256, 1);
    configASSERT(rx);
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);

    char line[64];
    size_t len = 0;
    for (;;) {
        sample_t s;
        if (xQueueReceive(uplink_queue, &s, pdMS_TO_TICKS(100)) == pdPASS) {
            char frame[96];
            int n = frame_log(&s, frame, sizeof frame);
            if (n > 0 && HAL_UART_Transmit(&huart1, (uint8_t *)frame, (uint16_t)n, 50) == HAL_OK) {
                stats.frames_sent++;
            }
        }

        char c;
        while (xStreamBufferReceive(rx, &c, 1, 0) == 1) {
            if (c == '\n') {
                line[len] = '\0';
                handle_line(line);
                len = 0;
            } else if (len < sizeof line - 1) {
                line[len++] = c;
            } else {
                len = 0; /* line too long: drop it and resync on the next newline */
            }
        }
        xEventGroupSetBits(alive, ALIVE_UPLINK);
    }
}
