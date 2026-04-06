/**
 * @file relay_control.c
 * @brief Non-latching relay control for ESP32 WROOM 32E. Shared by modem OPEN and BLE app.
 *
 * Source: EG_BLE_server (NON_LATCH). One GPIO per relay; HIGH = ON, LOW = OFF.
 * Board: ESP32 WROOM 32E — Relay 1 = GPIO 12, Relay 2 = GPIO 13.
 */

#define NON_LATCH
/* Optional: define RELAY_ACTIVE_LOW if your hardware uses LOW = ON */
/* #define RELAY_ACTIVE_LOW */

#include "relay_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "RELAY_CTRL";

/* Non-latching: one pin per relay. ESP32 WROOM 32E — GPIO 12, 13 (Eli board). */
typedef struct {
    gpio_num_t set_pin;
    gpio_num_t reset_pin;  /* unused in NON_LATCH; kept for struct compatibility */
} relay_pins_t;

/* // Eli board-GPIO 12, 13 are the pins for the relays on the board.
static const relay_pins_t relay_pins[MAX_RELAYS] = {
    {GPIO_NUM_12, GPIO_NUM_12},
    {GPIO_NUM_13, GPIO_NUM_13}
}; */

// Smallboard-GPIO 16, 17 are the pins for the relays on the board.
static const relay_pins_t relay_pins[MAX_RELAYS] = {
    {GPIO_NUM_16, GPIO_NUM_16},
    {GPIO_NUM_17, GPIO_NUM_17}
};



typedef struct {
    relay_command_t command;
    TickType_t start_time;
    bool is_timed;
} active_relay_t;

static QueueHandle_t relay_queue = NULL;
static TaskHandle_t relay_task_handle = NULL;
static bool relay_task_running = false;
static relay_status_t relay_status[MAX_RELAYS] = {0};
static active_relay_t active_relays[MAX_RELAYS] = {0};

static void relay_task(void *arg);
static esp_err_t configure_relay_gpio(void);
static void set_relay_state(uint8_t relay_number, bool state);
static void update_relay_timers(void);

esp_err_t relay_control_init(void)
{
    ESP_LOGI(TAG, "Initializing relay control (non-latching, ESP32 WROOM 32E)");

    esp_err_t ret = configure_relay_gpio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure relay GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    relay_queue = xQueueCreate(RELAY_QUEUE_SIZE, sizeof(relay_command_t));
    if (relay_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create relay queue");
        return ESP_FAIL;
    }

    /* Non-latching: set all outputs to OFF (LOW) at startup */
    for (int i = 0; i < MAX_RELAYS; i++) {
#ifdef RELAY_ACTIVE_LOW
        gpio_set_level(relay_pins[i].set_pin, 1);
#else
        gpio_set_level(relay_pins[i].set_pin, 0);
#endif
        relay_status[i].is_active = false;
        relay_status[i].remaining_ms = 0;
        relay_status[i].total_activations = 0;
        ESP_LOGI(TAG, "Relay %d OFF (GPIO %d)", i + 1, relay_pins[i].set_pin);
    }

    ESP_LOGI(TAG, "Relay control initialized (non-latching)");
    return ESP_OK;
}

esp_err_t relay_control_start(void)
{
    if (relay_task_handle != NULL) {
        ESP_LOGW(TAG, "Relay task already running");
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(relay_task, "relay_task", RELAY_TASK_STACK_SIZE, NULL, 5, &relay_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create relay task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Relay control task started");
    return ESP_OK;
}

esp_err_t relay_control_stop(void)
{
    if (relay_task_handle == NULL) return ESP_OK;
    relay_task_running = false;
    relay_command_t dummy_cmd = {0};
    xQueueSend(relay_queue, &dummy_cmd, 0);
    for (uint32_t t = 0; t < 200 && relay_task_handle != NULL; t++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (relay_task_handle != NULL) {
        vTaskDelete(relay_task_handle);
        relay_task_handle = NULL;
    }
    ESP_LOGI(TAG, "Relay control task stopped");
    return ESP_OK;
}

esp_err_t relay_execute_command(const relay_command_t *cmd)
{
    if (!cmd || relay_queue == NULL) return ESP_ERR_INVALID_ARG;
    if (cmd->relay_number < 1 || cmd->relay_number > 3) {
        ESP_LOGE(TAG, "Invalid relay number: %d", cmd->relay_number);
        return ESP_ERR_INVALID_ARG;
    }
    if (xQueueSend(relay_queue, cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Relay queue full, command dropped");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Queued relay: Relay %d, %ums, %s", cmd->relay_number, (unsigned)cmd->duration_ms, cmd->activate ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t relay_get_status(uint8_t relay_number, relay_status_t *status)
{
    if (!status || relay_number < 1 || relay_number > MAX_RELAYS) return ESP_ERR_INVALID_ARG;
    *status = relay_status[relay_number - 1];
    return ESP_OK;
}

esp_err_t relay_emergency_stop(void)
{
    ESP_LOGW(TAG, "EMERGENCY STOP - All relays OFF");
    for (int i = 0; i < MAX_RELAYS; i++) {
        set_relay_state(i + 1, false);
        relay_status[i].is_active = false;
        relay_status[i].remaining_ms = 0;
        active_relays[i].is_timed = false;
    }
    return ESP_OK;
}

esp_err_t relay_parse_command_from_json(const char *json_message, relay_command_t *cmd)
{
    if (!json_message || !cmd) return ESP_ERR_INVALID_ARG;
    cJSON *json = cJSON_Parse(json_message);
    if (!json) {
        ESP_LOGE(TAG, "Invalid JSON");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *g_item = cJSON_GetObjectItem(json, "g");
    if (!g_item || !cJSON_IsArray(g_item)) {
        cJSON_Delete(json);
        return ESP_ERR_NOT_FOUND;
    }
    cJSON *relay_num = cJSON_GetArrayItem(g_item, 0);
    cJSON *duration = cJSON_GetArrayItem(g_item, 1);
    if (!relay_num || !cJSON_IsNumber(relay_num) || !duration || !cJSON_IsNumber(duration)) {
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    int r = relay_num->valueint;
    int d_sec = duration->valueint;
    if (r < 1 || r > 3 || d_sec < 1 || d_sec > 15) {
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    cmd->relay_number = (uint8_t)r;
    cmd->duration_ms = (uint32_t)(d_sec * 1000);
    cmd->activate = true;
    snprintf(cmd->description, sizeof(cmd->description), "JSON command");
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t configure_relay_gpio(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 0,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    for (int i = 0; i < MAX_RELAYS; i++) {
        io_conf.pin_bit_mask |= (1ULL << relay_pins[i].set_pin);
    }
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        for (int i = 0; i < MAX_RELAYS; i++) {
#ifdef RELAY_ACTIVE_LOW
            gpio_set_level(relay_pins[i].set_pin, 1);
#else
            gpio_set_level(relay_pins[i].set_pin, 0);
#endif
        }
        ESP_LOGI(TAG, "Configured %d non-latching relays (GPIO %d, %d)", MAX_RELAYS, relay_pins[0].set_pin, relay_pins[1].set_pin);
    }
    return ret;
}

static void set_relay_state(uint8_t relay_number, bool state)
{
    if (relay_number < 1 || relay_number > MAX_RELAYS) return;
    const relay_pins_t *p = &relay_pins[relay_number - 1];
#ifdef RELAY_ACTIVE_LOW
    gpio_set_level(p->set_pin, state ? 0 : 1);
#else
    gpio_set_level(p->set_pin, state ? 1 : 0);
#endif
    ESP_LOGI(TAG, "Relay %d %s (GPIO %d)", relay_number, state ? "ON" : "OFF", p->set_pin);
}

static void update_relay_timers(void)
{
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < MAX_RELAYS; i++) {
        if (!active_relays[i].is_timed || !relay_status[i].is_active) continue;
        uint32_t elapsed_ms = pdTICKS_TO_MS(now - active_relays[i].start_time);
        if (elapsed_ms >= active_relays[i].command.duration_ms) {
            set_relay_state(i + 1, false);
            relay_status[i].is_active = false;
            relay_status[i].remaining_ms = 0;
            active_relays[i].is_timed = false;
          //  ESP_LOGI(TAG, "Relay %d auto-off after %ums", i + 1, (unsigned)elapsed_ms);
        } else {
            relay_status[i].remaining_ms = active_relays[i].command.duration_ms - elapsed_ms;
        }
    }
}

static void relay_task(void *arg)
{
    relay_command_t cmd;
    relay_task_running = true;
    ESP_LOGI(TAG, "Relay task started");

    while (relay_task_running) {
        if (xQueueReceive(relay_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!relay_task_running) break;
            ESP_LOGI(TAG, "Processing Command: %s", cmd.description);

            if (cmd.activate) {
                if (cmd.relay_number == 3) {
                    for (int r = 1; r <= MAX_RELAYS; r++) {
                        int idx = r - 1;
                        set_relay_state(r, true);
                        relay_status[idx].is_active = true;
                        relay_status[idx].total_activations++;
                        if (cmd.duration_ms > 0) {
                            active_relays[idx].command = cmd;
                            active_relays[idx].start_time = xTaskGetTickCount();
                            active_relays[idx].is_timed = true;
                            relay_status[idx].remaining_ms = cmd.duration_ms;
                        } else {
                            active_relays[idx].is_timed = false;
                            relay_status[idx].remaining_ms = 0;
                        }
                    }
                    ESP_LOGI(TAG, "Both relays ON for %ums", (unsigned)cmd.duration_ms);
                } else if (cmd.relay_number >= 1 && cmd.relay_number <= MAX_RELAYS) {
                    int idx = cmd.relay_number - 1;
                    set_relay_state(cmd.relay_number, true);
                    relay_status[idx].is_active = true;
                    relay_status[idx].total_activations++;
                    if (cmd.duration_ms > 0) {
                        active_relays[idx].command = cmd;
                        active_relays[idx].start_time = xTaskGetTickCount();
                        active_relays[idx].is_timed = true;
                        relay_status[idx].remaining_ms = cmd.duration_ms;
                        ESP_LOGI(TAG, "Relay %d ON %ums", cmd.relay_number, (unsigned)cmd.duration_ms);
                    } else {
                        active_relays[idx].is_timed = false;
                        relay_status[idx].remaining_ms = 0;
                        ESP_LOGI(TAG, "Relay %d ON permanent", cmd.relay_number);
                    }
                }
            } else {
                if (cmd.relay_number == 3) {
                    for (int r = 1; r <= MAX_RELAYS; r++) {
                        set_relay_state(r, false);
                        relay_status[r - 1].is_active = false;
                        relay_status[r - 1].remaining_ms = 0;
                        active_relays[r - 1].is_timed = false;
                    }
                    ESP_LOGI(TAG, "Both relays OFF");
                } else if (cmd.relay_number >= 1 && cmd.relay_number <= MAX_RELAYS) {
                    int idx = cmd.relay_number - 1;
                    set_relay_state(cmd.relay_number, false);
                    relay_status[idx].is_active = false;
                    relay_status[idx].remaining_ms = 0;
                    active_relays[idx].is_timed = false;
                    ESP_LOGI(TAG, "Relay %d OFF", cmd.relay_number);
                }
            }
        }
        update_relay_timers();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    relay_task_handle = NULL;
    ESP_LOGI(TAG, "Relay task stopped");
    vTaskDelete(NULL);
}
