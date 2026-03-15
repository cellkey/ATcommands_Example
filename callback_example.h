/**
 * @file callback_example.h
 * @brief Header file showing proper typedef declarations for callbacks
 * 
 * This header demonstrates the standard pattern:
 * - Typedef declarations in header
 * - Registration functions declared
 * - Clear documentation for each callback type
 */

#ifndef CALLBACK_EXAMPLE_H
#define CALLBACK_EXAMPLE_H

#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// CALLBACK TYPEDEF DECLARATIONS (Standard pattern)
// ============================================================================

/**
 * @brief Callback function type for data reception events
 * 
 * This callback is called whenever new data is received from the modem.
 * 
 * @param data Pointer to the received data (null-terminated string)
 * @param length Number of bytes in the data
 * 
 * @note The data pointer is only valid during the callback execution.
 *       If you need to keep the data, make a copy.
 * 
 * Example usage:
 * @code
 * void my_data_handler(const char *data, size_t length) {
 *     printf("Received: %s (%zu bytes)\n", data, length);
 * }
 * @endcode
 */
typedef void (*data_received_cb_t)(const char *data, size_t length);

/**
 * @brief Callback function type for connection status changes
 * 
 * This callback is called whenever the connection status changes.
 * 
 * @param connected true if now connected, false if disconnected
 * @param error_code Error code (0 = no error, >0 = specific error)
 * 
 * Common error codes:
 * - 0: No error (successful connection/disconnection)
 * - 1: Network unreachable
 * - 2: Connection timeout
 * - 3: Authentication failed
 * 
 * Example usage:
 * @code
 * void my_status_handler(bool connected, int error_code) {
 *     if (connected) {
 *         printf("Connected!\n");
 *     } else {
 *         printf("Disconnected (error: %d)\n", error_code);
 *     }
 * }
 * @endcode
 */
typedef void (*status_changed_cb_t)(bool connected, int error_code);

/**
 * @brief Callback function type for task processing
 * 
 * This callback is called when a task needs to be processed.
 * The callback should process the input and write results to the buffer.
 * 
 * @param task_data Input data describing the task to process
 * @param result_buffer Buffer where the result should be written
 * @param buffer_size Maximum size of the result buffer
 * @return true if task was processed successfully, false if failed
 * 
 * @note Always check buffer_size before writing to result_buffer
 * @note Return false for unknown/unsupported tasks
 * 
 * Example usage:
 * @code
 * bool my_task_processor(const char *task_data, char *result_buffer, size_t buffer_size) {
 *     if (strcmp(task_data, "GET_TEMP") == 0) {
 *         snprintf(result_buffer, buffer_size, "TEMP: 25.3C");
 *         return true;
 *     }
 *     return false; // Unknown task
 * }
 * @endcode
 */
typedef bool (*task_processor_cb_t)(const char *task_data, char *result_buffer, size_t buffer_size);

// ============================================================================
// ADDITIONAL CALLBACK TYPES (Examples of other common patterns)
// ============================================================================

/**
 * @brief Callback for error events (no return value needed)
 */
typedef void (*error_handler_cb_t)(int error_code, const char *error_message);

/**
 * @brief Callback for timer events (periodic execution)
 */
typedef void (*timer_callback_cb_t)(void);

/**
 * @brief Callback for configuration changes (validation required)
 * @return true if configuration is valid, false to reject
 */
typedef bool (*config_validator_cb_t)(const char *key, const char *value);

/**
 * @brief Callback for progress updates (with progress percentage)
 */
typedef void (*progress_callback_cb_t)(int percentage, const char *operation);

// ============================================================================
// REGISTRATION FUNCTION DECLARATIONS
// ============================================================================

/**
 * @brief Register a callback for data received events
 * @param callback Your callback function (or NULL to unregister)
 */
void register_data_callback(data_received_cb_t callback);

/**
 * @brief Register a callback for status change events
 * @param callback Your callback function (or NULL to unregister)
 */
void register_status_callback(status_changed_cb_t callback);

/**
 * @brief Register a callback for task processing
 * @param callback Your callback function (or NULL to unregister)
 */
void register_task_processor(task_processor_cb_t callback);

// ============================================================================
// UTILITY FUNCTION DECLARATIONS
// ============================================================================

/**
 * @brief Check if a callback is registered
 * @param callback_type Type of callback to check (0=data, 1=status, 2=task)
 * @return true if callback is registered
 */
bool is_callback_registered(int callback_type);

/**
 * @brief Unregister all callbacks
 */
void unregister_all_callbacks(void);

// ============================================================================
// EXAMPLE CALLBACK IMPLEMENTATIONS (For reference)
// ============================================================================

/**
 * @brief Example data handler implementation
 * This shows how a user would implement their callback
 */
void example_data_handler(const char *data, size_t length);

/**
 * @brief Example status handler implementation
 */
void example_status_handler(bool connected, int error_code);

/**
 * @brief Example task processor implementation
 */
bool example_task_processor(const char *task_data, char *result_buffer, size_t buffer_size);

// ============================================================================
// REAL ESP32 INTEGRATION MACROS (For your actual project)
// ============================================================================

/**
 * @brief Convenient macro to register all callbacks at once
 * @param data_cb Data callback function
 * @param status_cb Status callback function  
 * @param task_cb Task processor callback function
 */
#define REGISTER_ALL_CALLBACKS(data_cb, status_cb, task_cb) \
    do { \
        register_data_callback(data_cb); \
        register_status_callback(status_cb); \
        register_task_processor(task_cb); \
    } while(0)

/**
 * @brief Macro to create a simple data callback that just prints
 */
#define SIMPLE_DATA_CALLBACK(name) \
    void name(const char *data, size_t length) { \
        printf("[DATA] %s (%zu bytes)\n", data, length); \
    }

#endif // CALLBACK_EXAMPLE_H