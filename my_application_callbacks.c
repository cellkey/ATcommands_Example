/**
 * @file my_application_callbacks.c
 * @brief PRACTICAL IMPLEMENTATION - Your actual callback functions for the modem system
 * 
 * This file contains YOUR application-specific callback implementations
 * that work directly with your existing Modem_Config_Handling system.
 * 
 * Drop this into your project and register these callbacks!
 */

#include "Modem_Config_Handling.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "MY_APP";

// ============================================================================
// YOUR APPLICATION STATE (Customize this for your needs)
// ============================================================================

typedef struct {
    // Sensor data
    float temperature;
    float humidity;
    int battery_level;
    
    // System state
    bool led_state;
    int button_presses;
    uint32_t uptime_seconds;
    
    // Communication stats
    uint32_t messages_received;
    uint32_t commands_processed;
    uint32_t errors_encountered;
    
} app_state_t;

static app_state_t g_app_state = {
    .temperature = 25.0,
    .humidity = 60.0,
    .battery_level = 85,
    .led_state = false,
    .button_presses = 0,
    .uptime_seconds = 0,
    .messages_received = 0,
    .commands_processed = 0,
    .errors_encountered = 0
};

// ============================================================================
// CALLBACK IMPLEMENTATION 1: DATA RECEIVED HANDLER
// ============================================================================

/**
 * @brief YOUR data received callback - called when TCP/HTTP data arrives
 * 
 * This function is called by the modem system whenever data is received
 * from the server. You can parse commands, update state, etc.
 */
void my_data_received_callback(const char *data, size_t length) {
    g_app_state.messages_received++;
    
    ESP_LOGI(TAG, "📥 [DATA] Received %zu bytes: '%.*s'", length, (int)length, data);
    
    // Parse different types of incoming data
    if (strncmp(data, "CMD:", 4) == 0) {
        // Server command received
        const char *command = data + 4;
        ESP_LOGI(TAG, "⚙️  [DATA] Server command: %s", command);
        
        if (strcmp(command, "LED_ON") == 0) {
            g_app_state.led_state = true;
            ESP_LOGI(TAG, "💡 [DATA] LED turned ON");
            // gpio_set_level(LED_PIN, 1);  // Your GPIO code here
            
        } else if (strcmp(command, "LED_OFF") == 0) {
            g_app_state.led_state = false;
            ESP_LOGI(TAG, "💡 [DATA] LED turned OFF");
            // gpio_set_level(LED_PIN, 0);  // Your GPIO code here
            
        } else if (strcmp(command, "GET_STATUS") == 0) {
            ESP_LOGI(TAG, "📊 [DATA] Status request received");
            // Send status back (you could trigger this immediately)
            
        } else if (strcmp(command, "REBOOT") == 0) {
            ESP_LOGW(TAG, "🔄 [DATA] Reboot command received");
            // esp_restart();  // Uncomment when ready
            
        } else {
            ESP_LOGW(TAG, "❓ [DATA] Unknown command: %s", command);
            g_app_state.errors_encountered++;
        }
        
    } else if (strncmp(data, "SENSOR:", 7) == 0) {
        // Sensor configuration received
        ESP_LOGI(TAG, "🌡️  [DATA] Sensor config: %s", data + 7);
        
    } else if (strncmp(data, "CONFIG:", 7) == 0) {
        // Configuration update received
        ESP_LOGI(TAG, "⚙️  [DATA] Config update: %s", data + 7);
        
    } else if (strstr(data, "ERROR") != NULL) {
        // Server error message
        ESP_LOGE(TAG, "❌ [DATA] Server error: %s", data);
        g_app_state.errors_encountered++;
        
    } else {
        // General data/message
        ESP_LOGI(TAG, "📝 [DATA] Message: %s", data);
    }
}

// ============================================================================
// CALLBACK IMPLEMENTATION 2: STATUS CHANGE HANDLER
// ============================================================================

/**
 * @brief YOUR status change callback - called when modem connection status changes
 * 
 * Use this to update UI, LEDs, or take actions based on connection state
 */
void my_status_changed_callback(task_status_t status) {
    ESP_LOGI(TAG, "📡 [STATUS] Modem status changed to: %d", status);
    
    switch (status) {
        case TASK_STATUS_IDLE:
            ESP_LOGI(TAG, "🔵 [STATUS] Modem is idle");
            // Update status LED to blue, disable online features
            break;
            
        case TASK_STATUS_CONNECTING:
            ESP_LOGI(TAG, "🟡 [STATUS] Connecting to server...");
            // Show connecting animation, update UI
            break;
            
        case TASK_STATUS_CONNECTED:
            ESP_LOGI(TAG, "🟢 [STATUS] Connected successfully!");
            // Enable online features, show connected status
            // Maybe send a "hello" message to server
            if (modem_is_connected()) {
                char hello_msg[64];
                snprintf(hello_msg, sizeof(hello_msg), "DEVICE_ONLINE:uptime=%lu", g_app_state.uptime_seconds);
                modem_send_tcp_data(hello_msg);
            }
            break;
            
        case TASK_STATUS_SENDING:
            ESP_LOGI(TAG, "📤 [STATUS] Sending data...");
            // Show upload indicator
            break;
            
        case TASK_STATUS_RECEIVING:
            ESP_LOGI(TAG, "📥 [STATUS] Receiving data...");
            // Show download indicator
            break;
            
        case TASK_STATUS_PROCESSING:
            ESP_LOGI(TAG, "⚙️  [STATUS] Processing tasks...");
            // Show processing indicator
            break;
            
        case TASK_STATUS_ERROR:
            ESP_LOGE(TAG, "🔴 [STATUS] Connection error!");
            // Show error status, maybe try to reconnect
            g_app_state.errors_encountered++;
            break;
            
        case TASK_STATUS_DISCONNECTED:
            ESP_LOGW(TAG, "⚪ [STATUS] Disconnected from server");
            // Update UI, disable online features
            break;
    }
}

// ============================================================================
// CALLBACK IMPLEMENTATION 3: TASK PROCESSOR
// ============================================================================

/**
 * @brief YOUR task processor callback - called when server sends tasks to execute
 * 
 * This is where you handle server requests and generate responses
 */
bool my_task_processor_callback(const char *task_data, char *response, size_t response_size) {
    g_app_state.commands_processed++;
    
    ESP_LOGI(TAG, "⚙️  [TASK] Processing: '%s'", task_data);
    
    // Handle different task types
    if (strcmp(task_data, "GET_TEMPERATURE") == 0) {
        // Read temperature sensor (simulate for now)
        g_app_state.temperature = 25.0 + (esp_random() % 100) / 10.0;  // Random for demo
        snprintf(response, response_size, "TEMP:%.1f", g_app_state.temperature);
        ESP_LOGI(TAG, "🌡️  [TASK] Temperature response: %s", response);
        return true;
        
    } else if (strcmp(task_data, "GET_HUMIDITY") == 0) {
        // Read humidity sensor
        g_app_state.humidity = 50.0 + (esp_random() % 300) / 10.0;  // Random for demo
        snprintf(response, response_size, "HUMIDITY:%.1f", g_app_state.humidity);
        ESP_LOGI(TAG, "💧 [TASK] Humidity response: %s", response);
        return true;
        
    } else if (strcmp(task_data, "GET_BATTERY") == 0) {
        // Read battery level
        g_app_state.battery_level = 80 + (esp_random() % 20);  // Random for demo
        snprintf(response, response_size, "BATTERY:%d", g_app_state.battery_level);
        ESP_LOGI(TAG, "🔋 [TASK] Battery response: %s", response);
        return true;
        
    } else if (strcmp(task_data, "GET_STATUS") == 0) {
        // Send comprehensive status
        snprintf(response, response_size, 
                "STATUS:TEMP=%.1f,HUM=%.1f,BAT=%d,LED=%s,UPTIME=%lu",
                g_app_state.temperature,
                g_app_state.humidity,
                g_app_state.battery_level,
                g_app_state.led_state ? "ON" : "OFF",
                g_app_state.uptime_seconds);
        ESP_LOGI(TAG, "📊 [TASK] Status response: %s", response);
        return true;
        
    } else if (strncmp(task_data, "SET_LED:", 8) == 0) {
        // Control LED
        const char *led_cmd = task_data + 8;
        if (strcmp(led_cmd, "ON") == 0) {
            g_app_state.led_state = true;
            // gpio_set_level(LED_PIN, 1);  // Your GPIO code
            snprintf(response, response_size, "LED:ON");
        } else if (strcmp(led_cmd, "OFF") == 0) {
            g_app_state.led_state = false;
            // gpio_set_level(LED_PIN, 0);  // Your GPIO code
            snprintf(response, response_size, "LED:OFF");
        } else {
            snprintf(response, response_size, "ERROR:INVALID_LED_CMD");
            return false;
        }
        ESP_LOGI(TAG, "💡 [TASK] LED control: %s", response);
        return true;
        
    } else if (strcmp(task_data, "GET_STATS") == 0) {
        // Send communication statistics
        snprintf(response, response_size, 
                "STATS:MSG=%lu,CMD=%lu,ERR=%lu",
                g_app_state.messages_received,
                g_app_state.commands_processed,
                g_app_state.errors_encountered);
        ESP_LOGI(TAG, "📈 [TASK] Stats response: %s", response);
        return true;
        
    } else if (strncmp(task_data, "simulated_task_data", 19) == 0) {
        // Handle the default simulated task from your modem code
        snprintf(response, response_size, "SIMULATED_RESPONSE:OK");
        ESP_LOGI(TAG, "🎯 [TASK] Simulated task handled");
        return true;
        
    } else {
        // Unknown task
        ESP_LOGW(TAG, "❓ [TASK] Unknown task: %s", task_data);
        snprintf(response, response_size, "ERROR:UNKNOWN_TASK:%s", task_data);
        g_app_state.errors_encountered++;
        return false;
    }
}

// ============================================================================
// APPLICATION INITIALIZATION - REGISTER YOUR CALLBACKS
// ============================================================================

/**
 * @brief Initialize and register all your callback functions
 * Call this from your app_main() function
 */
void my_application_init(void) {
    ESP_LOGI(TAG, "🚀 Initializing MY APPLICATION with callbacks...");
    
    // Register YOUR callback functions with the modem system
    ESP_LOGI(TAG, "📞 Registering data received callback...");
    modem_register_data_callback(my_data_received_callback);
    
    ESP_LOGI(TAG, "📞 Registering status change callback...");
    modem_register_status_callback(my_status_changed_callback);
    
    ESP_LOGI(TAG, "📞 Registering task processor callback...");
    modem_register_task_processor(my_task_processor_callback);
    
    ESP_LOGI(TAG, "✅ All callbacks registered successfully!");
    ESP_LOGI(TAG, "🎯 Your application is now connected to the modem system!");
    
    // Initialize your application state
    g_app_state.uptime_seconds = 0;
    
    ESP_LOGI(TAG, "📱 Application ready to receive:");
    ESP_LOGI(TAG, "   📥 Data from server via my_data_received_callback()");
    ESP_LOGI(TAG, "   📡 Status updates via my_status_changed_callback()");
    ESP_LOGI(TAG, "   ⚙️  Task requests via my_task_processor_callback()");
}

// ============================================================================
// OPTIONAL: PERIODIC TASKS FOR YOUR APPLICATION
// ============================================================================

/**
 * @brief Optional background task for your application
 * This can send periodic data to the server
 */
void my_application_task(void *pvParameters) {
    ESP_LOGI(TAG, "🔄 Starting application background task...");
    
    while (1) {
        // Update uptime
        g_app_state.uptime_seconds += 30;
        
        // Send periodic heartbeat if connected
        if (modem_is_connected()) {
            char heartbeat[128];
            snprintf(heartbeat, sizeof(heartbeat), 
                    "HEARTBEAT:uptime=%lu,temp=%.1f,battery=%d",
                    g_app_state.uptime_seconds,
                    g_app_state.temperature,
                    g_app_state.battery_level);
            
            ESP_LOGI(TAG, "💓 Sending heartbeat: %s", heartbeat);
            modem_send_tcp_data(heartbeat);
        }
        
        // Sleep for 30 seconds
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/**
 * @brief Start the optional background task
 */
void my_application_start_background_task(void) {
    xTaskCreate(my_application_task, "my_app_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "🔄 Background task started");
}

// ============================================================================
// INTEGRATION EXAMPLE FOR YOUR app_main()
// ============================================================================

#ifdef EXAMPLE_APP_MAIN
/**
 * @brief Example of how to integrate this into your app_main()
 */
void app_main(void) {
    ESP_LOGI(TAG, "=== YOUR ESP32 APPLICATION STARTING ===");
    
    // Initialize NVS
    nvs_flash_init();
    
    // 1. Initialize YOUR application callbacks FIRST
    my_application_init();
    
    // 2. Initialize the modem system
    modem_config_t config = modem_get_default_config();
    strcpy(config.server_address, "your-server.com");
    config.server_port = 8080;
    strcpy(config.protocol, "TCP");
    
    if (modem_handler_init(&config)) {
        ESP_LOGI(TAG, "✅ Modem initialized");
        
        if (modem_handler_start()) {
            ESP_LOGI(TAG, "✅ Modem started");
            ESP_LOGI(TAG, "🎯 System is now LIVE - callbacks will be called automatically!");
            
            // 3. Start your optional background task
            my_application_start_background_task();
            
        } else {
            ESP_LOGE(TAG, "❌ Failed to start modem");
        }
    } else {
        ESP_LOGE(TAG, "❌ Failed to initialize modem");
    }
    
    // Your main application loop (if needed)
    while (1) {
        // Your application can do other work here
        // The callbacks handle all modem events automatically!
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

/*
 * INTEGRATION INSTRUCTIONS:
 * 
 * 1. Add this file to your project
 * 2. Include it in your CMakeLists.txt
 * 3. In your app_main(), call:
 *    my_application_init();
 *    // then your existing modem init code
 * 
 * 4. Your callbacks will automatically be called:
 *    - When TCP data arrives
 *    - When connection status changes  
 *    - When server sends tasks
 * 
 * 5. Customize the callback functions for your specific needs!
 */