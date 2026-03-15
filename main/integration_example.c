/**
 * @file integration_example.c
 * @brief Complete integration example showing how to use the AT command system
 * 
 * This file demonstrates the complete integration of:
 * - Task control (start/stop modem init)
 * - AT command examples
 * - Proper header includes
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// Include the AT command API and examples
#include "at_command_api.h"
#include "at_command_examples.h"

static const char *TAG = "INTEGRATION";

/**
 * @brief Task that demonstrates the complete AT command system integration
 */
void integration_demo_task(void *pvParameters) {

    ESP_LOGI(TAG, "Starting AT Command System Integration Demo");
    
    // Wait for the main system to initialize
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Step 1: Start modem initialization
    ESP_LOGI(TAG, "Step 1: Starting modem initialization");
    if (start_modem_init_task()) {
        ESP_LOGI(TAG, "Modem initialization task started successfully");
        
        // Wait for initialization to complete
        while (is_modem_init_active()) {
            ESP_LOGI(TAG, "Waiting for modem initialization...");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        ESP_LOGI(TAG, "Modem initialization completed!");
    } else {
        ESP_LOGW(TAG, "Failed to start modem initialization or already running");
    }
    
    // Step 2: Wait a bit for modem to settle
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Step 3: Run basic AT command examples
    ESP_LOGI(TAG, "Step 2: Running AT command examples");
    
    // Basic AT test
    example_simple_at_command();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Signal quality check
    example_check_signal_quality();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Network registration check
    example_check_network_registration();
    
    // Check if we can proceed with advanced examples
    // You could implement additional logic here to check network status
    ESP_LOGI(TAG, "Network registration check completed - you can uncomment advanced examples if connected");
    
    // You can uncomment these for real modem testing:
    // example_setup_gsm_network("internet", "", ""); // Replace with your APN
    // example_send_sms("+1234567890", "Test from ESP32-C3"); // Replace with real number
    // example_http_get("http://httpbin.org/get");
    
    // Step 4: Run complete test if desired
    ESP_LOGI(TAG, "Step 3: Running complete modem test");
    vTaskDelay(pdMS_TO_TICKS(2000));
    // example_complete_modem_test(); // Uncomment to run full test
    
    ESP_LOGI(TAG, "Integration demo completed!");
    
    // Delete this demo task
    vTaskDelete(NULL);
}

/**
 * @brief Start the integration demo
 * Call this from your main application after the AT command system is initialized
 */
void start_integration_demo(void) {
    xTaskCreate(
        integration_demo_task,
        "integration_demo",
        4096,
        NULL,
        6,
        NULL
    );
    
    ESP_LOGI(TAG, "Integration demo task created");
}

/**
 * @brief Example of how to include this in your main app_main()
 * 
 * void app_main(void) {
 *     // Your main AT command system initialization code here...
 *     
 *     // Start the integration demo
 *     start_integration_demo();
 * }
 */