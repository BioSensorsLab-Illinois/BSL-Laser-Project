#pragma once

#include <stddef.h>

#include "laser_controller_types.h"

#define LASER_CONTROLLER_LOG_ENTRY_COUNT 128U
#define LASER_CONTROLLER_LOG_CATEGORY_LEN 16U
#define LASER_CONTROLLER_LOG_MESSAGE_LEN 96U

typedef struct {
    laser_controller_time_ms_t timestamp_ms;
    char category[LASER_CONTROLLER_LOG_CATEGORY_LEN];
    char message[LASER_CONTROLLER_LOG_MESSAGE_LEN];
} laser_controller_log_entry_t;

void laser_controller_logger_init(void);
void laser_controller_logger_log(
    laser_controller_time_ms_t timestamp_ms,
    const char *category,
    const char *message);
void laser_controller_logger_logf(
    laser_controller_time_ms_t timestamp_ms,
    const char *category,
    const char *format,
    ...);
size_t laser_controller_logger_total_count(void);
size_t laser_controller_logger_copy_recent(
    laser_controller_log_entry_t *destination,
    size_t max_entries);
