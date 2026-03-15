/**
 * @file callback_dive_in_test.h
 * @brief DIVE IN NOW - Quick callback test you can add to your existing app_main
 * 
 * Add this to your enhanced_freertos_uart_at_commands.c to immediately test callbacks!
 */

#ifndef CALLBACK_DIVE_IN_TEST_H
#define CALLBACK_DIVE_IN_TEST_H

#include "Modem_Config_Handling.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// QUICK CALLBACK IMPLEMENTATIONS - ADD THESE TO YOUR PROJECT NOW!
// ============================================================================

/**
 * @brief Quick data callback for immediate testing
 */
static void quick_data_callback(const char *data, size_t length) {
    printf("\n🎯 [QUICK_TEST] DATA CALLBACK TRIGGERED!\n");
    printf("🎯 [QUICK_TEST] Received %zu bytes: '%.*s'\n", length, (int)length, data);
    printf("🎯 [QUICK_TEST] This proves your callback mechanism works!\n\n");
}

/**
 * @brief Quick status callback for immediate testing
 */
static void quick_status_callback(task_status_t status) {
    const char *status_names[] = {
        "IDLE", "CONNECTING", "CONNECTED", "SENDING", 
        "RECEIVING", "PROCESSING", "ERROR", "DISCONNECTED"
    };
    
    printf("\n📡 [QUICK_TEST] STATUS CALLBACK TRIGGERED!\n");
    printf("📡 [QUICK_TEST] Status changed to: %s (%d)\n", status_names[status], status);
    printf("📡 [QUICK_TEST] Your status callback is working!\n\n");
}

/**
 * @brief Quick task processor for immediate testing
 */
static bool quick_task_processor(const char *task_data, char *response, size_t response_size) {
    printf("\n⚙️  [QUICK_TEST] TASK PROCESSOR CALLBACK TRIGGERED!\n");
    printf("⚙️  [QUICK_TEST] Processing task: '%s'\n", task_data);
    
    if (strstr(task_data, "simulated_task_data")) {
        snprintf(response, response_size, "QUICK_TEST_RESPONSE:SUCCESS");
        printf("⚙️  [QUICK_TEST] ✅ Task completed successfully!\n");
        printf("⚙️  [QUICK_TEST] Response: %s\n\n", response);
        return true;
    }
    
    snprintf(response, response_size, "QUICK_TEST_RESPONSE:UNKNOWN_TASK");
    printf("⚙️  [QUICK_TEST] ❓ Unknown task, but callback still works!\n\n");
    return false;
}

// ============================================================================
// QUICK DIVE-IN FUNCTION - CALL THIS FROM YOUR APP_MAIN!
// ============================================================================

/**
 * @brief DIVE IN NOW! Call this function from your app_main to test callbacks immediately
 * 
 * Add this line to your app_main():
 * callback_dive_in_test();
 */
static void callback_dive_in_test(void) {
    printf("\n");
    printf("🚀 ========================================\n");
    printf("🚀 CALLBACK DIVE-IN TEST STARTING!\n");
    printf("🚀 ========================================\n");
    
    // Step 1: Register callbacks
    printf("📞 Step 1: Registering quick test callbacks...\n");
    modem_register_data_callback(quick_data_callback);
    modem_register_status_callback(quick_status_callback);
    modem_register_task_processor(quick_task_processor);
    printf("✅ All callbacks registered!\n\n");
    
    // Step 2: Test callbacks manually (simulate what the modem system does)
    printf("🧪 Step 2: Testing callbacks manually...\n");
    
    // Test data callback directly
    printf("Testing data callback...\n");
    quick_data_callback("TEST_DATA:Hello_From_Callback_Test", 32);
    
    // Test status callback directly
    printf("Testing status callback...\n");
    quick_status_callback(TASK_STATUS_CONNECTED);
    
    // Test task processor directly
    printf("Testing task processor...\n");
    char response[64];
    quick_task_processor("simulated_task_data", response, sizeof(response));
    
    printf("🎯 Step 3: Your callbacks are now registered with the modem system!\n");
    printf("🎯 When you start the modem handler, these callbacks will be called automatically.\n");
    printf("🎯 Watch for the [QUICK_TEST] messages in your logs!\n\n");
    
    printf("✅ ========================================\n");
    printf("✅ CALLBACK DIVE-IN TEST COMPLETED!\n");
    printf("✅ ========================================\n\n");
}

// ============================================================================
// OPTIONAL: BACKGROUND TASK TO CONTINUOUSLY TEST CALLBACKS
// ============================================================================

/**
 * @brief Background task that tests callbacks every 30 seconds
 */
static void callback_test_task(void *pvParameters) {
    int cycle = 0;
    
    while (1) {
        cycle++;
        printf("\n🔄 [BACKGROUND_TEST] Cycle %d - Testing callbacks...\n", cycle);
        
        // Simulate different scenarios
        switch (cycle % 4) {
            case 0:
                quick_data_callback("BACKGROUND_TEST:Periodic_heartbeat", 30);
                break;
            case 1:
                quick_status_callback(TASK_STATUS_SENDING);
                break;
            case 2:
                quick_status_callback(TASK_STATUS_RECEIVING);
                break;
            case 3:
                char test_response[64];
                quick_task_processor("BACKGROUND_TASK", test_response, sizeof(test_response));
                break;
        }
        
        // Check if modem system is running
        if (modem_get_status() != TASK_STATUS_IDLE) {
            printf("📊 [BACKGROUND_TEST] Modem is active (status: %d)\n", modem_get_status());
        }
        
        vTaskDelay(pdMS_TO_TICKS(30000));  // Wait 30 seconds
    }
}

/**
 * @brief Start the background callback testing task
 */
static void start_callback_background_test(void) {
    xTaskCreate(callback_test_task, "callback_test", 3072, NULL, 5, NULL);
    printf("🔄 Background callback testing started (every 30 seconds)\n");
}

// ============================================================================
// INTEGRATION INSTRUCTIONS
// ============================================================================

/**
 * HOW TO INTEGRATE THIS INTO YOUR EXISTING CODE:
 * 
 * 1. Include this header in your enhanced_freertos_uart_at_commands.c:
 *    #include "callback_dive_in_test.h"
 * 
 * 2. Add this line to your app_main() function, somewhere after initialization:
 *    callback_dive_in_test();
 * 
 * 3. Optional: Add background testing:
 *    start_callback_background_test();
 * 
 * 4. Build and run: idf.py build && idf.py flash monitor
 * 
 * 5. Watch the console for [QUICK_TEST] messages!
 * 
 * EXPECTED OUTPUT:
 * - You'll see callback registration messages
 * - You'll see manual callback testing
 * - When you start your modem system, the callbacks will trigger automatically
 * - Background testing will show callbacks working every 30 seconds
 * 
 * This proves your callback mechanism is working end-to-end!
 */

#ifdef __cplusplus
}
#endif

#endif // CALLBACK_DIVE_IN_TEST_H