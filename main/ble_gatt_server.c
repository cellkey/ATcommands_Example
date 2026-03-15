/**
 * @file ble_gatt_server.c
 * @brief BLE GATT server (from EG_BLE_server). JSON commands with # terminator;
 *        encrypted_data validation; relay executed immediately on parse; disconnect 5s after message.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ble_gatt_server.h"
#include "a7670e_config.h"
#include "nvs_config.h"
#include "relay_control.h"
#include "encryption.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "driver/gpio.h"
#include "esp_task_wdt.h"

static const char *TAG = "BLE_GATT";

/* BLE status LED: blink when not connected, solid ON when BLE connected.
 * Set STATUS_LED_ACTIVE_LOW if your LED is on when GPIO is LOW (e.g. common anode). */
#define STATUS_LED_GPIO     23
#define STATUS_LED_BLINK_MS 600
/* #define STATUS_LED_ACTIVE_LOW   uncomment if LED is active-low */

#define GATTS_SERVICE_UUID  0xFFE0
#define GATTS_CHAR_UUID_RX  0xFFE1
#define GATTS_CHAR_UUID_TX  0xFFE2
#define GATTS_NUM_HANDLE    6
#define MAX_DATA_LEN        50
#define MSG_BUF_LEN         64
#define NOTIFICATION_QUEUE_SIZE 10
/** Delay before we initiate disconnect after app message (app typically closes in ~2.5s). */
#define BLE_DISCONNECT_AFTER_MSG_MS  5000

typedef struct {
    char message[MSG_BUF_LEN];
    size_t length;
    bool is_response;
} notification_msg_t;

/* Unit ID from NVS (read at BLE init). Same NVS key as modem GET – single source for BLE name and GET /api/device/<id>/listen. */
static char s_unit_id[NVS_CONFIG_MAX_LEN];

/* Encryption: unit ID numeric part -> seed -> encrypted_data (for app auth) */
char encrypted_data[10];
unsigned long rnd;
unsigned long *rnd_ptr;

static uint16_t service_handle;
static uint16_t char1_handle;
static uint16_t char2_handle;

static uint8_t received_data[MAX_DATA_LEN + 1];
static char message_buffer[MSG_BUF_LEN];
static int message_index = 0;

static QueueHandle_t notification_queue = NULL;
static TaskHandle_t notification_task_handle = NULL;
static bool notification_task_running = false;
static TaskHandle_t connection_timeout_task_handle = NULL;
static bool connection_timeout_running = false;
static bool message_received_flag = false;

static TaskHandle_t disconnect_delay_task_handle = NULL;

static TaskHandle_t status_led_task_handle = NULL;
static bool status_led_state = false;

static uint16_t conn_id = 0;
static volatile bool is_connected = false;
static esp_gatt_if_t gatt_if_for_send = 0;
static bool notify_enabled = false;

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void notification_task(void *arg);
static esp_err_t start_notification_service(void);
static esp_err_t stop_notification_service(void);
static esp_err_t start_connection_timeout(void);
static esp_err_t stop_connection_timeout(void);
static void disconnect_delay_task(void *arg);
static void start_disconnect_delay(void);

/* ------------------------------------------------------------------------- */
/* Status LED: blink when not connected, ON when BLE connected                */
/* ------------------------------------------------------------------------- */

static void status_led_task(void *arg)
{
    (void)arg;
    if (esp_task_wdt_add(NULL) != ESP_OK) {
        ESP_LOGW(TAG, "Status LED task: WDT add failed");
    }
    gpio_reset_pin(STATUS_LED_GPIO);
    gpio_set_direction(STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
#ifdef STATUS_LED_ACTIVE_LOW
    gpio_set_level(STATUS_LED_GPIO, 1);   /* OFF = HIGH when active-low */
#else
    gpio_set_level(STATUS_LED_GPIO, 0);   /* OFF = LOW when active-high */
#endif

    while (1) {
        esp_task_wdt_reset();
        if (is_connected) {
            status_led_state = true;
#ifdef STATUS_LED_ACTIVE_LOW
            gpio_set_level(STATUS_LED_GPIO, 0);   /* ON = LOW */
#else
            gpio_set_level(STATUS_LED_GPIO, 1);   /* ON = HIGH */
#endif
        } else {
            status_led_state = !status_led_state;
#ifdef STATUS_LED_ACTIVE_LOW
            gpio_set_level(STATUS_LED_GPIO, status_led_state ? 0 : 1);
#else
            gpio_set_level(STATUS_LED_GPIO, status_led_state ? 1 : 0);
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(STATUS_LED_BLINK_MS));
    }
}

/* ------------------------------------------------------------------------- */
/* Notification task and API                                                 */
/* ------------------------------------------------------------------------- */

static void connection_timeout_task(void *arg)
{
    connection_timeout_running = true;
    message_received_flag = false;
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (connection_timeout_running && !message_received_flag) {
        ESP_LOGW(TAG, "No message within timeout - disconnecting");
        if (is_connected) {
            esp_ble_gatts_close(gatt_if_for_send, conn_id);
        }
    }
    connection_timeout_task_handle = NULL;
    connection_timeout_running = false;
    vTaskDelete(NULL);
}

static esp_err_t start_connection_timeout(void)
{
    if (connection_timeout_task_handle != NULL) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(connection_timeout_task, "ble_conn_to", 4096, NULL, 4, &connection_timeout_task_handle) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t stop_connection_timeout(void)
{
    if (connection_timeout_task_handle == NULL) return ESP_OK;
    connection_timeout_running = false;
    for (uint32_t t = 0; t < 100 && connection_timeout_task_handle != NULL; t++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (connection_timeout_task_handle != NULL) {
        vTaskDelete(connection_timeout_task_handle);
        connection_timeout_task_handle = NULL;
    }
    return ESP_OK;
}

/* Run 5s after app message; if still connected, initiate disconnect (app usually closes in ~2.5s). */
static void disconnect_delay_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BLE_DISCONNECT_AFTER_MSG_MS));
    if (is_connected) {
        ESP_LOGI(TAG, "Disconnect timeout - closing connection");
        esp_ble_gatts_close(gatt_if_for_send, conn_id);
    }
    disconnect_delay_task_handle = NULL;
    vTaskDelete(NULL);
}

static void start_disconnect_delay(void)
{
    if (disconnect_delay_task_handle != NULL) return;
    if (xTaskCreate(disconnect_delay_task, "ble_disc_dly", 2048, NULL, 4, &disconnect_delay_task_handle) != pdPASS) {
        ESP_LOGW(TAG, "Disconnect delay task create failed");
    }
}

esp_err_t ble_send_notification(const char *message, bool is_response)
{
    (void)is_response;
    if (!message || strlen(message) == 0) return ESP_ERR_INVALID_ARG;
    if (!is_connected || !notify_enabled || notification_queue == NULL) return ESP_ERR_INVALID_STATE;
    notification_msg_t msg;
    strncpy(msg.message, message, MSG_BUF_LEN - 1);
    msg.message[MSG_BUF_LEN - 1] = '\0';
    msg.length = strlen(msg.message);
    msg.is_response = is_response;
    if (xQueueSend(notification_queue, &msg, 0) != pdTRUE) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static esp_err_t start_notification_service(void)
{
    if (notification_task_handle != NULL) return ESP_ERR_INVALID_STATE;
    if (notification_queue == NULL) {
        notification_queue = xQueueCreate(NOTIFICATION_QUEUE_SIZE, sizeof(notification_msg_t));
        if (notification_queue == NULL) return ESP_FAIL;
    }
    if (xTaskCreate(notification_task, "ble_notify", 4096, NULL, 5, &notification_task_handle) != pdPASS) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Notification service started");
    return ESP_OK;
}

static esp_err_t stop_notification_service(void)
{
    if (notification_task_handle == NULL) return ESP_OK;
    notification_task_running = false;
    notification_msg_t dummy = {0};
    xQueueSend(notification_queue, &dummy, 0);
    for (uint32_t t = 0; t < 200 && notification_task_handle != NULL; t++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (notification_task_handle != NULL) {
        vTaskDelete(notification_task_handle);
        notification_task_handle = NULL;
    }
    return ESP_OK;
}

static void notification_task(void *arg)
{
    notification_msg_t msg;
    notification_task_running = true;
    while (notification_task_running) {
        if (xQueueReceive(notification_queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (is_connected && notify_enabled) {
                esp_err_t r = esp_ble_gatts_send_indicate(gatt_if_for_send, conn_id, char2_handle, msg.length, (uint8_t *)msg.message, false);
                if (r != ESP_OK) ESP_LOGW(TAG, "Notify send failed: %s", esp_err_to_name(r));
            }
        }
    }
    notification_task_handle = NULL;
    vTaskDelete(NULL);
}

bool ble_is_connected(void)
{
    return is_connected;
}

/* ------------------------------------------------------------------------- */
/* GATT DB                                                                    */
/* ------------------------------------------------------------------------- */

static esp_gatts_attr_db_t gatt_db[GATTS_NUM_HANDLE] = {
    [0] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_PRI_SERVICE}, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(uint16_t), (uint8_t *)&(uint16_t){GATTS_SERVICE_UUID}}},
    [1] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_DECLARE}, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&(uint8_t){ESP_GATT_CHAR_PROP_BIT_WRITE}}},
    [2] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){GATTS_CHAR_UUID_RX}, ESP_GATT_PERM_WRITE, 64, 0, NULL}},
    [3] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_DECLARE}, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&(uint8_t){ESP_GATT_CHAR_PROP_BIT_NOTIFY}}},
    [4] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){GATTS_CHAR_UUID_TX}, ESP_GATT_PERM_READ, 64, 0, NULL}},
    [5] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_CLIENT_CONFIG}, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(uint16_t), (uint8_t *)&(uint16_t){0x0000}}},
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    (void)param;
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising started");
            ESP_LOGI(TAG, "\033[1;32mUnit Ready..\033[0m");
            break;
        default:
            break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            esp_ble_gap_set_device_name(s_unit_id);
            esp_ble_gap_config_adv_data(&adv_data);
            esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, GATTS_NUM_HANDLE, 0);
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "Create attr table failed");
                break;
            }
            service_handle = param->add_attr_tab.handles[0];
            char1_handle = param->add_attr_tab.handles[2];
            char2_handle = param->add_attr_tab.handles[4];
            esp_ble_gatts_start_service(service_handle);
            break;

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "BLE client connected");
            is_connected = true;
            notify_enabled = true;
            conn_id = param->connect.conn_id;
            gatt_if_for_send = gatts_if;
            start_notification_service();
            start_connection_timeout();
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "BLE client disconnected");
            is_connected = false;
            notify_enabled = false;
            stop_connection_timeout();
            stop_notification_service();
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == char1_handle) {
                int len = param->write.len > MAX_DATA_LEN ? MAX_DATA_LEN : (int)param->write.len;
                memcpy(received_data, param->write.value, len);
                memcpy(message_buffer + message_index, received_data, (size_t)len);
                message_index += len;
                for (int i = message_index - len; i < message_index; i++) {
                    if (message_buffer[i] == '#') {
                        message_buffer[i] = '\0';
                        message_received_flag = true;
                        stop_connection_timeout();
                        ESP_LOGI(TAG, "BLE message: %s", message_buffer);

                        char response[80];
                        if (strlen(message_buffer) <= 15) {
                            snprintf(response, sizeof(response), "ACK: %s", message_buffer);
                        } else {
                            snprintf(response, sizeof(response), "ACK: %d chars", (int)strlen(message_buffer));
                        }
                        ble_send_notification(response, true);

                        if (strstr(message_buffer, encrypted_data) != NULL) {
                            ESP_LOGI(TAG, "Authorized");
                            relay_command_t relay_cmd = {0};
                            if (relay_parse_command_from_json(message_buffer, &relay_cmd) == ESP_OK) {
                                if (relay_execute_command(&relay_cmd) == ESP_OK) {
                                    ESP_LOGI(TAG, "Relay executed immediately");
                                }
                            }
                            start_disconnect_delay();
                        } else {
                            ESP_LOGW(TAG, "Unauthorized");
                            ble_send_notification("AUTH_ERROR", true);
                        }
                        message_index = 0;
                        memset(message_buffer, 0, MSG_BUF_LEN);
                        break;
                    }
                }
            }
            if (param->write.handle == (char2_handle + 1) && param->write.len == 2) {
                uint16_t v = (uint16_t)param->write.value[1] << 8 | param->write.value[0];
                if (v == 0x0001) {
                    notify_enabled = true;
                } else if (v == 0x0000) {
                    notify_enabled = false;
                    stop_notification_service();
                }
            }
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Init                                                                       */
/* ------------------------------------------------------------------------- */

esp_err_t ble_gatt_server_init(void)
{
    nvs_config_get_string(NVS_KEY_UNIT_ID, s_unit_id, sizeof(s_unit_id), A7670E_UNIT_ID);
    ESP_LOGI(TAG, "BLE init (unit ID: %s)", s_unit_id);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_ble_gatt_set_local_mtu(50);
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));

    /* Encryption seed from unit ID numeric part (e.g. cr18061952 -> 18061952) */
    const char *p = s_unit_id;
    while (*p && !isdigit((unsigned char)*p)) p++;
    rnd = (unsigned long)atol(p);
    rnd_ptr = &rnd;
    GetEncryptedData(rnd_ptr, encrypted_data);
    ESP_LOGI(TAG, "Encryption key: %s", encrypted_data);

    /* Status LED: blink when not connected, ON when BLE connected. */
    if (xTaskCreate(status_led_task, "status_led", 2048, NULL, 3, &status_led_task_handle) != pdPASS) {
        ESP_LOGW(TAG, "Status LED task create failed");
    } else {
        ESP_LOGI(TAG, "Status LED GPIO %d: blink=not connected, ON=connected", STATUS_LED_GPIO);
    }

    ESP_LOGI(TAG, "BLE GATT server ready - advertising");
    return ESP_OK;
}
