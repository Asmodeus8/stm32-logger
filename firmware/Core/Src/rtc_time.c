#include "app.h"
#include "board.h"
#include "civil_time.h"
#include "ff.h"
#include "semphr.h"
#include "task.h"

/* Written to a backup register once the ESP32 has set the clock, so a reset keeps the time valid. */
#define RTC_SET_MARKER 0x7B1Du

static SemaphoreHandle_t rtc_lock;

void rtc_time_init(void)
{
    rtc_lock = xSemaphoreCreateMutex();
    configASSERT(rtc_lock);
}

static void lock(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTake(rtc_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGive(rtc_lock);
    }
}

bool rtc_is_set(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_SET_MARKER;
}

uint32_t rtc_get_epoch(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    lock();
    /* The date must be read after the time to unlock the shadow registers. */
    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
    unlock();
    civil_t c = {.year = (uint16_t)(2000u + d.Year), .month = d.Month, .day = d.Date,
                 .hour = t.Hours, .minute = t.Minutes, .second = t.Seconds};
    return epoch_from_civil(&c);
}

void rtc_set_epoch(uint32_t epoch)
{
    civil_t c;
    civil_from_epoch(epoch, &c);
    if (c.year < 2000 || c.year > 2099) {
        return;
    }
    RTC_TimeTypeDef t = {.Hours = c.hour, .Minutes = c.minute, .Seconds = c.second,
                         .DayLightSaving = RTC_DAYLIGHTSAVING_NONE, .StoreOperation = RTC_STOREOPERATION_RESET};
    /* 1970-01-01 was a Thursday; RTC weekdays run Monday = 1 .. Sunday = 7. */
    RTC_DateTypeDef d = {.Year = (uint8_t)(c.year - 2000u), .Month = c.month, .Date = c.day,
                         .WeekDay = (uint8_t)(((epoch / 86400u) + 3u) % 7u + 1u)};
    lock();
    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_SET_MARKER);
    unlock();
}

/* FatFs file timestamps. */
DWORD get_fattime(void)
{
    civil_t c;
    civil_from_epoch(rtc_get_epoch(), &c);
    if (c.year < 1980) {
        c.year = 1980;
    }
    return ((DWORD)(c.year - 1980u) << 25) | ((DWORD)c.month << 21) | ((DWORD)c.day << 16) |
           ((DWORD)c.hour << 11) | ((DWORD)c.minute << 5) | ((DWORD)c.second >> 1);
}
