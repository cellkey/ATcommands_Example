/**
 * @file modem_callback_controller.h
 * @brief ADD CONTROL to your existing modem callback system
 * 
 * This header adds control mechanisms to your Modem_Config_Handling system
 * without breaking existing functionality. You can drop this into your project.
 */

#ifndef MODEM_CALLBACK_CONTROLLER_H
#define MODEM_CALLBACK_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "Modem_Config_Handling.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONTROL STRUCTURES
// ============================================================================

/**
 * @brief Callback control configuration
 */
typedef struct {
    bool callbacks_enabled;         // Master enable/disable
    bool debug_mode;               // Detailed logging
    uint32_t max_calls_per_second; // Rate limiting
    uint32_t total_calls_made;     // Statistics
    uint32_t last_call_time;       // For rate limiting
    
    // Individual callback controls
    bool data_callback_enabled;
    bool status_callback_enabled;
    bool task_callback_enabled;
    
    // Error handling
    uint32_t callback_errors;
    uint32_t callback_timeouts;
    
} modem_callback_control_t;

/**
 * @brief Callback statistics
 */
typedef struct {
    uint32_t data_calls;
    uint32_t status_calls;
    uint32_t task_calls;
    uint32_t failed_calls;
    uint32_t total_data_bytes;
    uint32_t last_reset_time;
} modem_callback_stats_t;

// ============================================================================
// CONTROL FUNCTIONS - Add these to your project
// ============================================================================

/**
 * @brief Initialize callback controller
 */
void modem_callback_controller_init(void);

/**
 * @brief Enable/disable all callbacks
 */
void modem_callbacks_enable(bool enable);

/**
 * @brief Enable/disable individual callback types
 */
void modem_data_callback_enable(bool enable);
void modem_status_callback_enable(bool enable);
void modem_task_callback_enable(bool enable);

/**
 * @brief Enable debug mode for callbacks
 */
void modem_callback_debug_enable(bool enable);

/**
 * @brief Set rate limiting (max calls per second)
 */
void modem_callback_set_rate_limit(uint32_t max_calls_per_second);

/**
 * @brief Get callback statistics
 */
void modem_callback_get_stats(modem_callback_stats_t *stats);

/**
 * @brief Reset callback statistics
 */
void modem_callback_reset_stats(void);

/**
 * @brief Get control configuration
 */
void modem_callback_get_control(modem_callback_control_t *control);

// ============================================================================
// CONTROLLED CALLBACK WRAPPERS - Replace your current callback calls with these
// ============================================================================

/**
 * @brief Controlled data callback execution
 * Use this instead of direct data_callback() calls
 */
bool modem_call_data_callback_controlled(const char *data, size_t length);

/**
 * @brief Controlled status callback execution
 * Use this instead of direct status_callback() calls
 */
bool modem_call_status_callback_controlled(task_status_t status);

/**
 * @brief Controlled task processor execution
 * Use this instead of direct task_processor() calls
 */
bool modem_call_task_callback_controlled(const char *task_data, char *response, size_t response_size);

// ============================================================================
// ENHANCED REGISTRATION - Add validation and control
// ============================================================================

/**
 * @brief Enhanced callback registration with validation
 */
typedef enum {
    CALLBACK_REG_SUCCESS = 0,
    CALLBACK_REG_NULL_POINTER,
    CALLBACK_REG_ALREADY_REGISTERED,
    CALLBACK_REG_DISABLED,
    CALLBACK_REG_VALIDATION_FAILED
} callback_registration_result_t;

/**
 * @brief Register data callback with validation
 */
callback_registration_result_t modem_register_data_callback_enhanced(
    modem_data_received_cb_t callback, 
    const char *callback_name
);

/**
 * @brief Register status callback with validation
 */
callback_registration_result_t modem_register_status_callback_enhanced(
    modem_status_changed_cb_t callback, 
    const char *callback_name
);

/**
 * @brief Register task processor with validation
 */
callback_registration_result_t modem_register_task_processor_enhanced(
    modem_task_processor_cb_t callback, 
    const char *callback_name
);

// ============================================================================
// DEBUGGING AND MONITORING
// ============================================================================

/**
 * @brief Print callback system status
 */
void modem_callback_print_status(void);

/**
 * @brief Check if callback system is healthy
 */
bool modem_callback_system_healthy(void);

/**
 * @brief Get last error information
 */
const char* modem_callback_get_last_error(void);

// ============================================================================
// CONVENIENCE MACROS - For easy integration
// ============================================================================

/**
 * @brief Quick enable/disable macros
 */
#define MODEM_CALLBACKS_ON()  modem_callbacks_enable(true)
#define MODEM_CALLBACKS_OFF() modem_callbacks_enable(false)

/**
 * @brief Debug macros
 */
#define MODEM_CALLBACK_DEBUG_ON()  modem_callback_debug_enable(true)
#define MODEM_CALLBACK_DEBUG_OFF() modem_callback_debug_enable(false)

/**
 * @brief Safe callback execution macros
 */
#define SAFE_DATA_CALLBACK(data, len) \
    modem_call_data_callback_controlled(data, len)

#define SAFE_STATUS_CALLBACK(status) \
    modem_call_status_callback_controlled(status)

#define SAFE_TASK_CALLBACK(task, resp, size) \
    modem_call_task_callback_controlled(task, resp, size)

#ifdef __cplusplus
}
#endif

#endif // MODEM_CALLBACK_CONTROLLER_H

/*
 * INTEGRATION INSTRUCTIONS:
 * 
 * 1. Add this header to your project
 * 2. Include in your Modem_Config_Handling.c
 * 3. Replace direct callback calls with controlled versions:
 *    
 *    OLD: if (data_callback) data_callback(data, length);
 *    NEW: SAFE_DATA_CALLBACK(data, length);
 * 
 * 4. In your app_main(), initialize control:
 *    modem_callback_controller_init();
 *    MODEM_CALLBACKS_ON();
 *    MODEM_CALLBACK_DEBUG_ON();
 * 
 * 5. Now you have full control over your callback system!
 */