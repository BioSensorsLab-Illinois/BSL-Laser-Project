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
#include "lwip/sockets.h"
#include "nvs.h"

#include "laser_controller_app.h"
#include "laser_controller_comms.h"

#define LASER_CONTROLLER_WIRELESS_AP_SSID          "BSL-HTLS-Bench"
#define LASER_CONTROLLER_WIRELESS_AP_PASSWORD      "bslbench2026"
#define LASER_CONTROLLER_WIRELESS_AP_CHANNEL       6
/*
 * AP max_connection bumped 1 -> 4 (2026-04-16): refresh-driven
 * re-association overlap was rejecting the new association before the
 * old one fully tore down, leaving the GUI unable to reconnect.
 */
#define LASER_CONTROLLER_WIRELESS_MAX_CLIENTS      4
#define LASER_CONTROLLER_WIRELESS_WS_URI           "/ws"
#define LASER_CONTROLLER_WIRELESS_AP_WS_URL        "ws://192.168.4.1/ws"
/*
 * CLIENT_SLOTS bumped 2 -> 4 (2026-04-16): two stale refreshes used
 * to fill both slots with dead FDs; new connections still work via
 * LRU eviction but the broadcast loop wasted time on the dead FDs
 * until CLIENT_DROP_FAILURES accumulated. Headroom plus the new
 * close callback (registered in start_http_server) keep slots clean.
 */
#define LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS     4
/*
 * WS receive-frame buffer. 768 was the pre-2026-04-17 value; any command
 * payload larger than this was rejected with ESP_ERR_INVALID_SIZE from
 * the URI handler, which in ESP-IDF httpd_ws tears the WebSocket
 * session down (TCP RST) and surfaces to the host as "Wireless
 * controller link dropped". That bit us after the `integrate.set_safety`
 * payload grew past 1 KB with the interlock-enable mask (10 new
 * booleans + `tof_low_bound_only`) on top of the existing ~20 numeric
 * policy fields. Raising to 2048 leaves ample headroom for every
 * command the host currently issues, while still rejecting pathological
 * > 2 KB commands to bound the per-session stack allocation at 731.
 */
#define LASER_CONTROLLER_WIRELESS_MAX_FRAME_LEN    2048U
#define LASER_CONTROLLER_WIRELESS_NVS_NAMESPACE    "laser_wifi"
#define LASER_CONTROLLER_WIRELESS_NVS_KEY          "config"
#define LASER_CONTROLLER_WIRELESS_CONFIG_VERSION   1U
#define LASER_CONTROLLER_WIRELESS_PASSWORD_LEN     65U
/*
 * Drop threshold tightened 4 -> 2 (2026-04-16): defense in depth in
 * case the close callback doesn't fire (e.g. on TCP RST without FIN).
 *
 * Tightened further 2 -> 1 (2026-04-17) after the WS stability audit:
 * we now also call httpd_sess_trigger_close() on the FIRST send failure
 * in broadcast_text(), so there is no reason to retry a second time.
 * Keeping a stale FD alive for a second broadcast pass was exactly the
 * head-of-line stall that blocked service/deployment-mode entry and
 * the browser-refresh reconnect path: while one zombie FD sat in send()
 * for up to HTTPD_SEND_WAIT_TIMEOUT seconds, every other client saw no
 * data, the output mutex stayed held, and the command_task queue
 * stopped draining.
 */
#define LASER_CONTROLLER_WIRELESS_CLIENT_DROP_FAILURES 1U

/*
 * Per-send timeout for WS frames. Must be < command ack timeout and
 * short enough that a dead client doesn't wedge the broadcast loop for
 * the full ESP-IDF default (5 s). 1500 ms is a compromise: long enough
 * that a congested but alive Wi-Fi link still completes one fragment,
 * short enough that a dead FD is evicted in well under the operator's
 * 5 s command ack deadline.
 */
/*
 * Tightened 2 -> 1 (2026-04-17 late) after the stuck-command-queue
 * incident: the httpd task serializes ALL WS sends (Stage 2). A single
 * slow or dead client can block the httpd task for up to this timeout,
 * stalling broadcasts AND per-client ACKs to ALL clients. 2s was too
 * generous; 1s is the right compromise for a Wi-Fi AP-mode link on a
 * busy bench.
 */
#define LASER_CONTROLLER_WIRELESS_SEND_WAIT_TIMEOUT_S 1
/*
 * TCP keep-alive probe parameters. On a quiet link we want to detect a
 * dead peer in ~9 s rather than the Linux default (hours). The server
 * side does not send heartbeat frames; the TCP layer does it for us.
 */
#define LASER_CONTROLLER_WIRELESS_KEEP_ALIVE_IDLE_S     5
#define LASER_CONTROLLER_WIRELESS_KEEP_ALIVE_INTERVAL_S 2
#define LASER_CONTROLLER_WIRELESS_KEEP_ALIVE_COUNT      2

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
static int s_client_fds[LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS] = { -1, -1, -1, -1 };
static uint8_t s_client_send_failures[LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS] = { 0U, 0U, 0U, 0U };
/*
 * Diagnostic counters for the Stage 2 async broadcast path. Increment
 * from the producer task (no lock; single-writer per counter is fine
 * since we only report them, never act on them):
 *   - broadcast_oom:        malloc failed for the heap-dup copy
 *   - broadcast_queue_fail: httpd_queue_work returned non-ESP_OK
 *     (control socket full or httpd task not running)
 * Both outcomes DROP the frame rather than running inline on the
 * caller's task, which was the frame-splice / command-queue-wedge
 * source in the 2026-04-17 session archive.
 */
static uint32_t s_broadcast_drops_oom = 0U;
static uint32_t s_broadcast_drops_queue_fail = 0U;
static uint32_t s_client_generations[LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS] = { 0U, 0U, 0U, 0U };
static uint32_t s_client_generation_counter;
/*
 * 2026-04-17: set by the WS GET handler whenever a new (or recycled)
 * client lands in our slot table. The comms TX task observes and
 * clears this; when set, the next TX tick emits a full status snapshot
 * unconditionally so the new client does not wait up to
 * COMMS_WIRELESS_FAST_TELEMETRY_PERIOD_MS (180 ms) or the post-command
 * quiet window (400 ms) before seeing any data. Without this, a page
 * refresh during a command-in-flight window produced the
 * "WS opens but no JSON handshake arrives" symptom reported in the
 * field.
 */
static volatile bool s_new_client_snapshot_pending;
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

static void laser_controller_wireless_force_close_all_clients(void);

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
        s_client_send_failures[index] = 0U;
        s_client_generations[index] = 0U;
    }
    portEXIT_CRITICAL(&s_client_lock);
}

static void laser_controller_wireless_add_client(int fd)
{
    ssize_t empty_index = -1;
    size_t oldest_index = 0U;
    uint32_t oldest_generation = UINT32_MAX;
    bool is_new_slot = false;

    portENTER_CRITICAL(&s_client_lock);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        if (s_client_fds[index] == fd) {
            s_client_send_failures[index] = 0U;
            s_client_generations[index] = ++s_client_generation_counter;
            portEXIT_CRITICAL(&s_client_lock);
            return;
        }
        if (s_client_fds[index] < 0) {
            if (empty_index < 0) {
                empty_index = (ssize_t)index;
            }
            continue;
        }

        if (s_client_generations[index] < oldest_generation) {
            oldest_generation = s_client_generations[index];
            oldest_index = index;
        }
    }

    const size_t target_index =
        empty_index >= 0 ? (size_t)empty_index : oldest_index;
    s_client_fds[target_index] = fd;
    s_client_send_failures[target_index] = 0U;
    s_client_generations[target_index] = ++s_client_generation_counter;
    is_new_slot = true;
    portEXIT_CRITICAL(&s_client_lock);

    if (is_new_slot) {
        /*
         * Flag a fresh-snapshot request outside the critical section —
         * comms/TX observes this on its next tick. Safe to set
         * unconditionally: the write is idempotent and the volatile
         * store doesn't need the lock.
         */
        s_new_client_snapshot_pending = true;
    }
}

static void laser_controller_wireless_remove_client(int fd)
{
    portENTER_CRITICAL(&s_client_lock);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        if (s_client_fds[index] == fd) {
            s_client_fds[index] = -1;
            s_client_send_failures[index] = 0U;
            s_client_generations[index] = 0U;
        }
    }
    portEXIT_CRITICAL(&s_client_lock);
}

static void laser_controller_wireless_note_send_result(int fd, bool ok)
{
    portENTER_CRITICAL(&s_client_lock);
    for (size_t index = 0U;
         index < LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS;
         ++index) {
        if (s_client_fds[index] != fd) {
            continue;
        }

        if (ok) {
            s_client_send_failures[index] = 0U;
            s_client_generations[index] = ++s_client_generation_counter;
        } else if (s_client_send_failures[index] + 1U >=
                   LASER_CONTROLLER_WIRELESS_CLIENT_DROP_FAILURES) {
            s_client_fds[index] = -1;
            s_client_send_failures[index] = 0U;
            s_client_generations[index] = 0U;
        } else {
            s_client_send_failures[index] += 1U;
        }
        break;
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
            /*
             * Tear down every tracked WS session on AP stop. Once the
             * AP is torn down the existing TCP sockets are dead; we
             * were leaving them in s_client_fds until the next
             * broadcast blew up with two failed sends. (2026-04-17)
             */
            laser_controller_wireless_force_close_all_clients();
            return;
        }

        if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            /*
             * Reverted 2026-04-17 evening — bulk-closing every tracked
             * client on STADISCONNECTED nuked the live browser WS on
             * every transient roaming / beacon-miss / power-save hiccup,
             * which manifested as `clientCount=0` seconds after a
             * successful WS upgrade and zero telemetry reaching the
             * browser even though commands still ack'd via direct
             * request-response. Let TCP keep-alive (configured in
             * start_http_server) + close-on-send-failure handle stale
             * sockets. Those paths are O(9 s) worst case, which is
             * acceptable; bulk-close was "O(now) but wrong".
             */
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

/*
 * WebSocket post-handshake callback. Fires on the httpd task AFTER the
 * framework sends the 101 Switching Protocols and flips ws_handshake_done
 * to true. This is the ONLY reliable hook for "a new WS client just
 * connected" — per ESP-IDF httpd_uri.c:353, the framework explicitly
 * does NOT invoke the user ws_handler on the initial upgrade GET. So
 * without this callback, our s_client_fds[] tracker only learned about
 * a client once it sent its first TEXT frame. If the client stayed
 * silent (which a listen-only client legitimately may), no telemetry
 * was ever broadcast to it — the wedge the AP-only audit found
 * 2026-04-17 evening. (2026-04-17)
 */
static esp_err_t laser_controller_wireless_ws_post_handshake(httpd_req_t *req)
{
    laser_controller_wireless_add_client(httpd_req_to_sockfd(req));
    /*
     * 2026-04-17 (audit round 2, S2): a connected WS client IS a host.
     * Stamp host activity so the headless auto-deploy at t=5s does NOT
     * fire if an operator has the console open but is still in the
     * pre-command ramp-up window. Without this, a listen-only WS
     * client (still legitimate — the operator may just be watching
     * telemetry) let auto-deploy run autonomously while the UI thought
     * it owned the session, causing a surprise deployment the
     * operator did not initiate.
     */
    laser_controller_app_note_host_activity();
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
        /*
         * Defensive: on some IDF paths the framework routes a refresh
         * through here. Keep add_client idempotent. The primary upgrade
         * hook is now the post-handshake callback above. (2026-04-17)
         */
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
        laser_controller_wireless_add_client(httpd_req_to_sockfd(req));
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

/*
 * httpd close callback. Fires when ESP-IDF tears down a session — TCP
 * FIN, RST, lru-purge, or explicit close.
 *
 * Reversal of 2026-04-17 morning note (same day): we now DO call
 * close(sockfd) here. Per ESP-IDF httpd_sess.c:373-378, when a
 * close_fn is registered, httpd delegates the actual close() to the
 * callback instead of calling it itself:
 *     if (hd->config.close_fn) { hd->config.close_fn(hd, session->fd); }
 *     else { close(session->fd); }
 * If we DO NOT call close(), the socket FD leaks forever. Under the
 * stress of repeated page-refresh / multi-client cycles, N open/close
 * pairs leak N FDs, LwIP's VFS socket table fills, and httpd's next
 * accept() returns RST — producing the exact "new sessions fail after
 * several refreshes" wedge the field reported.
 *
 * The FD-reuse race the earlier note feared — "a fresh WS connection
 * landed on the same FD number while our close was in flight, causing
 * the new session's first frame to fail and the GUI to hang on
 * 'Waiting for controller firmware handshake…'" — is now handled by
 * the new-client snapshot flag (see `add_client` +
 * `laser_controller_wireless_consume_new_client_pending` + the TX
 * task's force-snapshot path). Even if a broadcast ships to a reused
 * fd before our tracker is updated, the fresh `add_client` bumps the
 * snapshot flag and the very next TX tick emits a correct
 * status_snapshot to the new peer. The transient garbage frame is
 * harmless: it's a snapshot event, not a state-changing command.
 * (2026-04-17, revised)
 */
static void laser_controller_wireless_session_closed(
    httpd_handle_t hd, int sockfd)
{
    (void)hd;
    laser_controller_wireless_remove_client(sockfd);
    (void)close(sockfd);
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
        .ws_post_handshake_cb = laser_controller_wireless_ws_post_handshake,
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

    /*
     * max_open_sockets bumped 4 -> 7 (2026-04-16): the ESP-IDF default
     * cap is 7. Reaching the previous limit during a refresh-storm left
     * httpd unable to accept the new TCP connection until lru-purge ran.
     */
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.close_fn = laser_controller_wireless_session_closed;
    /*
     * Bound the per-send blocking time (2026-04-17). Default is 5 s;
     * a dead FD in the middle of the broadcast loop held our
     * s_output_lock for that long while staying blocked in send(),
     * blocking every other telemetry emit and every command response.
     */
    config.send_wait_timeout = LASER_CONTROLLER_WIRELESS_SEND_WAIT_TIMEOUT_S;
    config.recv_wait_timeout = 5;
    /*
     * Enable TCP keep-alive so a dead peer (laptop lid close, radio
     * off, abrupt app kill) is detected in ~9 s instead of waiting
     * for the next send() to fail. Combined with the
     * AP_STADISCONNECTED hook above, this closes the gap where a
     * disassociated station's TCP socket lived on until the next
     * broadcast attempt. (2026-04-17)
     */
    config.keep_alive_enable = true;
    config.keep_alive_idle = LASER_CONTROLLER_WIRELESS_KEEP_ALIVE_IDLE_S;
    config.keep_alive_interval =
        LASER_CONTROLLER_WIRELESS_KEEP_ALIVE_INTERVAL_S;
    config.keep_alive_count = LASER_CONTROLLER_WIRELESS_KEEP_ALIVE_COUNT;

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
    wifi_config_t ap_config = { 0 };
    wifi_config_t sta_config = { 0 };
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
        ap_config.ap.channel = LASER_CONTROLLER_WIRELESS_AP_CHANNEL;
        ap_config.ap.max_connection = LASER_CONTROLLER_WIRELESS_MAX_CLIENTS;
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        ap_config.ap.pmf_cfg.required = false;
        (void)strlcpy(
            (char *)ap_config.ap.ssid,
            LASER_CONTROLLER_WIRELESS_AP_SSID,
            sizeof(ap_config.ap.ssid));
        ap_config.ap.ssid_len = strlen(LASER_CONTROLLER_WIRELESS_AP_SSID);
        (void)strlcpy(
            (char *)ap_config.ap.password,
            LASER_CONTROLLER_WIRELESS_AP_PASSWORD,
            sizeof(ap_config.ap.password));

        err = esp_wifi_set_mode(WIFI_MODE_AP);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
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
        (char *)sta_config.sta.ssid,
        station_ssid,
        sizeof(sta_config.sta.ssid));
    (void)strlcpy(
        (char *)sta_config.sta.password,
        station_password,
        sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode =
        station_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    ap_config.ap.channel = LASER_CONTROLLER_WIRELESS_AP_CHANNEL;
    ap_config.ap.max_connection = LASER_CONTROLLER_WIRELESS_MAX_CLIENTS;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.pmf_cfg.required = false;
    (void)strlcpy(
        (char *)ap_config.ap.ssid,
        LASER_CONTROLLER_WIRELESS_AP_SSID,
        sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(LASER_CONTROLLER_WIRELESS_AP_SSID);
    (void)strlcpy(
        (char *)ap_config.ap.password,
        LASER_CONTROLLER_WIRELESS_AP_PASSWORD,
        sizeof(ap_config.ap.password));

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
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

/*
 * Inline (synchronous) broadcast — iterates tracked client FDs and
 * sends `line` to each. This is the slow path: the WS send can block
 * up to `send_wait_timeout` seconds per dead FD. Retained because
 * `httpd_queue_work` has a bounded control socket and may reject work
 * under heavy backpressure; in that case we fall back to inline send.
 */
static void laser_controller_wireless_broadcast_text_inline(const char *line)
{
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)(line != NULL ? line : ""),
        .len = line != NULL ? strlen(line) : 0U,
    };
    int fds[LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS] = { -1, -1, -1, -1 };

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

        const bool ok =
            httpd_ws_send_frame_async(s_server, fds[index], &frame) == ESP_OK;
        laser_controller_wireless_note_send_result(fds[index], ok);
        if (!ok) {
            (void)httpd_sess_trigger_close(s_server, fds[index]);
        }
    }
}

/*
 * Async broadcast work. Runs on the httpd task via
 * `httpd_queue_work`. The producer (comms TX, command response,
 * log emit, etc.) builds the frame into a heap-allocated copy, hands
 * it to this work item, and returns immediately — without holding
 * `s_output_lock` across the potentially-multi-second WS send. That
 * is the Stage 2 architectural fix: WS latency no longer stalls every
 * other emitter in the firmware. (2026-04-17)
 */
typedef struct {
    char *line;
} laser_controller_wireless_broadcast_work_t;

static void laser_controller_wireless_broadcast_work_fn(void *arg)
{
    laser_controller_wireless_broadcast_work_t *work =
        (laser_controller_wireless_broadcast_work_t *)arg;
    if (work == NULL) {
        return;
    }
    laser_controller_wireless_broadcast_text_inline(work->line);
    if (work->line != NULL) {
        free(work->line);
    }
    free(work);
}

void laser_controller_wireless_broadcast_text(const char *line)
{
    if (s_server == NULL || line == NULL || line[0] == '\0') {
        return;
    }
    /*
     * Skip the heap allocation entirely if there are no tracked
     * clients — no one to send to. This saves every
     * emit_fast_telemetry tick from doing a strdup+free when the
     * bench is USB-only with no WS peer.
     */
    if (!laser_controller_wireless_has_clients()) {
        return;
    }

    const size_t len = strlen(line);
    laser_controller_wireless_broadcast_work_t *work =
        (laser_controller_wireless_broadcast_work_t *)malloc(sizeof(*work));
    if (work == NULL) {
        s_broadcast_drops_oom += 1U;
        return;
    }
    work->line = (char *)malloc(len + 1U);
    if (work->line == NULL) {
        free(work);
        s_broadcast_drops_oom += 1U;
        return;
    }
    memcpy(work->line, line, len + 1U);

    if (httpd_queue_work(
            s_server,
            laser_controller_wireless_broadcast_work_fn,
            work) == ESP_OK) {
        return;  /* async path: the only path. */
    }
    /*
     * httpd control socket could not accept the work item. Drop the
     * frame. DO NOT run inline on the caller's task: inline sends race
     * with the httpd task's concurrent drain of queued work (both
     * write to the same WS socket), producing byte-interleaved frames
     * on the wire. That race wedged the command queue and produced
     * spliced "tof{..resp..}" console logs in session
     * 2026-04-17T19:37:50Z. Drop + counter is the safe behavior.
     *
     * Telemetry producers are resilient to drops: live_telemetry,
     * status_snapshot, and fast_telemetry all emit periodically and
     * the host reconciles on the next arrival. Command ACK drops are
     * handled by the host's ACK timeout + retry path.
     */
    free(work->line);
    free(work);
    s_broadcast_drops_queue_fail += 1U;
}

bool laser_controller_wireless_has_clients(void)
{
    return laser_controller_wireless_count_clients() != 0U;
}

bool laser_controller_wireless_consume_new_client_pending(void)
{
    /*
     * Read-then-clear. A race with a second client adding itself at
     * the same moment is benign: worst case we emit one snapshot that
     * covers both clients.
     */
    if (!s_new_client_snapshot_pending) {
        return false;
    }
    s_new_client_snapshot_pending = false;
    return true;
}

/*
 * Close every tracked WS session via httpd's own teardown path. The
 * httpd task will fire close_fn() which calls remove_client(). This
 * is safe to invoke from any task (event handler, comms, or control)
 * — httpd_sess_trigger_close only marks the session; the close
 * itself happens on the httpd task.
 *
 * A sockfd that is not a known session is a no-op (IDF returns
 * ESP_ERR_NOT_FOUND without side-effects), so calling on a stale FD
 * after the kernel already reaped it is harmless.
 */
static void laser_controller_wireless_force_close_all_clients(void)
{
    int fds[LASER_CONTROLLER_WIRELESS_CLIENT_SLOTS] = { -1, -1, -1, -1 };

    if (s_server == NULL) {
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
        (void)httpd_sess_trigger_close(s_server, fds[index]);
    }
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
