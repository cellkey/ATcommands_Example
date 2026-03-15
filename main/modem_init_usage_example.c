/**
 * @file modem_init_usage_example.c
 * 
 * @brief Example showing how to use the modem initialization task control
 * 
 * This file demonstrates various scenarios for controlling the modem
 * initialization task, including starting, stopping, and monitoring.
 * 
 * need to include "modem_task_control.h"
 *
 * Set DEBUG_STOP_AFTER_INIT to 1 to stop here after first init (no Example 2, 3, ...).
 * Set to 0 for normal run (all examples).
 */
#define DEBUG_STOP_AFTER_INIT  1

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "modem_task_control.h"
#include "a7670e_sequences.h"

static const char *TAG = "MODEM_EXAMPLE";

/**
 * @brief Example 1: Start modem init and wait for completion
 */
void example_start_and_wait(void) {

    ESP_LOGI(TAG, "=== Example 1: Start and Wait ===");
    
    // Check if already running
    if (is_modem_init_active()) {
        ESP_LOGW(TAG, "Modem initialization already in progress");
        return;
    }
    
    // Start the initialization
    if (start_modem_init_task()) {

        ESP_LOGI(TAG, "Modem initialization started successfully");
        
        // Wait for completion
        int wait_count = 0;
        while (is_modem_init_active()) {
            if (wait_count == 0) {
                ESP_LOGI(TAG, "Waiting for modem initialization to complete...");
            } else if (wait_count % 5 == 0) {
                ESP_LOGI(TAG, "Still waiting for modem init... (%d s)", wait_count);
            }
            wait_count++;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        ESP_LOGI(TAG, "Modem initialization completed!");
#if DEBUG_STOP_AFTER_INIT
        /* Connect (CIPOPEN + GET + 200 OK) already run inside init task */
        ESP_LOGI(TAG, "[DEBUG] Init + connect done in init task. Stopping here.");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
#endif
    } else {
        ESP_LOGE(TAG, "Failed to start modem initialization");
    }
}

/**
 * @brief Example 2: Start modem init and continue with other tasks
 */
void example_start_and_continue(void) {

    ESP_LOGI(TAG, "=== Example 2: Start and Continue ===");
    
    // Start initialization in background
    if (start_modem_init_task()) {

        ESP_LOGI(TAG, "Modem initialization started in background");
        
        // Continue with other work
        for (int i = 0; i < 10; i++) {

            ESP_LOGI(TAG, "Doing some other work... (%d/10)", i + 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // Check status periodically
            if (!is_modem_init_active()) {
                ESP_LOGI(TAG, "Modem initialization completed during work!");
                break;
            }
        }
    }
}

/**
 * @brief Example 3: Emergency stop scenario
 */
void example_emergency_stop(void) {

    ESP_LOGI(TAG, "=== Example 3: Emergency Stop ===");
    
    // Start initialization
    if (start_modem_init_task()) {
        ESP_LOGD(TAG, "Modem initialization started");
        
        // Wait a bit
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // Simulate emergency condition
        ESP_LOGW(TAG, "Emergency condition detected - stopping initialization");
        
        if (stop_modem_init_task()) {

            ESP_LOGI(TAG, "Modem initialization stopped successfully");
        } else {
            ESP_LOGW(TAG, "Modem initialization was not running or already completed");
        }
    }
}

/**
 * @brief Example 4: modem init Retry mechanism
 */
void example_retry_mechanism(void) {

    ESP_LOGI(TAG, "=== Example 4: Retry Mechanism ===");
    
    const int max_retries = 3;
    int retry_count = 0;
    
    while (retry_count < max_retries) {

        ESP_LOGI(TAG, "Modem initialization attempt %d/%d", retry_count + 1, max_retries);
        
        if (start_modem_init_task()) {
            // Wait for completion with timeout
            int timeout_count = 0;
            const int max_timeout = 30; // 30 seconds
            
            while (is_modem_init_active() && timeout_count < max_timeout) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                timeout_count++;
            }
            
            if (!is_modem_init_active()) {
                ESP_LOGI(TAG, "Modem initialization completed successfully!");
                return; // Success!
            } else {
                ESP_LOGW(TAG, "Modem initialization timed out - stopping and retrying");
                stop_modem_init_task();
            }
        } else {
            ESP_LOGW(TAG, "Failed to start modem initialization");
        }
        
        retry_count++;
        if (retry_count < max_retries) {
            ESP_LOGI(TAG, "Waiting before retry...");
            vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds before retry
        }
    }
    
    ESP_LOGE(TAG, "Modem initialization failed after %d attempts", max_retries);
}

/**
 * @brief Example 5: Conditional initialization based on system state
 */
void example_conditional_init(bool network_available, bool power_saving_mode) {

    ESP_LOGI(TAG, "=== Example 5: Conditional Initialization ===");
    
    ESP_LOGI(TAG, "System state - Network: %s, Power saving: %s",
             network_available ? "Available" : "Unavailable",
             power_saving_mode ? "Enabled" : "Disabled");
    
    // Only initialize if conditions are right
    if (!network_available) {
        ESP_LOGW(TAG, "Network not available - skipping modem initialization");
        return;
    }
    
    if (power_saving_mode) {
        ESP_LOGW(TAG, "Power saving mode active - deferring modem initialization");
        return;
    }
    
    // Check if already initialized recently
    if (is_modem_init_active()) {
        ESP_LOGI(TAG, "Modem initialization already in progress");
        return;
    }
    
    ESP_LOGI(TAG, "Conditions met - starting modem initialization");
    start_modem_init_task();
}

/**
 * @brief Main demonstration task
 */
void modem_control_demo_task(void *pvParameters) {

    ESP_LOGI(TAG, "Starting modem initialization control examples");
    
    // Wait for system to be ready
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Run examples
    example_start_and_wait();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    example_start_and_continue();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    example_emergency_stop();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    example_retry_mechanism();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test conditional initialization with different states
    example_conditional_init(false, false); // Network unavailable
    example_conditional_init(true, true);   // Power saving mode
    example_conditional_init(true, false);  // Good conditions
    
    ESP_LOGI(TAG, "Modem initialization control examples completed");
    
    // Delete this demo task
    vTaskDelete(NULL);
}

/**
 * @brief Start modem initialization (production).
 * 
 * Starts the modem init task which runs A7670E init, connects to server, and starts keepalive.
 * Note: Make sure the main UART AT command system is already initialized.
 */
void start_modem_init(void) {
    if (start_modem_init_task()) {
        ESP_LOGD(TAG, "Modem initialization started");
    } else {
        ESP_LOGW(TAG, "Modem initialization start failed (may already be running)");
    }
}

/* Legacy name for backward compatibility (if needed). */
void start_modem_control_examples(void) {
    start_modem_init();
}