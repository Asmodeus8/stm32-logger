#include <string.h>

#include "app.h"
#include "board.h"
#include "ff.h"
#include "record.h"
#include "task.h"

static FATFS fs;
static FIL file;
static bool mounted, file_open;
static char current_name[13];
static uint32_t unsynced;
static TickType_t last_sync, last_attempt;

static void unmount(void)
{
    if (file_open) {
        f_close(&file);
        file_open = false;
    }
    f_mount(NULL, "", 0);
    mounted = false;
    stats.sd_ready = false;
    current_name[0] = '\0';
}

static bool mount(void)
{
    last_attempt = xTaskGetTickCount();
    if (f_mount(&fs, "", 1) != FR_OK) {
        return false;
    }
    FRESULT r = f_mkdir("LOG");
    if (r != FR_OK && r != FR_EXIST) {
        f_mount(NULL, "", 0);
        return false;
    }
    mounted = true;
    stats.sd_ready = true;
    return true;
}

/* One file per UTC day, appended to, with a header when the file is new. */
static bool open_for(uint32_t epoch)
{
    char name[13];
    csv_filename(epoch, name);
    if (file_open && strcmp(name, current_name) == 0) {
        return true;
    }
    if (file_open) {
        f_close(&file);
        file_open = false;
    }

    char path[20] = "LOG/";
    strcat(path, name);
    if (f_open(&file, path, FA_OPEN_APPEND | FA_WRITE) != FR_OK) {
        return false;
    }
    file_open = true;
    memcpy(current_name, name, sizeof name);

    if (f_size(&file) == 0) {
        UINT bw;
        if (f_write(&file, CSV_HEADER, sizeof CSV_HEADER - 1, &bw) != FR_OK || bw != sizeof CSV_HEADER - 1) {
            return false;
        }
    }
    return true;
}

static bool write_row(const sample_t *s)
{
    char row[96];
    int n = csv_format(s, row, sizeof row);
    if (n < 0 || !open_for(s->epoch)) {
        return false;
    }
    UINT bw;
    if (f_write(&file, row, (UINT)n, &bw) != FR_OK || bw != (UINT)n) {
        return false;
    }
    unsynced++;
    return true;
}

static bool sync_if_due(void)
{
    if (unsynced == 0) {
        return true;
    }
    TickType_t now = xTaskGetTickCount();
    if (unsynced < LOG_SYNC_EVERY && now - last_sync < pdMS_TO_TICKS(LOG_SYNC_MS)) {
        return true;
    }
    /* f_sync flushes data and the directory entry, so a pulled card or power cut loses
     * at most the last few seconds. */
    if (f_sync(&file) != FR_OK) {
        return false;
    }
    unsynced = 0;
    last_sync = now;
    return true;
}

void logger_task(void *arg)
{
    (void)arg;
    last_attempt = xTaskGetTickCount() - pdMS_TO_TICKS(SD_RETRY_MS);

    for (;;) {
        sample_t s;
        bool got = xQueueReceive(log_queue, &s, pdMS_TO_TICKS(500)) == pdPASS;
        xEventGroupSetBits(alive, ALIVE_LOGGER);

        if (!mounted && xTaskGetTickCount() - last_attempt >= pdMS_TO_TICKS(SD_RETRY_MS)) {
            mount();
        }
        if (!mounted) {
            if (got) {
                stats.rows_dropped++;
            }
            continue;
        }

        if (got && !write_row(&s)) {
            stats.sd_errors++;
            stats.rows_dropped++;
            unmount();
            continue;
        }
        if (got) {
            stats.rows_logged++;
        }
        if (file_open && !sync_if_due()) {
            stats.sd_errors++;
            unmount();
        }
    }
}
