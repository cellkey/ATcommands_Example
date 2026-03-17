/**
 * @file digital_input_alert.c
 * @brief GPIO22 active-low monitor; send ALERT01 after 180s active.
 */

#include "digital_input_alert.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "at_command_api.h"
#include "server_keepalive.h"

static const char *TAG = "DIN_ALERT";

static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;
static volatile bool s_active = false;

/* "7\r\nALERT01\r\n\0" (include trailing NUL as requested). */
static const uint8_t ALERT01_MSG[] = {
    0x37, 0x0D, 0x0A,              /* "7\r\n" */
    0x41, 0x4C, 0x45, 0x52, 0x54, 0x30, 0x31, /* "ALERT01" */
    0x0D, 0x0A                     /* "\r\n" */
};

static void digital_input_gpio_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << DIGITAL_INPUT_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    (void)gpio_config(&io_conf);
}

bool digital_input_is_active(void) {
    return s_active;
}

static void digital_input_alert_task(void *arg) {
    (void)arg;

    digital_input_gpio_init();

    bool last_active = false;
    bool alert_sent_this_activation = false;
    TickType_t active_since = 0;

    while (s_running) {
        int level = gpio_get_level(DIGITAL_INPUT_GPIO);
        bool active = (level == 0); /* pull-up, active when pulled LOW */
        s_active = active;

        if (active && !last_active) {
            active_since = xTaskGetTickCount();
            alert_sent_this_activation = false;
            ESP_LOGI(TAG, "Digital input ACTIVE (GPIO%d LOW)", DIGITAL_INPUT_GPIO);
        } else if (!active && last_active) {
            ESP_LOGI(TAG, "Digital input deactivated (GPIO%d HIGH)", DIGITAL_INPUT_GPIO);
        }

        if (active && !alert_sent_this_activation) {
            TickType_t now = xTaskGetTickCount();
            TickType_t elapsed_ticks = now - active_since;
            if (elapsed_ticks >= pdMS_TO_TICKS((uint32_t)DIGITAL_INPUT_ALERT_DELAY_SEC * 1000U)) {
                at_result_t r = send_at_then_raw_data(SERVER_TCP_LINK_ID, sizeof(ALERT01_MSG), ALERT01_MSG);
                if (r == AT_RESULT_SUCCESS) {
                    ESP_LOGI(TAG, "ALERT01 sent to server (active > %ds)", DIGITAL_INPUT_ALERT_DELAY_SEC);
                    alert_sent_this_activation = true;
                } else {
                    ESP_LOGW(TAG, "ALERT01 send failed (r=%d); will retry while input stays active", (int)r);
                    /* retry later while still active */
                }
            }
        }

        last_active = active;
        vTaskDelay(pdMS_TO_TICKS(200)); /* debounce/poll interval */
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

bool digital_input_alert_task_start(void) {
    if (s_task != NULL) return true;
    s_running = true;
    BaseType_t ok = xTaskCreate(digital_input_alert_task, "din_alert", 2048, NULL, 5, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        s_running = false;
        return false;
    }
    return true;
}

void digital_input_alert_task_stop(void) {
    s_running = false;
}

