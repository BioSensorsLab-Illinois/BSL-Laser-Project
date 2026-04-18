#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define LASER_CONTROLLER_WIRELESS_SCAN_RESULT_COUNT 8U

typedef enum {
    LASER_CONTROLLER_WIRELESS_MODE_SOFTAP = 0,
    LASER_CONTROLLER_WIRELESS_MODE_STATION,
} laser_controller_wireless_mode_t;

typedef struct {
    char ssid[33];
    int16_t rssi_dbm;
    uint8_t channel;
    bool secure;
} laser_controller_wireless_scan_result_t;

typedef struct {
    bool started;
    laser_controller_wireless_mode_t mode;
    bool ap_ready;
    bool station_configured;
    bool station_connecting;
    bool station_connected;
    uint8_t client_count;
    char ssid[33];
    char station_ssid[33];
    int16_t station_rssi_dbm;
    uint8_t station_channel;
    bool scan_in_progress;
    uint8_t scan_result_count;
    laser_controller_wireless_scan_result_t
        scan_results[LASER_CONTROLLER_WIRELESS_SCAN_RESULT_COUNT];
    char ip_address[16];
    char ws_url[64];
    char last_error[96];
} laser_controller_wireless_status_t;

esp_err_t laser_controller_wireless_start(void);
esp_err_t laser_controller_wireless_configure(
    laser_controller_wireless_mode_t mode,
    const char *station_ssid,
    const char *station_password);
esp_err_t laser_controller_wireless_scan_networks(void);
void laser_controller_wireless_broadcast_text(const char *line);
bool laser_controller_wireless_has_clients(void);
/*
 * Atomically read and clear the "new client joined — please emit a
 * fresh snapshot" flag. Called by the comms TX task on every tick.
 * Returns true at most once per newly-connected client.
 */
bool laser_controller_wireless_consume_new_client_pending(void);
void laser_controller_wireless_copy_status(
    laser_controller_wireless_status_t *status);
