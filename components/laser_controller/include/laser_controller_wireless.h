#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool started;
    bool ap_ready;
    uint8_t client_count;
    char ssid[33];
    char ws_url[64];
} laser_controller_wireless_status_t;

esp_err_t laser_controller_wireless_start(void);
void laser_controller_wireless_broadcast_text(const char *line);
void laser_controller_wireless_copy_status(
    laser_controller_wireless_status_t *status);
