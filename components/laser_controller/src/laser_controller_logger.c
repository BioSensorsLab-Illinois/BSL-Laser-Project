#include "laser_controller_logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#ifndef LASER_CONTROLLER_LOG_MIRROR_TO_ESP_LOG
#define LASER_CONTROLLER_LOG_MIRROR_TO_ESP_LOG 0
#endif

static laser_controller_log_entry_t s_log_entries[LASER_CONTROLLER_LOG_ENTRY_COUNT];
static size_t s_log_write_index;
#if LASER_CONTROLLER_LOG_MIRROR_TO_ESP_LOG
static const char *kLoggerTag = "laser_log";
#endif
static portMUX_TYPE s_log_lock = portMUX_INITIALIZER_UNLOCKED;

void laser_controller_logger_init(void)
{
    memset(s_log_entries, 0, sizeof(s_log_entries));
    s_log_write_index = 0U;
}

void laser_controller_logger_log(
    laser_controller_time_ms_t timestamp_ms,
    const char *category,
    const char *message)
{
    portENTER_CRITICAL(&s_log_lock);
    laser_controller_log_entry_t *entry =
        &s_log_entries[s_log_write_index % LASER_CONTROLLER_LOG_ENTRY_COUNT];

    entry->timestamp_ms = timestamp_ms;
    (void)snprintf(entry->category, sizeof(entry->category), "%s", category != NULL ? category : "log");
    (void)snprintf(entry->message, sizeof(entry->message), "%s", message != NULL ? message : "");
    s_log_write_index++;
    portEXIT_CRITICAL(&s_log_lock);

#if LASER_CONTROLLER_LOG_MIRROR_TO_ESP_LOG
    ESP_LOGI(kLoggerTag, "[%lu ms] %s: %s",
             (unsigned long)entry->timestamp_ms,
             entry->category,
             entry->message);
#endif
}

void laser_controller_logger_logf(
    laser_controller_time_ms_t timestamp_ms,
    const char *category,
    const char *format,
    ...)
{
    char buffer[LASER_CONTROLLER_LOG_MESSAGE_LEN];
    va_list args;

    va_start(args, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    laser_controller_logger_log(timestamp_ms, category, buffer);
}

size_t laser_controller_logger_total_count(void)
{
    size_t count;

    portENTER_CRITICAL(&s_log_lock);
    count = s_log_write_index;
    portEXIT_CRITICAL(&s_log_lock);

    return count;
}

size_t laser_controller_logger_copy_recent(
    laser_controller_log_entry_t *destination,
    size_t max_entries)
{
    size_t available;
    size_t count;

    portENTER_CRITICAL(&s_log_lock);
    available = s_log_write_index;
    count = available < LASER_CONTROLLER_LOG_ENTRY_COUNT ?
        available :
        LASER_CONTROLLER_LOG_ENTRY_COUNT;

    if (max_entries < count) {
        count = max_entries;
    }

    if (destination == NULL || count == 0U) {
        portEXIT_CRITICAL(&s_log_lock);
        return 0U;
    }

    for (size_t index = 0; index < count; ++index) {
        const size_t source =
            (s_log_write_index - count + index) % LASER_CONTROLLER_LOG_ENTRY_COUNT;
        destination[index] = s_log_entries[source];
    }
    portEXIT_CRITICAL(&s_log_lock);

    return count;
}
