/**
 * @file callback_control_demo.c
 * @brief TAKE CONTROL - Interactive callback mechanism demonstration
 * 
 * This lets you experiment with and control every aspect of callbacks:
 * - Register different callbacks
 * - Trigger events manually
 * - See exactly what happens when
 * - Control the flow step by step
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ============================================================================
// STEP 1: DEFINE YOUR OWN CALLBACK TYPES (Full Control)
// ============================================================================

// Define different types of callbacks you want to control
typedef void (*simple_callback_t)(void);
typedef void (*data_callback_t)(const char *data);
typedef bool (*validator_callback_t)(int value);
typedef int (*processor_callback_t)(int input);

// ============================================================================
// STEP 2: CREATE CONTROLLABLE CALLBACK MANAGER
// ============================================================================

typedef struct {
    simple_callback_t simple_cb;
    data_callback_t data_cb;
    validator_callback_t validator_cb;
    processor_callback_t processor_cb;
    
    // Control flags
    bool callbacks_enabled;
    int call_count;
    bool debug_mode;
    
} callback_manager_t;

// Global manager instance (you control this)
static callback_manager_t g_manager = {0};

// ============================================================================
// STEP 3: REGISTRATION FUNCTIONS (You control when/how to register)
// ============================================================================

void register_simple_callback(simple_callback_t callback) {
    g_manager.simple_cb = callback;
    if (g_manager.debug_mode) {
        printf("🔧 [CONTROL] Simple callback registered: %p\n", (void*)callback);
    }
}

void register_data_callback(data_callback_t callback) {
    g_manager.data_cb = callback;
    if (g_manager.debug_mode) {
        printf("🔧 [CONTROL] Data callback registered: %p\n", (void*)callback);
    }
}

void register_validator_callback(validator_callback_t callback) {
    g_manager.validator_cb = callback;
    if (g_manager.debug_mode) {
        printf("🔧 [CONTROL] Validator callback registered: %p\n", (void*)callback);
    }
}

void register_processor_callback(processor_callback_t callback) {
    g_manager.processor_cb = callback;
    if (g_manager.debug_mode) {
        printf("🔧 [CONTROL] Processor callback registered: %p\n", (void*)callback);
    }
}

// ============================================================================
// STEP 4: CONTROL FUNCTIONS (YOU decide when to call callbacks)
// ============================================================================

void enable_callbacks(bool enable) {
    g_manager.callbacks_enabled = enable;
    printf("🎛️  [CONTROL] Callbacks %s\n", enable ? "ENABLED" : "DISABLED");
}

void enable_debug_mode(bool enable) {
    g_manager.debug_mode = enable;
    printf("🐛 [CONTROL] Debug mode %s\n", enable ? "ON" : "OFF");
}

void clear_all_callbacks(void) {
    g_manager.simple_cb = NULL;
    g_manager.data_cb = NULL;
    g_manager.validator_cb = NULL;
    g_manager.processor_cb = NULL;
    g_manager.call_count = 0;
    printf("🧹 [CONTROL] All callbacks cleared\n");
}

int get_callback_call_count(void) {
    return g_manager.call_count;
}

void reset_call_count(void) {
    g_manager.call_count = 0;
    printf("🔄 [CONTROL] Call count reset\n");
}

// ============================================================================
// STEP 5: TRIGGER FUNCTIONS (YOU control when events happen)
// ============================================================================

void trigger_simple_event(void) {
    printf("\n⚡ [TRIGGER] Simple event triggered\n");
    
    if (!g_manager.callbacks_enabled) {
        printf("❌ [CONTROL] Callbacks disabled - event ignored\n");
        return;
    }
    
    if (g_manager.simple_cb) {
        g_manager.call_count++;
        if (g_manager.debug_mode) {
            printf("📞 [CONTROL] Calling simple callback (call #%d)\n", g_manager.call_count);
        }
        g_manager.simple_cb();
    } else {
        printf("⚠️  [CONTROL] No simple callback registered\n");
    }
}

void trigger_data_event(const char *data) {
    printf("\n⚡ [TRIGGER] Data event triggered with: '%s'\n", data);
    
    if (!g_manager.callbacks_enabled) {
        printf("❌ [CONTROL] Callbacks disabled - event ignored\n");
        return;
    }
    
    if (g_manager.data_cb) {
        g_manager.call_count++;
        if (g_manager.debug_mode) {
            printf("📞 [CONTROL] Calling data callback (call #%d)\n", g_manager.call_count);
        }
        g_manager.data_cb(data);
    } else {
        printf("⚠️  [CONTROL] No data callback registered\n");
    }
}

bool trigger_validation_event(int value) {
    printf("\n⚡ [TRIGGER] Validation event triggered with: %d\n", value);
    
    if (!g_manager.callbacks_enabled) {
        printf("❌ [CONTROL] Callbacks disabled - validation failed\n");
        return false;
    }
    
    if (g_manager.validator_cb) {
        g_manager.call_count++;
        if (g_manager.debug_mode) {
            printf("📞 [CONTROL] Calling validator callback (call #%d)\n", g_manager.call_count);
        }
        bool result = g_manager.validator_cb(value);
        printf("✅ [CONTROL] Validation result: %s\n", result ? "PASSED" : "FAILED");
        return result;
    } else {
        printf("⚠️  [CONTROL] No validator callback registered - defaulting to true\n");
        return true;
    }
}

int trigger_processing_event(int input) {
    printf("\n⚡ [TRIGGER] Processing event triggered with: %d\n", input);
    
    if (!g_manager.callbacks_enabled) {
        printf("❌ [CONTROL] Callbacks disabled - returning input unchanged\n");
        return input;
    }
    
    if (g_manager.processor_cb) {
        g_manager.call_count++;
        if (g_manager.debug_mode) {
            printf("📞 [CONTROL] Calling processor callback (call #%d)\n", g_manager.call_count);
        }
        int result = g_manager.processor_cb(input);
        printf("🔄 [CONTROL] Processing result: %d → %d\n", input, result);
        return result;
    } else {
        printf("⚠️  [CONTROL] No processor callback registered - returning input unchanged\n");
        return input;
    }
}

// ============================================================================
// STEP 6: SAMPLE CALLBACK IMPLEMENTATIONS (You can experiment with these)
// ============================================================================

// Simple callbacks
void simple_callback_v1(void) {
    printf("🎯 [CALLBACK] Simple V1: Hello from callback!\n");
}

void simple_callback_v2(void) {
    printf("🎯 [CALLBACK] Simple V2: This is a different callback!\n");
}

// Data callbacks
void data_callback_v1(const char *data) {
    printf("🎯 [CALLBACK] Data V1: Received '%s' (length: %zu)\n", data, strlen(data));
}

void data_callback_v2(const char *data) {
    printf("🎯 [CALLBACK] Data V2: Processing '%s' -> %s\n", data, 
           strlen(data) > 5 ? "LONG" : "SHORT");
}

// Validator callbacks
bool validator_positive(int value) {
    bool valid = value > 0;
    printf("🎯 [CALLBACK] Validator: %d is %s\n", value, valid ? "POSITIVE" : "NOT POSITIVE");
    return valid;
}

bool validator_even(int value) {
    bool valid = (value % 2) == 0;
    printf("🎯 [CALLBACK] Validator: %d is %s\n", value, valid ? "EVEN" : "ODD");
    return valid;
}

// Processor callbacks
int processor_double(int input) {
    int result = input * 2;
    printf("🎯 [CALLBACK] Processor: Double %d = %d\n", input, result);
    return result;
}

int processor_square(int input) {
    int result = input * input;
    printf("🎯 [CALLBACK] Processor: Square %d = %d\n", input, result);
    return result;
}

// ============================================================================
// STEP 7: INTERACTIVE CONTROL INTERFACE
// ============================================================================

void show_status(void) {
    printf("\n📊 [STATUS] Current Callback Manager State:\n");
    printf("   Simple callback:    %s\n", g_manager.simple_cb ? "REGISTERED" : "NULL");
    printf("   Data callback:      %s\n", g_manager.data_cb ? "REGISTERED" : "NULL");
    printf("   Validator callback: %s\n", g_manager.validator_cb ? "REGISTERED" : "NULL");
    printf("   Processor callback: %s\n", g_manager.processor_cb ? "REGISTERED" : "NULL");
    printf("   Callbacks enabled:  %s\n", g_manager.callbacks_enabled ? "YES" : "NO");
    printf("   Debug mode:         %s\n", g_manager.debug_mode ? "ON" : "OFF");
    printf("   Total calls made:   %d\n", g_manager.call_count);
}

void demonstrate_control(void) {
    printf("=== CALLBACK MECHANISM CONTROL DEMONSTRATION ===\n");
    
    // Phase 1: Setup and control
    printf("\n🔧 Phase 1: Setup and Control\n");
    printf("----------------------------------------\n");
    enable_debug_mode(true);
    enable_callbacks(true);
    show_status();
    
    // Phase 2: Register different callbacks
    printf("\n🔧 Phase 2: Register Callbacks\n");
    printf("----------------------------------------\n");
    register_simple_callback(simple_callback_v1);
    register_data_callback(data_callback_v1);
    register_validator_callback(validator_positive);
    register_processor_callback(processor_double);
    show_status();
    
    // Phase 3: Trigger events
    printf("\n⚡ Phase 3: Trigger Events\n");
    printf("----------------------------------------\n");
    trigger_simple_event();
    trigger_data_event("Hello World");
    trigger_validation_event(5);
    trigger_validation_event(-3);
    trigger_processing_event(7);
    
    // Phase 4: Change callbacks and test
    printf("\n🔄 Phase 4: Change Callbacks\n");
    printf("----------------------------------------\n");
    register_simple_callback(simple_callback_v2);
    register_data_callback(data_callback_v2);
    register_validator_callback(validator_even);
    register_processor_callback(processor_square);
    
    trigger_simple_event();
    trigger_data_event("Test");
    trigger_validation_event(4);
    trigger_processing_event(3);
    
    // Phase 5: Control mechanisms
    printf("\n🎛️  Phase 5: Control Mechanisms\n");
    printf("----------------------------------------\n");
    printf("Disabling callbacks...\n");
    enable_callbacks(false);
    trigger_simple_event();
    trigger_data_event("This should be ignored");
    
    printf("\nRe-enabling callbacks...\n");
    enable_callbacks(true);
    trigger_simple_event();
    
    // Phase 6: Cleanup
    printf("\n🧹 Phase 6: Cleanup\n");
    printf("----------------------------------------\n");
    show_status();
    clear_all_callbacks();
    trigger_simple_event();  // Should show "no callback registered"
    show_status();
}

// ============================================================================
// STEP 8: YOUR ESP32 MODEM INTEGRATION EXAMPLE
// ============================================================================

void esp32_modem_control_example(void) {
    printf("\n=== ESP32 MODEM CONTROL EXAMPLE ===\n");
    printf("How to take control of YOUR modem callbacks:\n\n");
    
    printf("1. CREATE YOUR CALLBACK FUNCTIONS:\n");
    printf("   void my_data_handler(const char *data, size_t length) {\n");
    printf("       // YOUR control logic here\n");
    printf("   }\n\n");
    
    printf("2. REGISTER WITH CONTROL:\n");
    printf("   // Enable/disable as needed\n");
    printf("   modem_register_data_callback(my_data_handler);\n\n");
    
    printf("3. ADD CONTROL TO YOUR MODEM CODE:\n");
    printf("   // In your modem task, add control checks:\n");
    printf("   if (callbacks_enabled && data_callback) {\n");
    printf("       data_callback(received_data, length);\n");
    printf("   }\n\n");
    
    printf("4. CONTROL FROM YOUR APP:\n");
    printf("   // You can enable/disable callbacks\n");
    printf("   // You can change callbacks at runtime\n");
    printf("   // You can monitor callback activity\n");
}

// ============================================================================
// MAIN DEMONSTRATION
// ============================================================================

int main(void) {
    demonstrate_control();
    esp32_modem_control_example();
    
    printf("\n=== KEY CONTROL INSIGHTS ===\n");
    printf("✅ You can enable/disable callbacks at runtime\n");
    printf("✅ You can change which function is called\n");
    printf("✅ You can monitor callback activity\n");
    printf("✅ You can add validation and error handling\n");
    printf("✅ You control WHEN and HOW callbacks are triggered\n");
    printf("\n🎯 You now have FULL CONTROL over the callback mechanism!\n");
    
    return 0;
}

/*
 * COMPILATION AND USAGE:
 * gcc callback_control_demo.c -o control_demo
 * ./control_demo
 * 
 * This shows you how to take complete control over callback mechanisms
 * and adapt them for your specific needs!
 */