/**
 * @file integration_example.c
 * @brief HANDS-ON INTEGRATION - Test your callbacks with the existing modem system
 * 
 * This is a complete, ready-to-compile example that integrates with your
 * existing Modem_Config_Handling system. Use this to test and understand
 * how callbacks work in practice.
 */

#include "Modem_Config_Handling.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "INTEGRATION_TEST";

// ============================================================================
// STEP 1: SIMPLE CALLBACK IMPLEMENTATIONS FOR TESTING
// ============================================================================

/**
 * @brief Simple data callback for testing
 */
void test_data_callback(const char *data, size_t length) {
    ESP_LOGI(TAG, "🎯 [TEST_DATA] Received %zu bytes: '%.*s'", 
             length, (int)length, data);
    
    // Simple response - just log what we received
    if (strstr(data, "HELLO")) {
        ESP_LOGI(TAG, "🎯 [TEST_DATA] Got greeting from server!");
    }
    if (strstr(data, "ERROR")) {
        ESP_LOGE(TAG, "🎯 [TEST_DATA] Server reported error!");
    }
}

/**
 * @brief Simple status callback for testing
 */
void test_status_callback(task_status_t status) {
    const char *status_names[] = {
        "IDLE", "CONNECTING", "CONNECTED", "SENDING", 
        "RECEIVING", "PROCESSING", "ERROR", "DISCONNECTED"
    };
    
    ESP_LOGI(TAG, "🎯 [TEST_STATUS] Status changed to: %s (%d)", 
             status_names[status], status);
    
    switch (status) {
        case TASK_STATUS_CONNECTED:
            ESP_LOGI(TAG, "🎯 [TEST_STATUS] 🟢 WE'RE ONLINE!");
            break;
        case TASK_STATUS_ERROR:
            ESP_LOGE(TAG, "🎯 [TEST_STATUS] 🔴 CONNECTION ERROR!");
            break;
        case TASK_STATUS_DISCONNECTED:
            ESP_LOGW(TAG, "🎯 [TEST_STATUS] ⚪ DISCONNECTED");
            break;
        default:
            break;
    }
}

/**
 * @brief Simple task processor for testing
 */
bool test_task_processor(const char *task_data, char *response, size_t response_size) {
    ESP_LOGI(TAG, "🎯 [TEST_TASK] Processing task: '%s'", task_data);
    
    // Handle the simulated task from your modem code
    if (strstr(task_data, "simulated_task_data")) {
        snprintf(response, response_size, "TEST_RESPONSE:Task_completed_successfully");
        ESP_LOGI(TAG, "🎯 [TEST_TASK] ✅ Simulated task completed");
        return true;
    }
    
    // Handle simple test commands
    if (strcmp(task_data, "PING") == 0) {
        snprintf(response, response_size, "PONG");
        ESP_LOGI(TAG, "🎯 [TEST_TASK] ✅ PING -> PONG");
        return true;
    }
    
    if (strcmp(task_data, "GET_TIME") == 0) {
        uint32_t uptime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        snprintf(response, response_size, "UPTIME:%lu", uptime);
        ESP_LOGI(TAG, "🎯 [TEST_TASK] ✅ Uptime: %lu seconds", uptime);
        return true;
    }
    
    // Unknown task
    ESP_LOGW(TAG, "🎯 [TEST_TASK] ❓ Unknown task: %s", task_data);
    snprintf(response, response_size, "ERROR:UNKNOWN_TASK");
    return false;
}

// ============================================================================
// STEP 2: MANUAL TESTING FUNCTIONS
// ============================================================================

/**
 * @brief Manually trigger callback tests (for debugging)
 */
void test_callbacks_manually(void) {
    ESP_LOGI(TAG, "🧪 MANUAL CALLBACK TESTING");
    ESP_LOGI(TAG, "==========================");
    
    // Test data callback directly
    ESP_LOGI(TAG, "1. Testing data callback...");
    test_data_callback("HELLO from manual test", 22);
    test_data_callback("ERROR: Manual test error", 24);
    
    // Test status callback directly  
    ESP_LOGI(TAG, "2. Testing status callback...");
    test_status_callback(TASK_STATUS_CONNECTING);
    test_status_callback(TASK_STATUS_CONNECTED);
    test_status_callback(TASK_STATUS_ERROR);
    
    // Test task processor directly
    ESP_LOGI(TAG, "3. Testing task processor...");
    char response[128];
    bool result1 = test_task_processor("PING", response, sizeof(response));
    ESP_LOGI(TAG, "   PING result: %s (success: %s)", response, result1 ? "YES" : "NO");
    
    bool result2 = test_task_processor("GET_TIME", response, sizeof(response));
    ESP_LOGI(TAG, "   GET_TIME result: %s (success: %s)", response, result2 ? "YES" : "NO");
    
    bool result3 = test_task_processor("UNKNOWN", response, sizeof(response));
    ESP_LOGI(TAG, "   UNKNOWN result: %s (success: %s)", response, result3 ? "YES" : "NO");
    
    ESP_LOGI(TAG, "✅ Manual testing completed!");
}

/**
 * @brief Send test data to server if connected
 */
void send_test_data(void) {
    if (modem_is_connected()) {
        ESP_LOGI(TAG, "📤 Sending test data to server...");
        
        // Send some test messages
        modem_send_tcp_data("TEST_MESSAGE:Hello from ESP32!");
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        modem_send_tcp_data("STATUS:Device online and ready");
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        modem_send_tcp_data("SENSOR_DATA:temp=25.3,humidity=65.2");
        
        ESP_LOGI(TAG, "✅ Test data sent!");
    } else {
        ESP_LOGW(TAG, "⚠️  Cannot send test data - not connected");
    }
}

// ============================================================================
// STEP 3: INTEGRATION AND TESTING
// ============================================================================

/**
 * @brief Initialize callbacks and test the integration
 */
void integration_test_init(void) {
    ESP_LOGI(TAG, "🔧 INITIALIZING INTEGRATION TEST");
    ESP_LOGI(TAG, "==================================");
    
    // Register our test callbacks
    ESP_LOGI(TAG, "📞 Registering test callbacks...");
    modem_register_data_callback(test_data_callback);
    modem_register_status_callback(test_status_callback);
    modem_register_task_processor(test_task_processor);
    ESP_LOGI(TAG, "✅ Test callbacks registered!");
    
    // Test callbacks manually first
    test_callbacks_manually();
    
    ESP_LOGI(TAG, "🎯 Integration test ready!");
    ESP_LOGI(TAG, "   - Callbacks are now registered with the modem system");
    ESP_LOGI(TAG, "   - They will be called automatically when events occur");
    ESP_LOGI(TAG, "   - Start the modem system to see them in action!");
}

/**
 * @brief Background task for testing
 */
void integration_test_task(void *pvParameters) {
    ESP_LOGI(TAG, "🔄 Starting integration test background task...");
    
    int test_cycle = 0;
    
    while (1) {
        test_cycle++;
        ESP_LOGI(TAG, "🔄 Test cycle %d", test_cycle);
        
        // Check connection status
        task_status_t status = modem_get_status();
        ESP_LOGI(TAG, "📡 Current modem status: %d", status);
        
        // Send test data if connected
        if (status == TASK_STATUS_CONNECTED) {
            send_test_data();
        }
        
        // Wait 30 seconds before next cycle
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/**
 * @brief Start the integration test background task
 */
void start_integration_test_task(void) {
    xTaskCreate(integration_test_task, "integration_test", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "🔄 Integration test task started");
}

// ============================================================================
// STEP 4: COMPLETE INTEGRATION EXAMPLE
// ============================================================================

/**
 * @brief Complete integration example - call this from app_main()
 */
void run_complete_integration_test(void) {
    ESP_LOGI(TAG, "🚀 RUNNING COMPLETE INTEGRATION TEST");
    ESP_LOGI(TAG, "====================================");
    
    // Step 1: Initialize callbacks
    integration_test_init();
    
    // Step 2: Configure and start modem system
    ESP_LOGI(TAG, "🔧 Configuring modem system...");
    
    modem_config_t config = modem_get_default_config();
    
    // Customize configuration for testing
    strncpy(config.server_address, "httpbin.org", sizeof(config.server_address));
    config.server_port = 80;
    strncpy(config.protocol, "TCP", sizeof(config.protocol));
    config.task_interval = 15000;  // Test every 15 seconds
    config.max_task_cycles = 5;    // Limit to 5 cycles for testing
    
    ESP_LOGI(TAG, "📋 Test configuration:");
    ESP_LOGI(TAG, "   Server: %s:%d", config.server_address, config.server_port);
    ESP_LOGI(TAG, "   Protocol: %s", config.protocol);
    ESP_LOGI(TAG, "   Interval: %lu ms", config.task_interval);
    ESP_LOGI(TAG, "   Max cycles: %d", config.max_task_cycles);
    
    // Step 3: Initialize modem
    if (modem_handler_init(&config)) {
        ESP_LOGI(TAG, "✅ Modem handler initialized successfully!");
        
        // Step 4: Start modem
        if (modem_handler_start()) {
            ESP_LOGI(TAG, "✅ Modem handler started successfully!");
            ESP_LOGI(TAG, "🎯 CALLBACKS ARE NOW ACTIVE!");
            ESP_LOGI(TAG, "   - Watch for callback messages in the logs");
            ESP_LOGI(TAG, "   - Status changes will trigger test_status_callback()");
            ESP_LOGI(TAG, "   - Data reception will trigger test_data_callback()");
            ESP_LOGI(TAG, "   - Tasks will trigger test_task_processor()");
            
            // Step 5: Start background testing
            start_integration_test_task();
            
        } else {
            ESP_LOGE(TAG, "❌ Failed to start modem handler!");
        }
    } else {
        ESP_LOGE(TAG, "❌ Failed to initialize modem handler!");
    }
}

// ============================================================================
// STEP 5: EXAMPLE APP_MAIN INTEGRATION
// ============================================================================

/**
 * @brief Example app_main() function showing how to integrate everything
 */
void app_main(void) {
    ESP_LOGI(TAG, "=== CALLBACK INTEGRATION TEST APP ===");
    
    // Initialize NVS (required for ESP32)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Run the complete integration test
    run_complete_integration_test();
    
    // Main application loop
    ESP_LOGI(TAG, "🔄 Entering main application loop...");
    ESP_LOGI(TAG, "   The callbacks are now handling all modem events!");
    ESP_LOGI(TAG, "   Watch the logs to see them in action.");
    
    while (1) {
        // Your main application work here
        // The callbacks handle all modem events automatically!
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // Sleep 5 seconds
        
        // Optional: Print status periodically
        static int status_counter = 0;
        if (++status_counter >= 12) {  // Every minute (12 * 5 seconds)
            ESP_LOGI(TAG, "📊 Status check - Modem status: %d, Connected: %s", 
                     modem_get_status(), modem_is_connected() ? "YES" : "NO");
            status_counter = 0;
        }
    }
}

/*
 * COMPILATION INSTRUCTIONS:
 * 
 * 1. Add this file to your main/ directory
 * 2. Update your CMakeLists.txt to include it
 * 3. Build with: idf.py build
 * 4. Flash with: idf.py flash monitor
 * 
 * WHAT YOU'LL SEE:
 * - Manual callback testing first
 * - Modem initialization messages
 * - Status change callbacks as modem connects
 * - Task processor callbacks when server sends tasks
 * - Data callbacks if server sends data
 * - Background test messages every 30 seconds
 * 
 * This gives you HANDS-ON experience with the callback mechanism!
 */