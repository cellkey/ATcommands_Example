/**
 * @file callback_example.c
 * @brief Complete Callback Example - Shows the entire flow from typedef to usage
 * 
 * This example demonstrates:
 * 1. Typedef declaration
 * 2. Callback registration
 * 3. Callback usage in real code
 * 4. Multiple callback types
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// ============================================================================
// STEP 1: TYPEDEF DECLARATIONS (Usually in .h file)
// ============================================================================

/**
 * @brief Callback type for when data is received
 * @param data Pointer to received data
 * @param length Number of bytes received
 */
typedef void (*data_received_cb_t)(const char *data, size_t length);

/**
 * @brief Callback type for when connection status changes
 * @param connected true if connected, false if disconnected
 * @param error_code Error code (0 = no error)
 */
typedef void (*status_changed_cb_t)(bool connected, int error_code);

/**
 * @brief Callback type for processing tasks
 * @param task_data Input task data to process
 * @param result_buffer Buffer to write result
 * @param buffer_size Size of result buffer
 * @return true if processing successful, false if failed
 */
typedef bool (*task_processor_cb_t)(const char *task_data, char *result_buffer, size_t buffer_size);

// ============================================================================
// STEP 2: STORAGE FOR CALLBACK POINTERS (Usually static in .c file)
// ============================================================================

// Storage for registered callbacks (initially NULL)
static data_received_cb_t data_callback = NULL;
static status_changed_cb_t status_callback = NULL;
static task_processor_cb_t task_callback = NULL;

// Simulated system state
static bool system_connected = false;
static char received_buffer[256];

// ============================================================================
// STEP 3: REGISTRATION FUNCTIONS (Public API)
// ============================================================================

/**
 * @brief Register a callback for data received events
 * @param callback Your function to be called when data arrives
 */
void register_data_callback(data_received_cb_t callback) {
    data_callback = callback;
    printf("✓ Data callback registered\n");
}

/**
 * @brief Register a callback for status change events
 * @param callback Your function to be called when connection status changes
 */
void register_status_callback(status_changed_cb_t callback) {
    status_callback = callback;
    printf("✓ Status callback registered\n");
}

/**
 * @brief Register a callback for task processing
 * @param callback Your function to be called when tasks need processing
 */
void register_task_processor(task_processor_cb_t callback) {
    task_callback = callback;
    printf("✓ Task processor callback registered\n");
}

// ============================================================================
// STEP 4: INTERNAL FUNCTIONS THAT USE CALLBACKS
// ============================================================================

/**
 * @brief Simulate connection status change
 * @param connected New connection status
 * @param error_code Error code (0 = success)
 */
void simulate_connection_change(bool connected, int error_code) {
    system_connected = connected;
    
    // Call user's callback if registered
    if (status_callback) {
        printf("📞 Calling status callback...\n");
        status_callback(connected, error_code);
    } else {
        printf("⚠️  No status callback registered\n");
    }
}

/**
 * @brief Simulate data reception
 * @param data Received data
 */
void simulate_data_received(const char *data) {
    size_t length = strlen(data);
    strcpy(received_buffer, data);
    
    // Call user's callback if registered
    if (data_callback) {
        printf("📞 Calling data callback...\n");
        data_callback(data, length);
    } else {
        printf("⚠️  No data callback registered\n");
    }
}

/**
 * @brief Simulate task processing
 * @param task_data Task to process
 */
void simulate_task_processing(const char *task_data) {
    // Call user's callback if registered
    if (task_callback) {
        char result[128];
        printf("📞 Calling task processor callback...\n");
        
        bool success = task_callback(task_data, result, sizeof(result));
        if (success) {
            printf("✅ Task processed successfully: %s\n", result);
        } else {
            printf("❌ Task processing failed\n");
        }
    } else {
        printf("⚠️  No task processor callback registered\n");
    }
}

// ============================================================================
// STEP 5: USER APPLICATION CALLBACK FUNCTIONS
// ============================================================================

/**
 * @brief User's data received handler
 * This function will be called when data arrives
 */
void my_data_handler(const char *data, size_t length) {
    printf("🎯 MY APP: Received %zu bytes: '%s'\n", length, data);
    
    // User can do whatever they want here:
    if (strstr(data, "HELLO")) {
        printf("🎯 MY APP: Received greeting!\n");
    }
    if (strstr(data, "ERROR")) {
        printf("🎯 MY APP: Received error message!\n");
    }
}

/**
 * @brief User's status change handler
 * This function will be called when connection status changes
 */
void my_status_handler(bool connected, int error_code) {
    if (connected) {
        printf("🎯 MY APP: 🟢 Connected successfully!\n");
    } else {
        printf("🎯 MY APP: 🔴 Disconnected (error: %d)\n", error_code);
    }
}

/**
 * @brief User's task processor
 * This function will be called when tasks need processing
 */
bool my_task_processor(const char *task_data, char *result_buffer, size_t buffer_size) {
    printf("🎯 MY APP: Processing task: '%s'\n", task_data);
    
    // Process the task (example logic)
    if (strcmp(task_data, "GET_TEMPERATURE") == 0) {
        snprintf(result_buffer, buffer_size, "TEMPERATURE: 25.3°C");
        return true;
    } else if (strcmp(task_data, "GET_BATTERY") == 0) {
        snprintf(result_buffer, buffer_size, "BATTERY: 85%%");
        return true;
    } else {
        snprintf(result_buffer, buffer_size, "UNKNOWN_TASK");
        return false;
    }
}

// ============================================================================
// STEP 6: DEMONSTRATION - REAL USAGE EXAMPLE
// ============================================================================

/**
 * @brief Main demonstration function
 */
int main(void) {
    printf("=== CALLBACK MECHANISM DEMONSTRATION ===\n\n");

    // Phase 1: Register callbacks
    printf("📋 Phase 1: Registering Callbacks\n");
    printf("----------------------------------------\n");
    register_data_callback(my_data_handler);
    register_status_callback(my_status_handler);
    register_task_processor(my_task_processor);
    printf("\n");

    // Phase 2: Simulate system events
    printf("📋 Phase 2: Simulating System Events\n");
    printf("----------------------------------------\n");
    
    // Simulate connection
    printf("🔌 Connecting to server...\n");
    simulate_connection_change(true, 0);
    printf("\n");
    
    // Simulate data reception
    printf("📥 Receiving data...\n");
    simulate_data_received("HELLO ESP32!");
    simulate_data_received("SENSOR_DATA: 25.3");
    simulate_data_received("ERROR: Timeout");
    printf("\n");
    
    // Simulate task processing
    printf("⚙️  Processing tasks...\n");
    simulate_task_processing("GET_TEMPERATURE");
    simulate_task_processing("GET_BATTERY");
    simulate_task_processing("UNKNOWN_COMMAND");
    printf("\n");
    
    // Simulate disconnection
    printf("🔌 Disconnecting...\n");
    simulate_connection_change(false, 123);
    printf("\n");

    // Phase 3: Demonstrate what happens without callbacks
    printf("📋 Phase 3: Clearing Callbacks (Show what happens without them)\n");
    printf("----------------------------------------------------------------\n");
    register_data_callback(NULL);  // Clear callback
    printf("📥 Trying to receive data without callback...\n");
    simulate_data_received("This won't trigger callback");
    printf("\n");

    printf("=== DEMONSTRATION COMPLETE ===\n");
    return 0;
}

// ============================================================================
// STEP 7: REAL-WORLD ESP32 EXAMPLE (How you'd use this in your modem code)
// ============================================================================

#ifdef ESP32_EXAMPLE
/**
 * @brief Example of how to use this in your ESP32 modem application
 */
void esp32_modem_example(void) {
    // In your app_main() or initialization function:
    
    // 1. Register your callbacks
    register_data_callback(my_data_handler);
    register_status_callback(my_status_handler);
    register_task_processor(my_task_processor);
    
    // 2. Initialize modem system
    modem_handler_init(NULL);  // Your existing modem code
    modem_handler_start();
    
    // 3. The modem system will now call your functions automatically:
    //    - When TCP data arrives → my_data_handler() called
    //    - When connection changes → my_status_handler() called  
    //    - When tasks need processing → my_task_processor() called
    
    // 4. Your app just waits or does other work
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Your callbacks handle all the events!
    }
}
#endif

/* 
 * COMPILATION INSTRUCTIONS:
 * 
 * To compile and run this example:
 * gcc callback_example.c -o callback_example
 * ./callback_example
 * 
 * Expected Output:
 * - Registration messages
 * - Callback execution messages
 * - Your custom handler responses
 * - Demonstration of missing callbacks
 */