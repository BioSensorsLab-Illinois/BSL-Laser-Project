#include "laser_controller_wireless.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "laser_controller_comms.h"

#define LASER_CONTROLLER_WIRELESS_AP_SSID       "BSL-HTLS-Bench"
#define LASER_CONTROLLER_WIRELESS_AP_PASSWORD   "bslbench2026"
#define LASER_CONTROLLER_WIRELESS_AP_CHANNEL    6
#define LASER_CONTROLLER_WIRELESS_MAX_CLIENTS   1
#define LASER_CONTROLLER_WIRELESS_WS_URI        "/ws"
#define LASER_CONTROLLER_WIRELESS_WS_URL        "ws://192.168.4.1/ws"
#define LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS  2
#define LASER_CONTROLLER_WIRELESS_MAX_FRAME_LEN 768U

static const char *TAG = "laser_wireless";

static httpd_handle_t s_server;
static bool s_started;
static bool s_ap_ready;
static int s_client_fds[LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS] = { -1, -1 };
static portMUX_TYPE s_client_lock = portMUX_INITIALIZER_UNLOCKED;

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
    static const char *response =
        "{"
        "\"ssid\":\"" LASER_CONTROLLER_WIRELESS_AP_SSID "\","
        "\"wsUrl\":\"" LASER_CONTROLLER_WIRELESS_WS_URL "\""
        "}";

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

static void laser_controller_wireless_init_wifi_ap(void)
{
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = {
        .ap = {
            .channel = LASER_CONTROLLER_WIRELESS_AP_CHANNEL,
            .max_connection = LASER_CONTROLLER_WIRELESS_MAX_CLIENTS,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    esp_err_t err = ESP_OK;

    (void)strlcpy(
        (char *)wifi_config.ap.ssid,
        LASER_CONTROLLER_WIRELESS_AP_SSID,
        sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(LASER_CONTROLLER_WIRELESS_AP_SSID);
    (void)strlcpy(
        (char *)wifi_config.ap.password,
        LASER_CONTROLLER_WIRELESS_AP_PASSWORD,
        sizeof(wifi_config.ap.password));

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
        return;
    }

    (void)esp_netif_create_default_wifi_ap();

    err = esp_wifi_init(&wifi_init_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi mode set failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi config failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err));
        return;
    }

    s_ap_ready = true;
}

esp_err_t laser_controller_wireless_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    laser_controller_wireless_init_wifi_ap();
    laser_controller_wireless_start_http_server();
    s_started = s_ap_ready && s_server != NULL;

    return s_started ? ESP_OK : ESP_FAIL;
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

void laser_controller_wireless_copy_status(
    laser_controller_wireless_status_t *status)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->started = s_started;
    status->ap_ready = s_ap_ready;
    status->client_count = laser_controller_wireless_count_clients();
    (void)strlcpy(status->ssid, LASER_CONTROLLER_WIRELESS_AP_SSID, sizeof(status->ssid));
    (void)strlcpy(status->ws_url, LASER_CONTROLLER_WIRELESS_WS_URL, sizeof(status->ws_url));
}
