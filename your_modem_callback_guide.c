/**
 * @file your_modem_callback_guide.c
 * @brief COMPLETE CALLBACK GUIDE - Based on YOUR actual modem code
 * 
 * This shows EXACTLY how the callback pattern works in YOUR project:
 * 1. Where typedefs are defined (Modem_Config_Handling.h)
 * 2. How registration works (your .c file)
 * 3. How to write YOUR callback functions
 * 4. Real integration with YOUR modem system
 */

#include "Modem_Config_Handling.h"
#include "esp_log.h"

static const char *TAG = "MY_APP";

// ============================================================================
// STEP 1: TYPEDEF DECLARATIONS (Already done in Modem_Config_Handling.h)
// ============================================================================

/*
 * These are ALREADY DEFINED in your Modem_Config_Handling.h at line 78-80:
 * 
 * typedef void (*modem_data_received_cb_t)(const char *data, size_t length);
 * typedef void (*modem_status_changed_cb_t)(task_status_t status);
 * typedef bool (*modem_task_processor_cb_t)(const char *task_data, char *result_buffer, size_t buffer_size);
 */

// ============================================================================
// STEP 2: YOUR APPLICATION CALLBACK IMPLEMENTATIONS
// ============================================================================

/**
 * @brief YOUR data received callback implementation
 * This function will be called by the modem system when TCP data arrives
 */
void my_app_data_received(const char *data, size_t length) {
    ESP_LOGI(TAG, "🎯 Received %zu bytes: %s", length, data);
    
    // YOUR application logic here:
    if (strstr(data, "SENSOR")) {
        ESP_LOGI(TAG, "🌡️  Processing sensor data...");
        // Parse sensor data, update UI, save to database, etc.
    }
    
    if (strstr(data, "COMMAND")) {
        ESP_LOGI(TAG, "⚙️  Processing remote command...");
        // Execute remote command, control GPIO, etc.
    }
    
    if (strstr(data, "ERROR")) {
        ESP_LOGE(TAG, "❌ Server reported error: %s", data);
        // Handle error, retry connection, notify user, etc.
    }
}

/**
 * @brief YOUR status change callback implementation
 * This function will be called when modem connection status changes
 */
void my_app_status_changed(task_status_t status) {
    switch (status) {
        case TASK_STATUS_IDLE:
            ESP_LOGI(TAG, "🔵 Modem is idle");
            // Maybe update LED indicator, UI status, etc.
            break;
            
        case TASK_STATUS_CONNECTING:
            ESP_LOGI(TAG, "🟡 Connecting to server...");
            // Show "connecting" animation, disable certain features
            break;
            
        case TASK_STATUS_CONNECTED:
            ESP_LOGI(TAG, "🟢 Connected successfully!");
            // Enable online features, start periodic tasks, update UI
            break;
            
        case TASK_STATUS_SENDING:
            ESP_LOGI(TAG, "📤 Sending data...");
            // Show upload indicator
            break;
            
        case TASK_STATUS_RECEIVING:
            ESP_LOGI(TAG, "📥 Receiving data...");
            // Show download indicator
            break;
            
        case TASK_STATUS_PROCESSING:
            ESP_LOGI(TAG, "⚙️  Processing tasks...");
            // Show processing indicator
            break;
            
        case TASK_STATUS_ERROR:
            ESP_LOGE(TAG, "🔴 Connection error!");
            // Show error message, try to reconnect, log error
            break;
            
        case TASK_STATUS_DISCONNECTED:
            ESP_LOGW(TAG, "⚪ Disconnected");
            // Update UI, disable online features
            break;
    }
}

/**
 * @brief YOUR task processor callback implementation
 * This function will be called when the server sends tasks to execute
 */
bool my_app_task_processor(const char *task_data, char *result_buffer, size_t buffer_size) {
    ESP_LOGI(TAG, "🎯 Processing server task: %s", task_data);
    
    // YOUR task processing logic:
    
    if (strcmp(task_data, "GET_TEMPERATURE") == 0) {
        // Read temperature sensor
        float temp = 25.3;  // Your sensor reading code here
        snprintf(result_buffer, buffer_size, "TEMP:%.1f", temp);
        return true;
    }
    
    if (strcmp(task_data, "GET_BATTERY") == 0) {
        // Read battery level
        int battery = 85;  // Your battery reading code here
        snprintf(result_buffer, buffer_size, "BATTERY:%d", battery);
        return true;
    }
    
    if (strcmp(task_data, "LED_ON") == 0) {
        // Control GPIO - turn LED on
        // gpio_set_level(LED_PIN, 1);
        snprintf(result_buffer, buffer_size, "LED:ON");
        return true;
    }
    
    if (strcmp(task_data, "LED_OFF") == 0) {
        // Control GPIO - turn LED off
        // gpio_set_level(LED_PIN, 0);
        snprintf(result_buffer, buffer_size, "LED:OFF");
        return true;
    }
    
    if (strncmp(task_data, "SET_CONFIG:", 11) == 0) {
        // Update configuration
        const char *config_value = task_data + 11;
        ESP_LOGI(TAG, "Updating config: %s", config_value);
        // Your configuration update code here
        snprintf(result_buffer, buffer_size, "CONFIG:UPDATED");
        return true;
    }
    
    // Unknown task
    ESP_LOGW(TAG, "Unknown task: %s", task_data);
    snprintf(result_buffer, buffer_size, "ERROR:UNKNOWN_TASK");
    return false;
}

// ============================================================================
// STEP 3: REGISTRATION - How to connect YOUR functions to the modem system
// ============================================================================

/**
 * @brief Initialize YOUR application with the modem system
 * Call this from your app_main() function
 */
void my_app_init(void) {
    ESP_LOGI(TAG, "Initializing application...");
    
    // STEP 3A: Register YOUR callback functions with the modem system
    // These functions are ALREADY AVAILABLE in Modem_Config_Handling.c:
    
    modem_register_data_callback(my_app_data_received);
    modem_register_status_callback(my_app_status_changed);
    modem_register_task_processor(my_app_task_processor);
    
    ESP_LOGI(TAG, "✓ All callbacks registered");
    
    // STEP 3B: Initialize and start the modem system
    modem_config_t config = modem_get_default_config();
    
    // Customize configuration for YOUR application:
    strncpy(config.server_address, "your-server.com", sizeof(config.server_address));
    config.server_port = 8080;
    strncpy(config.protocol, "TCP", sizeof(config.protocol));
    config.task_interval = 30000;  // Check for tasks every 30 seconds
    
    // Initialize and start
    if (modem_handler_init(&config)) {
        ESP_LOGI(TAG, "✓ Modem handler initialized");
        
        if (modem_handler_start()) {
            ESP_LOGI(TAG, "✓ Modem handler started");
            ESP_LOGI(TAG, "🚀 YOUR APP IS NOW RUNNING!");
            ESP_LOGI(TAG, "   - Will receive TCP data via my_app_data_received()");
            ESP_LOGI(TAG, "   - Will get status updates via my_app_status_changed()");
            ESP_LOGI(TAG, "   - Will process server tasks via my_app_task_processor()");
        } else {
            ESP_LOGE(TAG, "Failed to start modem handler");
        }
    } else {
        ESP_LOGE(TAG, "Failed to initialize modem handler");
    }
}

// ============================================================================
// STEP 4: YOUR MAIN APPLICATION LOOP
// ============================================================================

/**
 * @brief YOUR main application function
 * This is what you put in app_main()
 */
void app_main(void) {
    ESP_LOGI(TAG, "=== YOUR ESP32 APPLICATION STARTING ===");
    
    // Initialize NVS (required for ESP32)
    nvs_flash_init();
    
    // Initialize YOUR application with callbacks
    my_app_init();
    
    // YOUR main application loop
    // The modem system runs in background and calls YOUR functions!
    while (1) {
        // Do YOUR application work here:
        
        // Check button presses, update display, etc.
        // vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Send periodic data to server (optional)
        // if (modem_is_connected()) {
        //     modem_send_tcp_data("HEARTBEAT");
        // }
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Sleep 5 seconds
    }
}

// ============================================================================
// STEP 5: ADVANCED USAGE EXAMPLES
// ============================================================================

/**
 * @brief Send data to server from YOUR application
 */
void my_app_send_sensor_data(void) {
    if (modem_is_connected()) {
        char data[128];
        float temperature = 25.3;  // Read from your sensor
        int humidity = 65;         // Read from your sensor
        
        snprintf(data, sizeof(data), "SENSOR_DATA:TEMP=%.1f,HUMIDITY=%d", temperature, humidity);
        
        if (modem_send_tcp_data(data)) {
            ESP_LOGI(TAG, "✓ Sensor data sent");
        } else {
            ESP_LOGE(TAG, "✗ Failed to send sensor data");
        }
    }
}

/**
 * @brief Handle button press in YOUR application
 */
void my_app_button_pressed(int button_id) {
    ESP_LOGI(TAG, "Button %d pressed", button_id);
    
    if (modem_is_connected()) {
        char message[64];
        snprintf(message, sizeof(message), "BUTTON_PRESS:%d", button_id);
        modem_send_tcp_data(message);
    }
}

// ============================================================================
// COMPLETE FLOW SUMMARY
// ============================================================================

/*
 * COMPLETE CALLBACK FLOW IN YOUR PROJECT:
 * 
 * 1. TYPEDEF DECLARATIONS (Modem_Config_Handling.h lines 78-80):
 *    ✓ modem_data_received_cb_t
 *    ✓ modem_status_changed_cb_t  
 *    ✓ modem_task_processor_cb_t
 * 
 * 2. CALLBACK STORAGE (Modem_Config_Handling.c lines 35, etc.):
 *    ✓ static modem_data_received_cb_t data_callback = NULL;
 *    ✓ static modem_status_changed_cb_t status_callback = NULL;
 *    ✓ static modem_task_processor_cb_t task_processor = NULL;
 * 
 * 3. REGISTRATION FUNCTIONS (Modem_Config_Handling.c lines 339-351):
 *    ✓ modem_register_data_callback()
 *    ✓ modem_register_status_callback() 
 *    ✓ modem_register_task_processor()
 * 
 * 4. CALLBACK USAGE (Throughout Modem_Config_Handling.c):
 *    ✓ if (data_callback) data_callback(data, length);
 *    ✓ if (status_callback) status_callback(new_status);
 *    ✓ if (task_processor) task_processor(task_data, result, size);
 * 
 * 5. YOUR IMPLEMENTATION (This file):
 *    ✓ Write your callback functions
 *    ✓ Register them with the modem system
 *    ✓ Let the modem system call them automatically
 * 
 * RESULT: Event-driven programming - YOUR functions get called when things happen!
 */