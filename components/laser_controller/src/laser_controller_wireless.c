#include "laser_controller_wireless.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"

#include "laser_controller_comms.h"

#define LASER_CONTROLLER_WIRELESS_AP_SSID          "BSL-HTLS-Bench"
#define LASER_CONTROLLER_WIRELESS_AP_PASSWORD      "bslbench2026"
#define LASER_CONTROLLER_WIRELESS_AP_CHANNEL       6
#define LASER_CONTROLLER_WIRELESS_MAX_CLIENTS      1
#define LASER_CONTROLLER_WIRELESS_WS_URI           "/ws"
#define LASER_CONTROLLER_WIRELESS_AP_WS_URL        "ws://192.168.4.1/ws"
#define LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS     2
#define LASER_CONTROLLER_WIRELESS_MAX_FRAME_LEN    768U
#define LASER_CONTROLLER_WIRELESS_NVS_NAMESPACE    "laser_wifi"
#define LASER_CONTROLLER_WIRELESS_NVS_KEY          "config"
#define LASER_CONTROLLER_WIRELESS_CONFIG_VERSION   1U
#define LASER_CONTROLLER_WIRELESS_PASSWORD_LEN     65U

typedef struct {
    uint32_t version;
    uint32_t mode;
    char station_ssid[33];
    char station_password[LASER_CONTROLLER_WIRELESS_PASSWORD_LEN];
} laser_controller_wireless_persisted_config_t;

static const char *TAG = "laser_wireless";

static httpd_handle_t s_server;
static esp_netif_t *s_wifi_ap_netif;
static esp_netif_t *s_wifi_sta_netif;
static esp_event_handler_instance_t s_wifi_event_handle;
static esp_event_handler_instance_t s_ip_event_handle;
static bool s_stack_initialized;
static bool s_wifi_initialized;
static bool s_handlers_registered;
static bool s_config_loaded;
static bool s_started;
static bool s_ap_ready;
static bool s_station_configured;
static bool s_station_connecting;
static bool s_station_connected;
static laser_controller_wireless_mode_t s_mode =
    LASER_CONTROLLER_WIRELESS_MODE_SOFTAP;
static int s_client_fds[LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS] = { -1, -1 };
static char s_active_ssid[33] = LASER_CONTROLLER_WIRELESS_AP_SSID;
static char s_station_ssid[33];
static char s_station_password[LASER_CONTROLLER_WIRELESS_PASSWORD_LEN];
static int16_t s_station_rssi_dbm;
static uint8_t s_station_channel;
static bool s_scan_in_progress;
static uint8_t s_scan_result_count;
static laser_controller_wireless_scan_result_t
    s_scan_results[LASER_CONTROLLER_WIRELESS_SCAN_RESULT_COUNT];
static char s_ip_address[16] = "192.168.4.1";
static char s_ws_url[64] = LASER_CONTROLLER_WIRELESS_AP_WS_URL;
static char s_last_error[96];
static portMUX_TYPE s_wireless_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_client_lock = portMUX_INITIALIZER_UNLOCKED;

static void laser_controller_wireless_copy_text(
    char *dest,
    size_t dest_size,
    const char *src)
{
    if (dest == NULL || dest_size == 0U) {
        return;
    }

    (void)strlcpy(dest, src != NULL ? src : "", dest_size);
}

static void laser_controller_wireless_set_errorf(const char *fmt, ...)
{
    va_list args;

    portENTER_CRITICAL(&s_wireless_lock);
    va_start(args, fmt);
    (void)vsnprintf(s_last_error, sizeof(s_last_error), fmt, args);
    va_end(args);
    portEXIT_CRITICAL(&s_wireless_lock);
}

static void laser_controller_wireless_clear_error_locked(void)
{
    s_last_error[0] = '\0';
}

static void laser_controller_wireless_clear_scan_results_locked(void)
{
    s_scan_in_progress = false;
    s_scan_result_count = 0U;
    memset(s_scan_results, 0, sizeof(s_scan_results));
}

static void laser_controller_wireless_reset_endpoint_locked(void)
{
    s_ip_address[0] = '\0';
    s_ws_url[0] = '\0';
}

static bool laser_controller_wireless_authmode_secure(
    wifi_auth_mode_t auth_mode)
{
    return auth_mode != WIFI_AUTH_OPEN &&
           auth_mode != WIFI_AUTH_OWE &&
           auth_mode != WIFI_AUTH_MAX;
}

static void laser_controller_wireless_refresh_station_metrics(void)
{
    wifi_ap_record_t ap_info = { 0 };
    const esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

    portENTER_CRITICAL(&s_wireless_lock);
    if (err == ESP_OK) {
        s_station_rssi_dbm = ap_info.rssi;
        s_station_channel = ap_info.primary;
    } else {
        s_station_rssi_dbm = 0;
        s_station_channel = 0U;
    }
    portEXIT_CRITICAL(&s_wireless_lock);
}

static void laser_controller_wireless_set_softap_identity_locked(void)
{
    laser_controller_wireless_copy_text(
        s_active_ssid,
        sizeof(s_active_ssid),
        LASER_CONTROLLER_WIRELESS_AP_SSID);
    laser_controller_wireless_copy_text(
        s_ip_address,
        sizeof(s_ip_address),
        "192.168.4.1");
    laser_controller_wireless_copy_text(
        s_ws_url,
        sizeof(s_ws_url),
        LASER_CONTROLLER_WIRELESS_AP_WS_URL);
}

static void laser_controller_wireless_set_station_identity_locked(void)
{
    laser_controller_wireless_copy_text(
        s_active_ssid,
        sizeof(s_active_ssid),
        s_station_ssid);
}

static void laser_controller_wireless_update_station_endpoint_locked(
    const ip_event_got_ip_t *event)
{
    if (event == NULL) {
        laser_controller_wireless_reset_endpoint_locked();
        return;
    }

    (void)snprintf(
        s_ip_address,
        sizeof(s_ip_address),
        IPSTR,
        IP2STR(&event->ip_info.ip));
    (void)snprintf(
        s_ws_url,
        sizeof(s_ws_url),
        "ws://%s" LASER_CONTROLLER_WIRELESS_WS_URI,
        s_ip_address);
}

static void laser_controller_wireless_clear_clients(void)
{
    portENTER_CRITICAL(&s_client_lock);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        s_client_fds[index] = -1;
    }
    portEXIT_CRITICAL(&s_client_lock);
}

static void laser_controller_wireless_add_client(int fd)
{
    portENTER_CRITICAL(&s_client_lock);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        if (s_client_fds[index] == fd) {
            portEXIT_CRITICAL(&s_client_lock);
            return;
        }
    }

    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        if (s_client_fds[index] < 0) {
            s_client_fds[index] = fd;
            break;
        }
    }
    portEXIT_CRITICAL(&s_client_lock);
}

static void laser_controller_wireless_remove_client(int fd)
{
    portENTER_CRITICAL(&s_client_lock);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        if (s_client_fds[index] == fd) {
            s_client_fds[index] = -1;
        }
    }
    portEXIT_CRITICAL(&s_client_lock);
}

static uint8_t laser_controller_wireless_count_clients(void)
{
    uint8_t count = 0U;

    portENTER_CRITICAL(&s_client_lock);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        if (s_client_fds[index] >= 0) {
            ++count;
        }
    }
    portEXIT_CRITICAL(&s_client_lock);

    return count;
}

static void laser_controller_wireless_load_defaults_locked(void)
{
    s_mode = LASER_CONTROLLER_WIRELESS_MODE_SOFTAP;
    s_station_ssid[0] = '\0';
    s_station_password[0] = '\0';
    s_station_configured = false;
    s_station_connecting = false;
    s_station_connected = false;
    s_station_rssi_dbm = 0;
    s_station_channel = 0U;
    s_ap_ready = false;
    s_started = false;
    laser_controller_wireless_clear_scan_results_locked();
    laser_controller_wireless_set_softap_identity_locked();
    laser_controller_wireless_clear_error_locked();
}

static void laser_controller_wireless_load_config_locked(void)
{
    nvs_handle_t handle = 0;
    laser_controller_wireless_persisted_config_t persisted = { 0 };
    size_t size = sizeof(persisted);
    esp_err_t err = ESP_OK;

    if (s_config_loaded) {
        return;
    }

    laser_controller_wireless_load_defaults_locked();
    s_config_loaded = true;

    err = nvs_open(
        LASER_CONTROLLER_WIRELESS_NVS_NAMESPACE,
        NVS_READWRITE,
        &handle);
    if (err != ESP_OK) {
        return;
    }

    err = nvs_get_blob(
        handle,
        LASER_CONTROLLER_WIRELESS_NVS_KEY,
        &persisted,
        &size);
    nvs_close(handle);
    if (err != ESP_OK ||
        size != sizeof(persisted) ||
        persisted.version != LASER_CONTROLLER_WIRELESS_CONFIG_VERSION) {
        return;
    }

    if (persisted.mode == LASER_CONTROLLER_WIRELESS_MODE_STATION) {
        s_mode = LASER_CONTROLLER_WIRELESS_MODE_STATION;
    }
    laser_controller_wireless_copy_text(
        s_station_ssid,
        sizeof(s_station_ssid),
        persisted.station_ssid);
    laser_controller_wireless_copy_text(
        s_station_password,
        sizeof(s_station_password),
        persisted.station_password);
    s_station_configured = s_station_ssid[0] != '\0';
    if (!s_station_configured) {
        s_mode = LASER_CONTROLLER_WIRELESS_MODE_SOFTAP;
    }
}

static esp_err_t laser_controller_wireless_save_config(void)
{
    nvs_handle_t handle = 0;
    laser_controller_wireless_persisted_config_t persisted = {
        .version = LASER_CONTROLLER_WIRELESS_CONFIG_VERSION,
    };
    esp_err_t err = ESP_OK;

    portENTER_CRITICAL(&s_wireless_lock);
    persisted.mode = (uint32_t)s_mode;
    laser_controller_wireless_copy_text(
        persisted.station_ssid,
        sizeof(persisted.station_ssid),
        s_station_ssid);
    laser_controller_wireless_copy_text(
        persisted.station_password,
        sizeof(persisted.station_password),
        s_station_password);
    portEXIT_CRITICAL(&s_wireless_lock);

    err = nvs_open(
        LASER_CONTROLLER_WIRELESS_NVS_NAMESPACE,
        NVS_READWRITE,
        &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(
        handle,
        LASER_CONTROLLER_WIRELESS_NVS_KEY,
        &persisted,
        sizeof(persisted));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void laser_controller_wireless_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_START) {
            portENTER_CRITICAL(&s_wireless_lock);
            s_ap_ready = true;
            s_started = s_server != NULL;
            if (s_mode == LASER_CONTROLLER_WIRELESS_MODE_SOFTAP) {
                laser_controller_wireless_set_softap_identity_locked();
            }
            laser_controller_wireless_clear_error_locked();
            portEXIT_CRITICAL(&s_wireless_lock);
            return;
        }

        if (event_id == WIFI_EVENT_AP_STOP) {
            portENTER_CRITICAL(&s_wireless_lock);
            s_ap_ready = false;
            portEXIT_CRITICAL(&s_wireless_lock);
            return;
        }

        if (event_id == WIFI_EVENT_STA_START) {
            bool should_connect = false;

            portENTER_CRITICAL(&s_wireless_lock);
            should_connect =
                s_mode == LASER_CONTROLLER_WIRELESS_MODE_STATION &&
                s_station_configured &&
                !s_scan_in_progress;
            s_station_connecting = should_connect;
            s_station_connected = false;
            s_station_rssi_dbm = 0;
            s_station_channel = 0U;
            if (s_mode == LASER_CONTROLLER_WIRELESS_MODE_STATION) {
                laser_controller_wireless_set_station_identity_locked();
            }
            laser_controller_wireless_reset_endpoint_locked();
            portEXIT_CRITICAL(&s_wireless_lock);

            if (should_connect) {
                (void)esp_wifi_connect();
            } else {
                laser_controller_wireless_set_errorf(
                    "Station mode needs stored credentials.");
            }
            return;
        }

        if (event_id == WIFI_EVENT_STA_CONNECTED) {
            portENTER_CRITICAL(&s_wireless_lock);
            s_station_connecting = true;
            s_station_rssi_dbm = 0;
            s_station_channel = 0U;
            if (s_mode == LASER_CONTROLLER_WIRELESS_MODE_STATION) {
                laser_controller_wireless_set_station_identity_locked();
            }
            laser_controller_wireless_clear_error_locked();
            portEXIT_CRITICAL(&s_wireless_lock);
            return;
        }

        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            const wifi_event_sta_disconnected_t *event =
                (const wifi_event_sta_disconnected_t *)event_data;
            bool should_reconnect = false;

            portENTER_CRITICAL(&s_wireless_lock);
            should_reconnect =
                s_mode == LASER_CONTROLLER_WIRELESS_MODE_STATION &&
                s_station_configured &&
                !s_scan_in_progress;
            s_station_connected = false;
            s_station_connecting = should_reconnect;
            s_station_rssi_dbm = 0;
            s_station_channel = 0U;
            laser_controller_wireless_reset_endpoint_locked();
            if (event != NULL) {
                (void)snprintf(
                    s_last_error,
                    sizeof(s_last_error),
                    "Station disconnected (reason %u).",
                    (unsigned int)event->reason);
            }
            portEXIT_CRITICAL(&s_wireless_lock);

            if (should_reconnect) {
                (void)esp_wifi_connect();
            }
            return;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;

        portENTER_CRITICAL(&s_wireless_lock);
        s_station_connecting = false;
        s_station_connected = true;
        s_started = s_server != NULL;
        laser_controller_wireless_set_station_identity_locked();
        laser_controller_wireless_update_station_endpoint_locked(event);
        laser_controller_wireless_clear_error_locked();
        portEXIT_CRITICAL(&s_wireless_lock);
        laser_controller_wireless_refresh_station_metrics();
    }
}

static esp_err_t laser_controller_wireless_ensure_stack_ready(void)
{
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = ESP_OK;

    if (!s_stack_initialized) {
        err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }

        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }

        if (s_wifi_ap_netif == NULL) {
            s_wifi_ap_netif = esp_netif_create_default_wifi_ap();
        }
        if (s_wifi_sta_netif == NULL) {
            s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
        }
        s_stack_initialized = true;
    }

    if (!s_wifi_initialized) {
        err = esp_wifi_init(&wifi_init_config);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);
        (void)esp_wifi_set_ps(WIFI_PS_NONE);
        s_wifi_initialized = true;
    }

    if (!s_handlers_registered) {
        err = esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &laser_controller_wireless_event_handler,
            NULL,
            &s_wifi_event_handle);
        if (err != ESP_OK) {
            return err;
        }

        err = esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &laser_controller_wireless_event_handler,
            NULL,
            &s_ip_event_handle);
        if (err != ESP_OK) {
            return err;
        }
        s_handlers_registered = true;
    }

    return ESP_OK;
}

static esp_err_t laser_controller_wireless_ws_handler(httpd_req_t *req)
{
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
    };
    uint8_t payload[LASER_CONTROLLER_WIRELESS_MAX_FRAME_LEN] = { 0 };
    esp_err_t err = ESP_OK;

    if (req->method == HTTP_GET) {
        laser_controller_wireless_add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    err = httpd_ws_recv_frame(req, &frame, 0U);
    if (err != ESP_OK) {
        return err;
    }

    if (frame.len == 0U) {
        return ESP_OK;
    }

    if (frame.len >= sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    frame.payload = payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        laser_controller_wireless_remove_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        payload[frame.len] = '\0';
        laser_controller_comms_receive_line((const char *)payload);
    }

    return ESP_OK;
}

static esp_err_t laser_controller_wireless_meta_handler(httpd_req_t *req)
{
    char response[320];
    laser_controller_wireless_status_t status = { 0 };

    laser_controller_wireless_copy_status(&status);
    (void)snprintf(
        response,
        sizeof(response),
        "{"
        "\"mode\":\"%s\","
        "\"ssid\":\"%s\","
        "\"stationSsid\":\"%s\","
        "\"wsUrl\":\"%s\","
        "\"ipAddress\":\"%s\""
        "}",
        status.mode == LASER_CONTROLLER_WIRELESS_MODE_STATION ? "station" : "softap",
        status.ssid,
        status.station_ssid,
        status.ws_url,
        status.ip_address);

    (void)httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static void laser_controller_wireless_start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    static const httpd_uri_t ws_uri = {
        .uri = LASER_CONTROLLER_WIRELESS_WS_URI,
        .method = HTTP_GET,
        .handler = laser_controller_wireless_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    static const httpd_uri_t meta_uri = {
        .uri = "/meta",
        .method = HTTP_GET,
        .handler = laser_controller_wireless_meta_handler,
        .user_ctx = NULL,
    };

    if (s_server != NULL) {
        return;
    }

    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "http server start failed");
        return;
    }

    (void)httpd_register_uri_handler(s_server, &ws_uri);
    (void)httpd_register_uri_handler(s_server, &meta_uri);
}

static esp_err_t laser_controller_wireless_apply_config(
    laser_controller_wireless_mode_t mode)
{
    wifi_config_t wifi_config = { 0 };
    char station_ssid[33];
    char station_password[LASER_CONTROLLER_WIRELESS_PASSWORD_LEN];
    esp_err_t err = ESP_OK;
    bool station_configured = false;

    portENTER_CRITICAL(&s_wireless_lock);
    laser_controller_wireless_copy_text(
        station_ssid,
        sizeof(station_ssid),
        s_station_ssid);
    laser_controller_wireless_copy_text(
        station_password,
        sizeof(station_password),
        s_station_password);
    station_configured = station_ssid[0] != '\0';
    portEXIT_CRITICAL(&s_wireless_lock);

    if (mode == LASER_CONTROLLER_WIRELESS_MODE_STATION &&
        !station_configured) {
        laser_controller_wireless_set_errorf(
            "Station mode needs a configured SSID.");
        return ESP_ERR_INVALID_ARG;
    }

    laser_controller_wireless_clear_clients();
    err = esp_wifi_stop();
    if (err != ESP_OK &&
        err != ESP_ERR_WIFI_NOT_INIT &&
        err != ESP_ERR_WIFI_NOT_STARTED) {
        return err;
    }

    portENTER_CRITICAL(&s_wireless_lock);
    s_mode = mode;
    s_ap_ready = false;
    s_station_connecting = false;
    s_station_connected = false;
    s_station_rssi_dbm = 0;
    s_station_channel = 0U;
    s_started = false;
    if (mode == LASER_CONTROLLER_WIRELESS_MODE_STATION) {
        laser_controller_wireless_set_station_identity_locked();
        laser_controller_wireless_reset_endpoint_locked();
    } else {
        laser_controller_wireless_set_softap_identity_locked();
    }
    laser_controller_wireless_clear_error_locked();
    portEXIT_CRITICAL(&s_wireless_lock);

    if (mode == LASER_CONTROLLER_WIRELESS_MODE_SOFTAP) {
        wifi_config.ap.channel = LASER_CONTROLLER_WIRELESS_AP_CHANNEL;
        wifi_config.ap.max_connection = LASER_CONTROLLER_WIRELESS_MAX_CLIENTS;
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.ap.pmf_cfg.required = false;
        (void)strlcpy(
            (char *)wifi_config.ap.ssid,
            LASER_CONTROLLER_WIRELESS_AP_SSID,
            sizeof(wifi_config.ap.ssid));
        wifi_config.ap.ssid_len = strlen(LASER_CONTROLLER_WIRELESS_AP_SSID);
        (void)strlcpy(
            (char *)wifi_config.ap.password,
            LASER_CONTROLLER_WIRELESS_AP_PASSWORD,
            sizeof(wifi_config.ap.password));

        err = esp_wifi_set_mode(WIFI_MODE_AP);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_wifi_start();
        if (err != ESP_OK) {
            return err;
        }

        portENTER_CRITICAL(&s_wireless_lock);
        s_started = s_server != NULL;
        s_ap_ready = true;
        laser_controller_wireless_set_softap_identity_locked();
        portEXIT_CRITICAL(&s_wireless_lock);
        return ESP_OK;
    }

    (void)strlcpy(
        (char *)wifi_config.sta.ssid,
        station_ssid,
        sizeof(wifi_config.sta.ssid));
    (void)strlcpy(
        (char *)wifi_config.sta.password,
        station_password,
        sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode =
        station_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    portENTER_CRITICAL(&s_wireless_lock);
    s_started = s_server != NULL;
    s_station_connecting = true;
    s_station_connected = false;
    laser_controller_wireless_set_station_identity_locked();
    portEXIT_CRITICAL(&s_wireless_lock);

    return ESP_OK;
}

esp_err_t laser_controller_wireless_start(void)
{
    laser_controller_wireless_mode_t mode;
    esp_err_t err = ESP_OK;

    err = laser_controller_wireless_ensure_stack_ready();
    if (err != ESP_OK) {
        laser_controller_wireless_set_errorf(
            "Wireless stack init failed: %s",
            esp_err_to_name(err));
        return err;
    }

    portENTER_CRITICAL(&s_wireless_lock);
    laser_controller_wireless_load_config_locked();
    mode = s_mode;
    portEXIT_CRITICAL(&s_wireless_lock);

    laser_controller_wireless_start_http_server();
    if (s_server == NULL) {
        laser_controller_wireless_set_errorf("HTTP server start failed.");
        return ESP_FAIL;
    }

    err = laser_controller_wireless_apply_config(mode);
    if (err != ESP_OK &&
        mode == LASER_CONTROLLER_WIRELESS_MODE_STATION) {
        ESP_LOGW(
            TAG,
            "station start failed, falling back to softap: %s",
            esp_err_to_name(err));
        portENTER_CRITICAL(&s_wireless_lock);
        s_mode = LASER_CONTROLLER_WIRELESS_MODE_SOFTAP;
        portEXIT_CRITICAL(&s_wireless_lock);
        err = laser_controller_wireless_apply_config(
            LASER_CONTROLLER_WIRELESS_MODE_SOFTAP);
    }

    if (err != ESP_OK) {
        laser_controller_wireless_set_errorf(
            "Wireless start failed: %s",
            esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t laser_controller_wireless_configure(
    laser_controller_wireless_mode_t mode,
    const char *station_ssid,
    const char *station_password)
{
    esp_err_t err = ESP_OK;

    if (mode != LASER_CONTROLLER_WIRELESS_MODE_SOFTAP &&
        mode != LASER_CONTROLLER_WIRELESS_MODE_STATION) {
        return ESP_ERR_INVALID_ARG;
    }

    err = laser_controller_wireless_ensure_stack_ready();
    if (err != ESP_OK) {
        return err;
    }

    portENTER_CRITICAL(&s_wireless_lock);
    laser_controller_wireless_load_config_locked();
    s_mode = mode;
    if (station_ssid != NULL && station_ssid[0] != '\0') {
        laser_controller_wireless_copy_text(
            s_station_ssid,
            sizeof(s_station_ssid),
            station_ssid);
    }
    if (station_password != NULL) {
        laser_controller_wireless_copy_text(
            s_station_password,
            sizeof(s_station_password),
            station_password);
    }
    s_station_configured = s_station_ssid[0] != '\0';
    portEXIT_CRITICAL(&s_wireless_lock);

    if (mode == LASER_CONTROLLER_WIRELESS_MODE_STATION) {
        portENTER_CRITICAL(&s_wireless_lock);
        if (!s_station_configured) {
            portEXIT_CRITICAL(&s_wireless_lock);
            return ESP_ERR_INVALID_ARG;
        }
        portEXIT_CRITICAL(&s_wireless_lock);
    }

    err = laser_controller_wireless_save_config();
    if (err != ESP_OK) {
        laser_controller_wireless_set_errorf(
            "Wireless config save failed: %s",
            esp_err_to_name(err));
        return err;
    }

    laser_controller_wireless_start_http_server();
    if (s_server == NULL) {
        return ESP_FAIL;
    }

    err = laser_controller_wireless_apply_config(mode);
    if (err != ESP_OK) {
        laser_controller_wireless_set_errorf(
            "Wireless reconfigure failed: %s",
            esp_err_to_name(err));
    }
    return err;
}

esp_err_t laser_controller_wireless_scan_networks(void)
{
    wifi_scan_config_t scan_config = { 0 };
    wifi_mode_t mode = WIFI_MODE_NULL;
    wifi_ap_record_t ap_records[LASER_CONTROLLER_WIRELESS_SCAN_RESULT_COUNT] = { 0 };
    uint16_t result_count = LASER_CONTROLLER_WIRELESS_SCAN_RESULT_COUNT;
    bool restore_softap_mode = false;
    bool resume_station_after_scan = false;
    esp_err_t err = ESP_OK;

    err = laser_controller_wireless_ensure_stack_ready();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        laser_controller_wireless_set_errorf(
            "Wireless mode query failed: %s",
            esp_err_to_name(err));
        return err;
    }

    if (mode == WIFI_MODE_AP) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            laser_controller_wireless_set_errorf(
                "Wi-Fi scan prep failed: %s",
                esp_err_to_name(err));
            return err;
        }
        restore_softap_mode = true;
    }

    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_config.scan_time.active.min = 30U;
    scan_config.scan_time.active.max = 80U;

    portENTER_CRITICAL(&s_wireless_lock);
    s_scan_in_progress = true;
    laser_controller_wireless_clear_error_locked();
    resume_station_after_scan =
        s_mode == LASER_CONTROLLER_WIRELESS_MODE_STATION &&
        s_station_configured;
    portEXIT_CRITICAL(&s_wireless_lock);

    if ((mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) &&
        resume_station_after_scan) {
        (void)esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    err = esp_wifi_scan_start(&scan_config, true);
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_records(&result_count, ap_records);
    }

    portENTER_CRITICAL(&s_wireless_lock);
    s_scan_in_progress = false;
    if (err == ESP_OK) {
        memset(s_scan_results, 0, sizeof(s_scan_results));
        s_scan_result_count = (uint8_t)result_count;
        for (uint16_t index = 0U; index < result_count; ++index) {
            laser_controller_wireless_copy_text(
                s_scan_results[index].ssid,
                sizeof(s_scan_results[index].ssid),
                (const char *)ap_records[index].ssid);
            s_scan_results[index].rssi_dbm = ap_records[index].rssi;
            s_scan_results[index].channel = ap_records[index].primary;
            s_scan_results[index].secure =
                laser_controller_wireless_authmode_secure(ap_records[index].authmode);
        }
        laser_controller_wireless_clear_error_locked();
    } else {
        (void)snprintf(
            s_last_error,
            sizeof(s_last_error),
            "Wi-Fi scan failed (%s).",
            esp_err_to_name(err));
    }
    portEXIT_CRITICAL(&s_wireless_lock);

    if (restore_softap_mode) {
        const esp_err_t restore_err = esp_wifi_set_mode(WIFI_MODE_AP);
        if (restore_err != ESP_OK && err == ESP_OK) {
            laser_controller_wireless_set_errorf(
                "Wi-Fi scan restore failed: %s",
                esp_err_to_name(restore_err));
            err = restore_err;
        }
    }

    if ((mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) &&
        resume_station_after_scan) {
        portENTER_CRITICAL(&s_wireless_lock);
        s_station_connecting = true;
        portEXIT_CRITICAL(&s_wireless_lock);
        (void)esp_wifi_connect();
    }

    return err;
}

void laser_controller_wireless_broadcast_text(const char *line)
{
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)(line != NULL ? line : ""),
        .len = line != NULL ? strlen(line) : 0U,
    };
    int fds[LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS] = { -1, -1 };

    if (s_server == NULL || line == NULL || line[0] == '\0') {
        return;
    }

    portENTER_CRITICAL(&s_client_lock);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        fds[index] = s_client_fds[index];
    }
    portEXIT_CRITICAL(&s_client_lock);

    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        if (fds[index] < 0) {
            continue;
        }

        if (httpd_ws_send_frame_async(s_server, fds[index], &frame) != ESP_OK) {
            laser_controller_wireless_remove_client(fds[index]);
        }
    }
}

bool laser_controller_wireless_has_clients(void)
{
    return laser_controller_wireless_count_clients() != 0U;
}

void laser_controller_wireless_copy_status(
    laser_controller_wireless_status_t *status)
{
    bool refresh_station_metrics = false;

    if (status == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_wireless_lock);
    refresh_station_metrics = s_station_connected;
    portEXIT_CRITICAL(&s_wireless_lock);

    if (refresh_station_metrics) {
        laser_controller_wireless_refresh_station_metrics();
    }

    memset(status, 0, sizeof(*status));

    portENTER_CRITICAL(&s_wireless_lock);
    status->started = s_started;
    status->mode = s_mode;
    status->ap_ready = s_ap_ready;
    status->station_configured = s_station_configured;
    status->station_connecting = s_station_connecting;
    status->station_connected = s_station_connected;
    laser_controller_wireless_copy_text(
        status->ssid,
        sizeof(status->ssid),
        s_active_ssid);
    laser_controller_wireless_copy_text(
        status->station_ssid,
        sizeof(status->station_ssid),
        s_station_ssid);
    status->station_rssi_dbm = s_station_rssi_dbm;
    status->station_channel = s_station_channel;
    status->scan_in_progress = s_scan_in_progress;
    status->scan_result_count = s_scan_result_count;
    memcpy(
        status->scan_results,
        s_scan_results,
        sizeof(status->scan_results));
    laser_controller_wireless_copy_text(
        status->ip_address,
        sizeof(status->ip_address),
        s_ip_address);
    laser_controller_wireless_copy_text(
        status->ws_url,
        sizeof(status->ws_url),
        s_ws_url);
    laser_controller_wireless_copy_text(
        status->last_error,
        sizeof(status->last_error),
        s_last_error);
    portEXIT_CRITICAL(&s_wireless_lock);

    status->client_count = laser_controller_wireless_count_clients();
}
