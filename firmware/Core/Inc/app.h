#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "event_groups.h"
#include "queue.h"
#include "sample.h"

#define SAMPLE_PERIOD_MS   1000u
#define LOG_QUEUE_LEN      32u
#define UPLINK_QUEUE_LEN   8u
#define LOG_SYNC_EVERY     10u     /* f_sync after this many rows */
#define LOG_SYNC_MS        5000u   /* or after this long */
#define SD_RETRY_MS        5000u

/* Each task sets its bit every loop; the supervisor feeds the watchdog only when all have checked in. */
#define ALIVE_SENSORS  (1u << 0)
#define ALIVE_LOGGER   (1u << 1)
#define ALIVE_UPLINK   (1u << 2)
#define ALIVE_ALL      (ALIVE_SENSORS | ALIVE_LOGGER | ALIVE_UPLINK)

extern QueueHandle_t log_queue;
extern QueueHandle_t uplink_queue;
extern EventGroupHandle_t alive;

typedef struct {
    volatile uint32_t samples;
    volatile uint32_t bme_errors;
    volatile uint32_t rows_logged;
    volatile uint32_t rows_dropped;
    volatile uint32_t sd_errors;
    volatile uint32_t frames_sent;
    volatile uint32_t frames_acked;
    volatile uint32_t time_syncs;
    volatile bool sd_ready;
    volatile bool watchdog_reset;
} app_stats_t;

extern app_stats_t stats;

void sensors_task(void *arg);
void logger_task(void *arg);
void uplink_task(void *arg);
void supervisor_task(void *arg);

void uplink_uart_rx_isr(void);

/* RTC, shared between tasks behind a mutex. */
void rtc_time_init(void);
uint32_t rtc_get_epoch(void);
void rtc_set_epoch(uint32_t epoch);
bool rtc_is_set(void);

#endif
