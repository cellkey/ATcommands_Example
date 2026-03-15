/**
 * @file modem_callback_controller.c
 * @brief IMPLEMENTATION - Complete control over your modem callbacks
 * 
 * This gives you FULL CONTROL over when, how, and if callbacks execute.
 * You can enable/disable, monitor, rate-limit, and debug all callback activity.
 */

#include "modem_callback_controller.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "CALLBACK_CTRL";

// ============================================================================
// INTERNAL STATE - YOU CONTROL THIS
// ============================================================================

static modem_callback_control_t g_control = {
    .callbacks_enabled = true,
    .debug_mode = false,
    .max_calls_per_second = 100,  // Default rate limit
    .total_calls_made = 0,
    .last_call_time = 0,
    .data_callback_enabled = true,
    .status_callback_enabled = true,
    .task_callback_enabled = true,
    .callback_errors = 0,
    .callback_timeouts = 0
};

static modem_callback_stats_t g_stats = {0};
static char g_last_error[128] = "";

// External callback storage (from your original code)
extern modem_data_received_cb_t data_callback;
extern modem_status_changed_cb_t status_callback;
extern modem_task_processor_cb_t task_processor;

// ============================================================================
// CONTROL FUNCTIONS - YOU CONTROL THE BEHAVIOR
// ============================================================================

void modem_callback_controller_init(void) {
    ESP_LOGI(TAG, "🎛️  Initializing callback controller");
    
    // Reset all statistics
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.last_reset_time = esp_timer_get_time() / 1000;
    
    // Initialize control state
    g_control.total_calls_made = 0;
    g_control.callback_errors = 0;
    g_control.callback_timeouts = 0;
    strcpy(g_last_error, "No errors");
    
    ESP_LOGI(TAG, "✅ Callback controller initialized");
}

void modem_callbacks_enable(bool enable) {
    g_control.callbacks_enabled = enable;
    ESP_LOGI(TAG, "🎛️  Callbacks %s", enable ? "ENABLED" : "DISABLED");
}

void modem_data_callback_enable(bool enable) {
    g_control.data_callback_enabled = enable;
    if (g_control.debug_mode) {
        ESP_LOGI(TAG, "🎛️  Data callbacks %s", enable ? "ENABLED" : "DISABLED");
    }
}

void modem_status_callback_enable(bool enable) {
    g_control.status_callback_enabled = enable;
    if (g_control.debug_mode) {
        ESP_LOGI(TAG, "🎛️  Status callbacks %s", enable ? "ENABLED" : "DISABLED");
    }
}

void modem_task_callback_enable(bool enable) {
    g_control.task_callback_enabled = enable;
    if (g_control.debug_mode) {
        ESP_LOGI(TAG, "🎛️  Task callbacks %s", enable ? "ENABLED" : "DISABLED");
    }
}

void modem_callback_debug_enable(bool enable) {
    g_control.debug_mode = enable;
    ESP_LOGI(TAG, "🐛 Debug mode %s", enable ? "ENABLED" : "DISABLED");
}

void modem_callback_set_rate_limit(uint32_t max_calls_per_second) {
    g_control.max_calls_per_second = max_calls_per_second;
    ESP_LOGI(TAG, "⏱️  Rate limit set to %lu calls/second", max_calls_per_second);
}

// ============================================================================
// RATE LIMITING - CONTROL HOW OFTEN CALLBACKS CAN RUN
// ============================================================================

static bool check_rate_limit(void) {
    if (g_control.max_calls_per_second == 0) {
        return true; // No limit
    }
    
    uint32_t current_time = esp_timer_get_time() / 1000; // Convert to ms
    uint32_t time_diff = current_time - g_control.last_call_time;
    
    // If enough time has passed, allow the call
    if (time_diff >= (1000 / g_control.max_calls_per_second)) {
        g_control.last_call_time = current_time;
        return true;
    }
    
    if (g_control.debug_mode) {
        ESP_LOGW(TAG, "⏱️  Rate limit exceeded, call blocked");
    }
    return false;
}

// ============================================================================
// CONTROLLED CALLBACK EXECUTION - THE HEART OF CONTROL
// ============================================================================

bool modem_call_data_callback_controlled(const char *data, size_t length) {
    // Master control check
    if (!g_control.callbacks_enabled) {
        if (g_control.debug_mode) {
            ESP_LOGD(TAG, "🚫 Data callback blocked - callbacks disabled");
        }
        return false;
    }
    
    // Individual callback control check
    if (!g_control.data_callback_enabled) {
        if (g_control.debug_mode) {
            ESP_LOGD(TAG, "🚫 Data callback blocked - data callbacks disabled");
        }
        return false;
    }
    
    // Rate limiting check
    if (!check_rate_limit()) {
        return false;
    }
    
    // Callback exists check
    if (!data_callback) {
        if (g_control.debug_mode) {
            ESP_LOGD(TAG, "⚠️  No data callback registered");
        }
        return false;
    }
    
    // Execute the callback with control
    if (g_control.debug_mode) {
        ESP_LOGI(TAG, "📞 Calling data callback with %zu bytes: '%.20s%s'", 
                 length, data, length > 20 ? "..." : "");
    }
    
    try {
        // Call the actual callback
        data_callback(data, length);
        
        // Update statistics
        g_control.total_calls_made++;
        g_stats.data_calls++;
        g_stats.total_data_bytes += length;
        
        if (g_control.debug_mode) {
            ESP_LOGI(TAG, "✅ Data callback completed successfully");
        }
        
        return true;
        
    } catch (...) {
        // Handle callback errors
        g_control.callback_errors++;
        g_stats.failed_calls++;
        snprintf(g_last_error, sizeof(g_last_error), "Data callback threw exception");
        ESP_LOGE(TAG, "❌ Data callback failed: %s", g_last_error);
        return false;
    }
}

bool modem_call_status_callback_controlled(task_status_t status) {
    // Control checks
    if (!g_control.callbacks_enabled || !g_control.status_callback_enabled) {
        if (g_control.debug_mode) {
            ESP_LOGD(TAG, "🚫 Status callback blocked");
        }
        return false;
    }
    
    if (!check_rate_limit()) {
        return false;
    }
    
    if (!status_callback) {
        if (g_control.debug_mode) {
            ESP_LOGD(TAG, "⚠️  No status callback registered");
        }
        return false;
    }
    
    // Execute with control
    if (g_control.debug_mode) {
        ESP_LOGI(TAG, "📞 Calling status callback with status: %d", status);
    }
    
    try {
        status_callback(status);
        
        // Update statistics
        g_control.total_calls_made++;
        g_stats.status_calls++;
        
        if (g_control.debug_mode) {
            ESP_LOGI(TAG, "✅ Status callback completed successfully");
        }
        
        return true;
        
    } catch (...) {
        g_control.callback_errors++;
        g_stats.failed_calls++;
        snprintf(g_last_error, sizeof(g_last_error), "Status callback threw exception");
        ESP_LOGE(TAG, "❌ Status callback failed: %s", g_last_error);
        return false;
    }
}

bool modem_call_task_callback_controlled(const char *task_data, char *response, size_t response_size) {
    // Control checks
    if (!g_control.callbacks_enabled || !g_control.task_callback_enabled) {
        if (g_control.debug_mode) {
            ESP_LOGD(TAG, "🚫 Task callback blocked");
        }
        return false;
    }
    
    if (!check_rate_limit()) {
        return false;
    }
    
    if (!task_processor) {
        if (g_control.debug_mode) {
            ESP_LOGD(TAG, "⚠️  No task processor registered");
        }
        return false;
    }
    
    // Execute with control
    if (g_control.debug_mode) {
        ESP_LOGI(TAG, "📞 Calling task processor with: '%s'", task_data);
    }
    
    try {
        bool result = task_processor(task_data, response, response_size);
        
        // Update statistics
        g_control.total_calls_made++;
        g_stats.task_calls++;
        
        if (g_control.debug_mode) {
            ESP_LOGI(TAG, "✅ Task callback completed: %s", result ? "SUCCESS" : "FAILED");
        }
        
        return result;
        
    } catch (...) {
        g_control.callback_errors++;
        g_stats.failed_calls++;
        snprintf(g_last_error, sizeof(g_last_error), "Task callback threw exception");
        ESP_LOGE(TAG, "❌ Task callback failed: %s", g_last_error);
        return false;
    }
}

// ============================================================================
// ENHANCED REGISTRATION - WITH VALIDATION AND CONTROL
// ============================================================================

callback_registration_result_t modem_register_data_callback_enhanced(
    modem_data_received_cb_t callback, 
    const char *callback_name) {
    
    if (!callback) {
        ESP_LOGW(TAG, "❌ Cannot register NULL data callback");
        return CALLBACK_REG_NULL_POINTER;
    }
    
    if (data_callback && callback != data_callback) {
        ESP_LOGW(TAG, "⚠️  Replacing existing data callback");
    }
    
    // Use original registration function
    modem_register_data_callback(callback);
    
    ESP_LOGI(TAG, "✅ Data callback '%s' registered successfully", 
             callback_name ? callback_name : "unnamed");
    
    return CALLBACK_REG_SUCCESS;
}

callback_registration_result_t modem_register_status_callback_enhanced(
    modem_status_changed_cb_t callback, 
    const char *callback_name) {
    
    if (!callback) {
        ESP_LOGW(TAG, "❌ Cannot register NULL status callback");
        return CALLBACK_REG_NULL_POINTER;
    }
    
    modem_register_status_callback(callback);
    
    ESP_LOGI(TAG, "✅ Status callback '%s' registered successfully", 
             callback_name ? callback_name : "unnamed");
    
    return CALLBACK_REG_SUCCESS;
}

callback_registration_result_t modem_register_task_processor_enhanced(
    modem_task_processor_cb_t callback, 
    const char *callback_name) {
    
    if (!callback) {
        ESP_LOGW(TAG, "❌ Cannot register NULL task processor");
        return CALLBACK_REG_NULL_POINTER;
    }
    
    modem_register_task_processor(callback);
    
    ESP_LOGI(TAG, "✅ Task processor '%s' registered successfully", 
             callback_name ? callback_name : "unnamed");
    
    return CALLBACK_REG_SUCCESS;
}

// ============================================================================
// MONITORING AND STATISTICS - KNOW WHAT'S HAPPENING
// ============================================================================

void modem_callback_get_stats(modem_callback_stats_t *stats) {
    if (stats) {
        *stats = g_stats;
    }
}

void modem_callback_reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.last_reset_time = esp_timer_get_time() / 1000;
    g_control.total_calls_made = 0;
    g_control.callback_errors = 0;
    ESP_LOGI(TAG, "📊 Statistics reset");
}

void modem_callback_get_control(modem_callback_control_t *control) {
    if (control) {
        *control = g_control;
    }
}

void modem_callback_print_status(void) {
    ESP_LOGI(TAG, "📊 CALLBACK SYSTEM STATUS:");
    ESP_LOGI(TAG, "   Master enabled:     %s", g_control.callbacks_enabled ? "YES" : "NO");
    ESP_LOGI(TAG, "   Data callbacks:     %s", g_control.data_callback_enabled ? "YES" : "NO");
    ESP_LOGI(TAG, "   Status callbacks:   %s", g_control.status_callback_enabled ? "YES" : "NO");
    ESP_LOGI(TAG, "   Task callbacks:     %s", g_control.task_callback_enabled ? "YES" : "NO");
    ESP_LOGI(TAG, "   Debug mode:         %s", g_control.debug_mode ? "ON" : "OFF");
    ESP_LOGI(TAG, "   Rate limit:         %lu calls/sec", g_control.max_calls_per_second);
    ESP_LOGI(TAG, "   Total calls:        %lu", g_control.total_calls_made);
    ESP_LOGI(TAG, "   Data calls:         %lu", g_stats.data_calls);
    ESP_LOGI(TAG, "   Status calls:       %lu", g_stats.status_calls);
    ESP_LOGI(TAG, "   Task calls:         %lu", g_stats.task_calls);
    ESP_LOGI(TAG, "   Failed calls:       %lu", g_stats.failed_calls);
    ESP_LOGI(TAG, "   Total data bytes:   %lu", g_stats.total_data_bytes);
    ESP_LOGI(TAG, "   Callback errors:    %lu", g_control.callback_errors);
}

bool modem_callback_system_healthy(void) {
    // Define health criteria
    if (g_control.callback_errors > 10) {
        return false;
    }
    
    if (g_stats.failed_calls > (g_control.total_calls_made / 2)) {
        return false; // More than 50% failure rate
    }
    
    return true;
}

const char* modem_callback_get_last_error(void) {
    return g_last_error;
}

/*
 * HOW TO INTEGRATE THIS INTO YOUR EXISTING CODE:
 * 
 * 1. In your Modem_Config_Handling.c, replace direct callback calls:
 * 
 *    OLD:
 *    if (data_callback) {
 *        data_callback(data, length);
 *    }
 * 
 *    NEW:
 *    SAFE_DATA_CALLBACK(data, length);
 * 
 * 2. In your app_main():
 *    modem_callback_controller_init();
 *    MODEM_CALLBACKS_ON();
 *    MODEM_CALLBACK_DEBUG_ON();
 * 
 * 3. You now have complete control!
 */