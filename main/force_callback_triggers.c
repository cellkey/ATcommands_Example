/**
 * @file force_callback_triggers.c
 * @brief FORCE CALLBACKS TO HAPPEN - Manual trigger system for testing
 * 
 * This lets you manually trigger callbacks with realistic data
 * without waiting for real network communication.
 */

#include "Modem_Config_Handling.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "FORCE_CALLBACKS";

// ============================================================================
// MANUAL TRIGGER FUNCTIONS - FORCE CALLBACKS TO HAPPEN NOW!
// ============================================================================

/**
 * @brief Force data callback with realistic server data
 */
void force_data_callback_trigger(void) {
    ESP_LOGI(TAG, "🚀 FORCING data callback to trigger...");
    
    // Simulate realistic server data scenarios
    const char *realistic_data[] = {
        "SENSOR_REQUEST:temperature",
        "CMD:LED_ON",
        "STATUS:server_online", 
        "TASK:get_battery_level",
        "CONFIG:update_interval=30",
        "ALERT:low_battery_warning",
        "DATA:temp=25.3,humidity=65.2",
        "HEARTBEAT:server_ping"
    };
    
    int num_scenarios = sizeof(realistic_data) / sizeof(realistic_data[0]);
    
    for (int i = 0; i < num_scenarios; i++) {
        size_t data_length = strlen(realistic_data[i]);
        
        ESP_LOGI(TAG, "📡 [FORCE] Triggering with: '%s' (length: %zu)", 
                 realistic_data[i], data_length);
        
        // FORCE THE CALLBACK TO HAPPEN USING PUBLIC FUNCTION!
        modem_trigger_data_callback(realistic_data[i], data_length);
        
        ESP_LOGI(TAG, "✅ [FORCE] Data callback completed");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second between triggers
    }
    
    ESP_LOGI(TAG, "🎯 All forced data callbacks completed!");
}

/**
 * @brief Force status callback through all states
 */
void force_status_callback_trigger(void) {
    ESP_LOGI(TAG, "🚀 FORCING status callback through all states...");
    
    task_status_t all_states[] = {
        TASK_STATUS_IDLE,
        TASK_STATUS_CONNECTING,
        TASK_STATUS_CONNECTED,
        TASK_STATUS_SENDING,
        TASK_STATUS_RECEIVING,
        TASK_STATUS_PROCESSING,
        TASK_STATUS_ERROR,
        TASK_STATUS_DISCONNECTED
    };
    
    const char *state_names[] = {
        "IDLE", "CONNECTING", "CONNECTED", "SENDING",
        "RECEIVING", "PROCESSING", "ERROR", "DISCONNECTED"
    };
    
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "📡 [FORCE] Triggering status: %s (%d)", 
                 state_names[i], all_states[i]);
        
        // FORCE THE STATUS CALLBACK USING PUBLIC FUNCTION!
        modem_trigger_status_callback(all_states[i]);
        
        ESP_LOGI(TAG, "✅ [FORCE] Status callback completed");
        vTaskDelay(pdMS_TO_TICKS(1500)); // Wait 1.5 seconds between states
    }
    
    ESP_LOGI(TAG, "🎯 All forced status callbacks completed!");
}

/**
 * @brief Force task processor callback with realistic tasks
 */
void force_task_callback_trigger(void) {
    ESP_LOGI(TAG, "🚀 FORCING task processor callback...");
    
    const char *realistic_tasks[] = {
        "GET_TEMPERATURE",
        "GET_HUMIDITY", 
        "GET_BATTERY",
        "LED_ON",
        "LED_OFF",
        "GET_STATUS",
        "PING",
        "GET_UPTIME",
        "REBOOT",
        "UNKNOWN_TASK"
    };
    
    int num_tasks = sizeof(realistic_tasks) / sizeof(realistic_tasks[0]);
    
    for (int i = 0; i < num_tasks; i++) {
        char response[128];
        
        ESP_LOGI(TAG, "⚙️  [FORCE] Processing task: '%s'", realistic_tasks[i]);
        
        // FORCE THE TASK PROCESSOR CALLBACK USING PUBLIC FUNCTION!
        bool result = modem_trigger_task_callback(realistic_tasks[i], response, sizeof(response));
        
        ESP_LOGI(TAG, "📝 [FORCE] Task result: %s (success: %s)", 
                 response, result ? "YES" : "NO");
        
        vTaskDelay(pdMS_TO_TICKS(800)); // Wait 0.8 seconds between tasks
    }
    
    ESP_LOGI(TAG, "🎯 All forced task callbacks completed!");
}

// ============================================================================
// COMPREHENSIVE FORCE TEST - ALL CALLBACKS AT ONCE
// ============================================================================

/**
 * @brief Force all callbacks in a realistic sequence
 */
void force_complete_callback_sequence(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🚀 ==========================================");
    ESP_LOGI(TAG, "🚀 FORCING COMPLETE CALLBACK SEQUENCE");
    ESP_LOGI(TAG, "🚀 ==========================================");
    
    // Simulate a realistic modem session
    ESP_LOGI(TAG, "📋 Simulating realistic modem communication session...");
    
    // Phase 1: Connection sequence
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📡 Phase 1: Connection Sequence");
    ESP_LOGI(TAG, "--------------------------------");
    
    modem_trigger_status_callback(TASK_STATUS_CONNECTING);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    modem_trigger_status_callback(TASK_STATUS_CONNECTED);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Phase 2: Initial server communication
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📥 Phase 2: Server Data Reception");
    ESP_LOGI(TAG, "----------------------------------");
    
    modem_trigger_data_callback("WELCOME:ESP32_connected", 22);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    modem_trigger_data_callback("CMD:send_status", 15);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Phase 3: Task processing
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "⚙️  Phase 3: Task Processing");
    ESP_LOGI(TAG, "-----------------------------");
    
    char response[64];
    
    modem_trigger_status_callback(TASK_STATUS_PROCESSING);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    modem_trigger_task_callback("GET_STATUS", response, sizeof(response));
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    modem_trigger_status_callback(TASK_STATUS_SENDING);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Phase 4: More server communication
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📤 Phase 4: Bidirectional Communication");
    ESP_LOGI(TAG, "----------------------------------------");
    
    // Phase 4: More server communication
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📤 Phase 4: Bidirectional Communication");
    ESP_LOGI(TAG, "----------------------------------------");
    
    modem_trigger_data_callback("TASK:get_sensor_data", 20);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    char response2[64];
    modem_trigger_task_callback("GET_TEMPERATURE", response2, sizeof(response2));
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    modem_trigger_data_callback("ACK:temperature_received", 24);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Phase 5: Error simulation
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "❌ Phase 5: Error Handling");
    ESP_LOGI(TAG, "---------------------------");
    
    modem_trigger_status_callback(TASK_STATUS_ERROR);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    modem_trigger_status_callback(TASK_STATUS_CONNECTING);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    modem_trigger_status_callback(TASK_STATUS_CONNECTED);
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "✅ ==========================================");
    ESP_LOGI(TAG, "✅ COMPLETE CALLBACK SEQUENCE FINISHED!");
    ESP_LOGI(TAG, "✅ ==========================================");
}

// ============================================================================
// BACKGROUND FORCE TASK - CONTINUOUS CALLBACK FORCING
// ============================================================================

/**
 * @brief Background task that continuously forces callbacks
 */
void force_callback_background_task(void *pvParameters) {
    ESP_LOGI(TAG, "🔄 Starting background callback forcing task...");
    
    int cycle = 0;
    
    while (1) {
        cycle++;
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "🔄 [BACKGROUND] Force cycle %d starting...", cycle);
        
        // Force different callback types in rotation
        switch (cycle % 4) {
            case 1:
                ESP_LOGI(TAG, "🔄 [BACKGROUND] Forcing data callbacks...");
                force_data_callback_trigger();
                break;
                
            case 2:
                ESP_LOGI(TAG, "🔄 [BACKGROUND] Forcing status callbacks...");
                force_status_callback_trigger();
                break;
                
            case 3:
                ESP_LOGI(TAG, "🔄 [BACKGROUND] Forcing task callbacks...");
                force_task_callback_trigger();
                break;
                
            case 0:
                ESP_LOGI(TAG, "🔄 [BACKGROUND] Forcing complete sequence...");
                force_complete_callback_sequence();
                break;
        }
        
        ESP_LOGI(TAG, "⏳ [BACKGROUND] Waiting 45 seconds before next force cycle...");
        vTaskDelay(pdMS_TO_TICKS(45000)); // Wait 45 seconds between cycles
    }
}

/**
 * @brief Start the background callback forcing task
 */
void start_force_callback_background_task(void) {
    xTaskCreate(force_callback_background_task, "force_callbacks", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "🔄 Background callback forcing started (every 45 seconds)");
}

// ============================================================================
// MAIN FORCE FUNCTION - CALL THIS TO START FORCING!
// ============================================================================

/**
 * @brief Main function to force all callbacks - call this from your app!
 */
void force_all_callbacks_now(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "💥 ==========================================");
    ESP_LOGI(TAG, "💥 FORCING ALL CALLBACKS TO HAPPEN NOW!");
    ESP_LOGI(TAG, "💥 ==========================================");
    
    // Immediate one-time forcing
    ESP_LOGI(TAG, "1️⃣  Forcing data callbacks...");
    force_data_callback_trigger();
    
    vTaskDelay(pdMS_TO_TICKS(3000)); // 3 second break
    
    ESP_LOGI(TAG, "2️⃣  Forcing status callbacks...");
    force_status_callback_trigger();
    
    vTaskDelay(pdMS_TO_TICKS(3000)); // 3 second break
    
    ESP_LOGI(TAG, "3️⃣  Forcing task callbacks...");
    force_task_callback_trigger();
    
    vTaskDelay(pdMS_TO_TICKS(3000)); // 3 second break
    
    ESP_LOGI(TAG, "4️⃣  Forcing complete realistic sequence...");
    force_complete_callback_sequence();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "✅ ALL FORCED CALLBACKS COMPLETED!");
    ESP_LOGI(TAG, "✅ Your callbacks have been thoroughly tested!");
    
    // Start background forcing
    ESP_LOGI(TAG, "🔄 Starting continuous background forcing...");
    start_force_callback_background_task();
}

/*
 * USAGE INSTRUCTIONS:
 * 
 * 1. Include this file in your project
 * 2. Add to your app_main():
 *    force_all_callbacks_now();
 * 
 * 3. Watch the console for:
 *    - [FORCE] messages showing callback triggers
 *    - Your actual callback function output
 *    - Background forcing every 45 seconds
 * 
 * This FORCES your callbacks to happen with realistic data
 * without waiting for real network communication!
 */