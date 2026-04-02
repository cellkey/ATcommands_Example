/**
 * @file wifi_manager.c
 * @brief WiFi STA connection manager.
 *
 * Lifecycle:
 *   1. wifi_manager_init()  →  reads NVS, inits stack, calls esp_wifi_start()
 *   2. WIFI_EVENT_STA_START →  esp_wifi_connect()
 *   3. IP_EVENT_STA_GOT_IP  →  state = CONNECTED, stores IP string
 *   4. WIFI_EVENT_STA_DISCONNECTED → state = DISCONNECTED, schedules 5-s retry via esp_timer
 *   5. Timer fires → esp_wifi_connect()  (loop back to step 3 on success)
 */

#include "wifi_manager.h"    /* WIFI_DEFAULT_SSID / WIFI_DEFAULT_PASS / WIFI_STATUS_LED_GPIO */
#include "nvs_config.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "WIFI_MGR";

/* ── module state ───────────────────────────────────────────────────── */
static volatile wifi_mgr_state_t s_state           = WIFI_MGR_STATE_IDLE;
static volatile bool             s_server_connected = false;
static char                      s_ip_str[16]      = {0};   /* "xxx.xxx.xxx.xxx\0" */
static esp_timer_handle_t        s_reconnect_timer  = NULL;

/* ── WiFi status LED task ───────────────────────────────────────────── */
/*
 *  Not connected to network  →  steady ON
 *  Connected to network      →  blink 500 ms ON / 500 ms OFF
 *  Connected to server       →  blink 200 ms ON / 1500 ms OFF
 */
static void wifi_status_led_task(void *arg)
{
    (void)arg;
    gpio_reset_pin(WIFI_STATUS_LED_GPIO);
    gpio_set_direction(WIFI_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(WIFI_STATUS_LED_GPIO, 1);   /* start: steady ON (not yet connected) */

    while (1) {
        if (s_server_connected) {
            /* Server TCP up → 200 ms ON / 1500 ms OFF */
            gpio_set_level(WIFI_STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(WIFI_STATUS_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(1500));
        } else if (s_state == WIFI_MGR_STATE_CONNECTED) {
            /* Local network only → 500 ms ON / 500 ms OFF */
            gpio_set_level(WIFI_STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(WIFI_STATUS_LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            /* Not connected → steady ON, poll every 100 ms for state change */
            gpio_set_level(WIFI_STATUS_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ── reconnect timer callback ───────────────────────────────────────── */
static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (s_state == WIFI_MGR_STATE_DISCONNECTED) {
        ESP_LOGI(TAG, "Reconnect attempt...");
        esp_wifi_connect();
    }
}

/* ── unified event handler ──────────────────────────────────────────── */
static void wifi_event_handler(void *arg,
                                esp_event_base_t event_base,
                                int32_t          event_id,
                                void            *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {

        case WIFI_EVENT_STA_START:
            s_state = WIFI_MGR_STATE_CONNECTING;
            ESP_LOGI(TAG, "STA started – connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            /* Associated; wait for DHCP (IP_EVENT_STA_GOT_IP). */
            ESP_LOGI(TAG, "STA associated – awaiting IP...");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d =
                (wifi_event_sta_disconnected_t *)event_data;
            s_ip_str[0] = '\0';
            s_state     = WIFI_MGR_STATE_DISCONNECTED;
            ESP_LOGW(TAG, "Disconnected (reason %d) – retry in 5 s", (int)d->reason);
            if (s_reconnect_timer) {
                /* 5 000 000 µs = 5 s */
                esp_timer_start_once(s_reconnect_timer, 5ULL * 1000000ULL);
            }
            break;
        }

        default:
            break;
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        s_state = WIFI_MGR_STATE_CONNECTED;
        ESP_LOGI(TAG, "Connected – IP: %s", s_ip_str);
    }
}

/* ── public API ─────────────────────────────────────────────────────── */

bool wifi_manager_init(void)
{
    /* Read credentials: NVS value wins; compiled default is the fallback. */
    char ssid[WIFI_MGR_SSID_MAX] = {0};
    char pass[WIFI_MGR_PASS_MAX] = {0};

    bool ssid_from_nvs = nvs_config_get_string(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid), WIFI_DEFAULT_SSID);
    bool pass_from_nvs = nvs_config_get_string(NVS_KEY_WIFI_PASS, pass, sizeof(pass), WIFI_DEFAULT_PASS);

    if (ssid[0] == '\0') {
        /* Both NVS and the compiled default are empty – nothing to connect to. */
        ESP_LOGW(TAG, "No SSID configured (NVS empty, WIFI_DEFAULT_SSID not set) – WiFi not started");
        return false;
    }

    ESP_LOGI(TAG, "Init – SSID: \"%s\" (%s)  pass: %s",
             ssid,
             ssid_from_nvs ? "NVS"     : "default",
             pass_from_nvs ? "from NVS" : (pass[0] ? "default" : "none"));

    /* ── esp_netif ── init once (safe to call if already done by another component) */
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(err));
        return false;
    }

    /* ── default event loop ── create once */
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(err));
        return false;
    }

    /* ── netif STA object ── */
    esp_netif_create_default_wifi_sta();

    /* ── WiFi driver ── */
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    /* ── event handlers ── */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT,   IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    /* ── reconnect timer (created once here, started on disconnect) ── */
    esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name     = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    /* ── WiFi config ── */
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid,     ssid, sizeof(wifi_cfg.sta.ssid)     - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);

    /* Allow open networks (no password) and WPA/WPA2 mixed. */
    wifi_cfg.sta.threshold.authmode =
        (pass[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());  /* triggers WIFI_EVENT_STA_START → connect */

    s_state = WIFI_MGR_STATE_CONNECTING;

    /* Status LED task: lightweight, low priority. */
    xTaskCreate(wifi_status_led_task, "wifi_led", 1536, NULL, 3, NULL);

    return true;
}

bool wifi_manager_is_connected(void)
{
    return s_state == WIFI_MGR_STATE_CONNECTED;
}

wifi_mgr_state_t wifi_manager_get_state(void)
{
    return s_state;
}

bool wifi_manager_get_ip(char *buf, size_t size)
{
    if (!buf || size == 0) return false;
    buf[0] = '\0';
    if (s_state != WIFI_MGR_STATE_CONNECTED || s_ip_str[0] == '\0') return false;
    strncpy(buf, s_ip_str, size - 1);
    buf[size - 1] = '\0';
    return true;
}

void wifi_manager_set_server_connected(bool connected)
{
    s_server_connected = connected;
    ESP_LOGI(TAG, "Server connection: %s", connected ? "UP (800/200 blink)" : "DOWN (500/500 blink)");
}
