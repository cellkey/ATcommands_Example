/**
 * @file Modem_Config_Handling.c
 * @brief Consolidated modem configuration and communication handler implementation
 * #include "Modem_Config_Handling.h"
 * Production-ready modem management system
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "Modem_Config_Handling.h"
#include "modem_definitions.h"

static const char *TAG = "MODEM_HANDLER";

// Internal state management
static modem_config_t current_config;
static task_status_t current_status = TASK_STATUS_IDLE;
static TaskHandle_t modem_task_handle = NULL;
static bool handler_initialized = false;
static bool handler_running = false;

// Synchronization
static SemaphoreHandle_t config_mutex = NULL;
static SemaphoreHandle_t status_mutex = NULL;

// Callbacks
static modem_data_received_cb_t data_callback = NULL;
static modem_status_changed_cb_t status_callback = NULL;
static modem_task_processor_cb_t task_processor = NULL;

// Internal function prototypes
static void modem_main_task(void *arg);
static bool setup_modem_connection(void);
static bool execute_communication_cycle(void);
static void update_status(task_status_t new_status);
static bool send_data_internal(const char *data, communication_mode_t mode);

/**
 * @brief Initialize the modem handler with configuration
 */
bool modem_handler_init(const modem_config_t *config) {
    if (handler_initialized) {
        ESP_LOGW(TAG, "Modem handler already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing modem handler...");
    
    // Create synchronization objects
    config_mutex = xSemaphoreCreateMutex();
    status_mutex = xSemaphoreCreateMutex();
    
    if (!config_mutex || !status_mutex) {
        ESP_LOGE(TAG, "Failed to create synchronization objects");
        return false;
    }
    
    // Copy configuration
    if (config) {
        memcpy(&current_config, config, sizeof(modem_config_t));
    } else {
        current_config = modem_get_default_config();
    }
    
    // Initialize modem detection if enabled
    if (current_config.auto_detect_modem) {
        ESP_LOGI(TAG, "Auto-detecting modem type...");
        modem_type_t detected = detect_modem_type();
        if (detected != MODEM_TYPE_MAX) {
            current_config.modem_type = detected;
            set_modem_type(detected);
        }
    } else {
        set_modem_type(current_config.modem_type);
    }
    
    ESP_LOGI(TAG, "✓ Modem handler initialized with %s", get_modem_type_name(current_config.modem_type));
    handler_initialized = true;
    
    return true;
}

/**
 * @brief Start the modem handler task
 */
bool modem_handler_start(void) {
    if (!handler_initialized) {
        ESP_LOGE(TAG, "Modem handler not initialized");
        return false;
    }
    
    if (handler_running) {
        ESP_LOGW(TAG, "Modem handler already running");
        return true;
    }
    
    ESP_LOGI(TAG, "Starting modem handler task...");
    
    BaseType_t result = xTaskCreate(
        modem_main_task,
        "modem_handler",
        6144,  // Stack size
        NULL,
        7,     // Priority
        &modem_task_handle
    );
    
    if (result == pdPASS) {
        handler_running = true;
        ESP_LOGI(TAG, "✓ Modem handler task started");
        return true;
    } else {
        ESP_LOGE(TAG, "✗ Failed to create modem handler task");
        return false;
    }
}

/**
 * @brief Stop the modem handler task
 */
bool modem_handler_stop(void) {
    if (!handler_running) {
        ESP_LOGW(TAG, "Modem handler not running");
        return true;
    }
    
    ESP_LOGI(TAG, "Stopping modem handler...");
    
    handler_running = false;
    
    if (modem_task_handle) {
        vTaskDelete(modem_task_handle);
        modem_task_handle = NULL;
    }
    
    update_status(TASK_STATUS_IDLE);
    ESP_LOGI(TAG, "✓ Modem handler stopped");
    
    return true;
}

/**
 * @brief Main modem handler task
 */
static void modem_main_task(void *arg) {
    ESP_LOGI(TAG, "Modem handler task running");
    
    update_status(TASK_STATUS_CONNECTING);
    
    // Setup connection
    if (!setup_modem_connection()) {
        ESP_LOGE(TAG, "Failed to setup modem connection");
        update_status(TASK_STATUS_ERROR);
        handler_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    update_status(TASK_STATUS_CONNECTED);
    
    // Main communication loop
    int cycle_count = 0;
    while (handler_running) {
        // Check cycle limit
        if (current_config.max_task_cycles > 0 && cycle_count >= current_config.max_task_cycles) {
            ESP_LOGI(TAG, "Maximum task cycles reached (%d)", current_config.max_task_cycles);
            break;
        }
        
        // Execute communication cycle
        if (current_config.enable_periodic_tasks) {
            if (execute_communication_cycle()) {
                cycle_count++;
            }
        }
        
        // Wait for next cycle
        vTaskDelay(pdMS_TO_TICKS(current_config.task_interval));
    }
    
    // Cleanup
    ESP_LOGI(TAG, "Modem handler task completed (%d cycles)", cycle_count);
    modem_disconnect_from_server();
    update_status(TASK_STATUS_DISCONNECTED);
    handler_running = false;
    vTaskDelete(NULL);
}

/**
 * @brief Setup modem connection based on configuration
 */
static bool setup_modem_connection(void) {
    ESP_LOGI(TAG, "Setting up modem connection...");
    
    const modem_at_commands_t *cmd = get_modem_commands();
    
    // Basic modem initialization
    at_result_t result = send_at_command("AT", "OK", 2000);
    if (result != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "Modem not responding");
        return false;
    }
    
    // Configure PDP context
    char pdp_cmd[128];
    snprintf(pdp_cmd, sizeof(pdp_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", current_config.apn);
    result = send_at_command(pdp_cmd, "OK", 5000);
    if (result != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "Failed to configure PDP context");
        return false;
    }
    
    // Activate PDP context
    result = send_at_command(cmd->pdp_activate_cmd, "OK", 30000);
    if (result != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "Failed to activate PDP context");
        return false;
    }
    
    // Protocol-specific setup
    if (strcmp(current_config.protocol, "HTTP") == 0) {
        // HTTP setup
        if (get_modem_capabilities()->supports_http) {
            result = send_at_command(cmd->http_init_cmd, "OK", 5000);
            if (result != AT_RESULT_SUCCESS) {
                ESP_LOGW(TAG, "HTTP initialization failed, falling back to TCP");
                strncpy(current_config.protocol, "TCP", sizeof(current_config.protocol));
            }
        }
    }
    
    ESP_LOGI(TAG, "✓ Modem connection setup completed for %s", current_config.protocol);
    return true;
}

/**
 * @brief Execute one communication cycle
 */
static bool execute_communication_cycle(void) {
    update_status(TASK_STATUS_SENDING);
    
    // Send task request
    char request[128];
    snprintf(request, sizeof(request), "GET_TASKS:%s", get_modem_type_name(current_config.modem_type));
    
    bool success = false;
    if (strcmp(current_config.protocol, "HTTP") == 0) {
        success = modem_send_http_request("/api/tasks", request);
    } else {
        success = modem_send_tcp_data(request);
    }
    
    if (!success) {
        update_status(TASK_STATUS_ERROR);
        return false;
    }
    
    update_status(TASK_STATUS_RECEIVING);
    
    // Wait for response (simplified for now)
    vTaskDelay(pdMS_TO_TICKS(current_config.response_timeout));
    
    update_status(TASK_STATUS_PROCESSING);
    
    // Process task if callback is registered
    if (task_processor) {
        char response[256];
        if (task_processor("simulated_task_data", response, sizeof(response))) {
            // Send response back
            update_status(TASK_STATUS_SENDING);
            if (strcmp(current_config.protocol, "HTTP") == 0) {
                modem_send_http_request("/api/response", response);
            } else {
                modem_send_tcp_data(response);
            }
        }
    }
    
    update_status(TASK_STATUS_CONNECTED);
    return true;
}

/**
 * @brief Send TCP data
 */
bool modem_send_tcp_data(const char *data) {
    return send_data_internal(data, COMM_MODE_TCP_SOCKET);
}

/**
 * @brief Send HTTP request
 */
bool modem_send_http_request(const char *endpoint, const char *data) {
    // Simplified HTTP implementation
    char full_request[512];
    snprintf(full_request, sizeof(full_request), "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s",
             endpoint, current_config.server_address, (int)strlen(data), data);
    
    return send_data_internal(full_request, COMM_MODE_HTTP_REST);
}

/**
 * @brief Internal data sending function
 */
static bool send_data_internal(const char *data, communication_mode_t mode) {
    const modem_at_commands_t *cmd = get_modem_commands();
    
    // Build send command
    char send_cmd[64];
    get_tcp_send_command(send_cmd, sizeof(send_cmd), strlen(data));
    
    // Send command
    at_result_t result = send_at_command_ex(send_cmd, cmd->tcp_send_prompt, 5000, false);
    if (result != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "Failed to enter send mode");
        return false;
    }
    
    // Send actual data (simplified)
    result = send_at_command_ex(data, cmd->tcp_send_ok, 10000, false);
    return (result == AT_RESULT_SUCCESS);
}

/**
 * @brief Update task status with callback notification
 */
static void update_status(task_status_t new_status) {
    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (current_status != new_status) {
            current_status = new_status;
            if (status_callback) {
                status_callback(new_status);
            }
        }
        xSemaphoreGive(status_mutex);
    }
}

/**
 * @brief Get current task status
 */
task_status_t modem_get_status(void) {
    task_status_t status = TASK_STATUS_IDLE;
    if (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        status = current_status;
        xSemaphoreGive(status_mutex);
    }
    return status;
}

/**
 * @brief Register data received callback
 */
void modem_register_data_callback(modem_data_received_cb_t callback) {
    data_callback = callback;
}

/**
 * @brief Register status changed callback
 */
void modem_register_status_callback(modem_status_changed_cb_t callback) {
    status_callback = callback;
}

/**
 * @brief Register task processor callback
 */
void modem_register_task_processor(modem_task_processor_cb_t callback) {
    task_processor = callback;
}

// ============================================================================
// CALLBACK TRIGGER FUNCTIONS - For testing and manual activation
// ============================================================================

/**
 * @brief Manually trigger data callback (for testing)
 */
void modem_trigger_data_callback(const char *data, size_t length) {
    if (data_callback) {
        data_callback(data, length);
    }
}

/**
 * @brief Manually trigger status callback (for testing)  
 */
void modem_trigger_status_callback(task_status_t status) {
    if (status_callback) {
        status_callback(status);
    }
}

/**
 * @brief Manually trigger task processor callback (for testing)
 */
bool modem_trigger_task_callback(const char *task_data, char *response, size_t response_size) {
    if (task_processor) {
        return task_processor(task_data, response, response_size);
    }
    return false;
}

/**
 * @brief Get default configuration
 */
modem_config_t modem_get_default_config(void)
 {
    modem_config_t config = {
        .modem_type = CURRENT_MODEM_TYPE,
        .auto_detect_modem = true,
        .apn = "internet",
        .username = "",
        .password = "",
        .server_address = "your-server.com",
        .server_port = 8080,
        .protocol = "TCP",
        .connection_timeout = 30000,
        .response_timeout = 10000,
        .retry_attempts = 3,
        .retry_delay = 5000,
        .enable_periodic_tasks = true,
        .task_interval = 10000,
        .max_task_cycles = 0,  // Unlimited
        .enable_power_saving = false,
        .idle_timeout = 60000
    };
    return config;
}

/**
 * @brief Get HTTP-specific configuration
 */
modem_config_t modem_get_http_config(const char *server, int port) {
    modem_config_t config = modem_get_default_config();
    strncpy(config.server_address, server, sizeof(config.server_address) - 1);
    config.server_port = port;
    strncpy(config.protocol, "HTTP", sizeof(config.protocol) - 1);
    return config;
}

/**
 * @brief Get TCP-specific configuration
 */
modem_config_t modem_get_tcp_config(const char *server, int port) {
    modem_config_t config = modem_get_default_config();
    strncpy(config.server_address, server, sizeof(config.server_address) - 1);
    config.server_port = port;
    strncpy(config.protocol, "TCP", sizeof(config.protocol) - 1);
    return config;
}

/**
 * @brief Test connection
 */
bool modem_test_connection(void) {
    at_result_t result = send_at_command("AT", "OK", 2000);
    return (result == AT_RESULT_SUCCESS);
}

/**
 * @brief Check if connected
 */
bool modem_is_connected(void) {
    return (current_status == TASK_STATUS_CONNECTED);
}

/**
 * @brief Cleanup handler
 */
void modem_handler_cleanup(void) {
    modem_handler_stop();
    
    if (config_mutex) {
        vSemaphoreDelete(config_mutex);
        config_mutex = NULL;
    }
    
    if (status_mutex) {
        vSemaphoreDelete(status_mutex);
        status_mutex = NULL;
    }
    
    handler_initialized = false;
    ESP_LOGI(TAG, "Modem handler cleanup completed");
}