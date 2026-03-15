#!/usr/bin/env python3
"""
callback_demo.py - Callback Mechanism Demonstration
Shows the complete flow from declaration to usage in Python
(Same concepts apply to C, just easier to run)
"""

from typing import Callable, Optional
import time

# ============================================================================
# STEP 1: TYPE DEFINITIONS (Similar to typedef in C)
# ============================================================================

# Python equivalent of: typedef void (*data_received_cb_t)(const char *data, size_t length);
DataReceivedCallback = Callable[[str, int], None]

# Python equivalent of: typedef void (*status_changed_cb_t)(bool connected, int error_code);
StatusChangedCallback = Callable[[bool, int], None]

# Python equivalent of: typedef bool (*task_processor_cb_t)(const char *task_data, char *result_buffer, size_t buffer_size);
TaskProcessorCallback = Callable[[str], tuple[bool, str]]

# ============================================================================
# STEP 2: CALLBACK STORAGE (Static variables in C)
# ============================================================================

class CallbackManager:
    def __init__(self):
        # Storage for registered callbacks (initially None)
        self.data_callback: Optional[DataReceivedCallback] = None
        self.status_callback: Optional[StatusChangedCallback] = None
        self.task_callback: Optional[TaskProcessorCallback] = None
        
        # Simulated system state
        self.system_connected = False
        self.received_buffer = ""

    # ========================================================================
    # STEP 3: REGISTRATION FUNCTIONS (Public API)
    # ========================================================================

    def register_data_callback(self, callback: Optional[DataReceivedCallback]):
        """Register a callback for data received events"""
        self.data_callback = callback
        if callback:
            print("✓ Data callback registered")
        else:
            print("✗ Data callback unregistered")

    def register_status_callback(self, callback: Optional[StatusChangedCallback]):
        """Register a callback for status change events"""
        self.status_callback = callback
        if callback:
            print("✓ Status callback registered")
        else:
            print("✗ Status callback unregistered")

    def register_task_processor(self, callback: Optional[TaskProcessorCallback]):
        """Register a callback for task processing"""
        self.task_callback = callback
        if callback:
            print("✓ Task processor callback registered")
        else:
            print("✗ Task processor callback unregistered")

    # ========================================================================
    # STEP 4: INTERNAL FUNCTIONS THAT USE CALLBACKS
    # ========================================================================

    def simulate_connection_change(self, connected: bool, error_code: int):
        """Simulate connection status change"""
        self.system_connected = connected
        
        # Call user's callback if registered
        if self.status_callback:
            print("📞 Calling status callback...")
            self.status_callback(connected, error_code)
        else:
            print("⚠️  No status callback registered")

    def simulate_data_received(self, data: str):
        """Simulate data reception"""
        length = len(data)
        self.received_buffer = data
        
        # Call user's callback if registered
        if self.data_callback:
            print("📞 Calling data callback...")
            self.data_callback(data, length)
        else:
            print("⚠️  No data callback registered")

    def simulate_task_processing(self, task_data: str):
        """Simulate task processing"""
        # Call user's callback if registered
        if self.task_callback:
            print("📞 Calling task processor callback...")
            
            success, result = self.task_callback(task_data)
            if success:
                print(f"✅ Task processed successfully: {result}")
            else:
                print("❌ Task processing failed")
        else:
            print("⚠️  No task processor callback registered")

# ============================================================================
# STEP 5: USER APPLICATION CALLBACK FUNCTIONS
# ============================================================================

def my_data_handler(data: str, length: int):
    """User's data received handler - This function will be called when data arrives"""
    print(f"🎯 MY APP: Received {length} bytes: '{data}'")
    
    # User can do whatever they want here:
    if "HELLO" in data:
        print("🎯 MY APP: Received greeting!")
    if "ERROR" in data:
        print("🎯 MY APP: Received error message!")

def my_status_handler(connected: bool, error_code: int):
    """User's status change handler - Called when connection status changes"""
    if connected:
        print("🎯 MY APP: 🟢 Connected successfully!")
    else:
        print(f"🎯 MY APP: 🔴 Disconnected (error: {error_code})")

def my_task_processor(task_data: str) -> tuple[bool, str]:
    """User's task processor - Called when tasks need processing"""
    print(f"🎯 MY APP: Processing task: '{task_data}'")
    
    # Process the task (example logic)
    if task_data == "GET_TEMPERATURE":
        return True, "TEMPERATURE: 25.3°C"
    elif task_data == "GET_BATTERY":
        return True, "BATTERY: 85%"
    else:
        return False, "UNKNOWN_TASK"

# ============================================================================
# STEP 6: DEMONSTRATION - REAL USAGE EXAMPLE
# ============================================================================

def main():
    print("=== CALLBACK MECHANISM DEMONSTRATION ===\n")

    # Create callback manager (similar to your modem system)
    manager = CallbackManager()

    # Phase 1: Register callbacks
    print("📋 Phase 1: Registering Callbacks")
    print("----------------------------------------")
    manager.register_data_callback(my_data_handler)
    manager.register_status_callback(my_status_handler)
    manager.register_task_processor(my_task_processor)
    print()

    # Phase 2: Simulate system events
    print("📋 Phase 2: Simulating System Events")
    print("----------------------------------------")
    
    # Simulate connection
    print("🔌 Connecting to server...")
    manager.simulate_connection_change(True, 0)
    print()
    
    # Simulate data reception
    print("📥 Receiving data...")
    manager.simulate_data_received("HELLO ESP32!")
    manager.simulate_data_received("SENSOR_DATA: 25.3")
    manager.simulate_data_received("ERROR: Timeout")
    print()
    
    # Simulate task processing
    print("⚙️  Processing tasks...")
    manager.simulate_task_processing("GET_TEMPERATURE")
    manager.simulate_task_processing("GET_BATTERY")
    manager.simulate_task_processing("UNKNOWN_COMMAND")
    print()
    
    # Simulate disconnection
    print("🔌 Disconnecting...")
    manager.simulate_connection_change(False, 123)
    print()

    # Phase 3: Demonstrate what happens without callbacks
    print("📋 Phase 3: Clearing Callbacks (Show what happens without them)")
    print("----------------------------------------------------------------")
    manager.register_data_callback(None)  # Clear callback
    print("📥 Trying to receive data without callback...")
    manager.simulate_data_received("This won't trigger callback")
    print()

    print("=== DEMONSTRATION COMPLETE ===")

# ============================================================================
# STEP 7: REAL-WORLD ESP32 EXAMPLE (Pseudocode for your actual usage)
# ============================================================================

def esp32_modem_example_pseudocode():
    """
    Example of how to use this in your ESP32 modem application
    (This is pseudocode - shows the pattern for C implementation)
    """
    print("\n=== ESP32 MODEM USAGE PATTERN ===")
    print("In your app_main() or initialization function:")
    print()
    print("1. Register your callbacks:")
    print("   modem_register_data_callback(my_data_handler);")
    print("   modem_register_status_callback(my_status_handler);")
    print("   modem_register_task_processor(my_task_processor);")
    print()
    print("2. Initialize modem system:")
    print("   modem_handler_init(NULL);")
    print("   modem_handler_start();")
    print()
    print("3. The modem system will now call your functions automatically:")
    print("   - When TCP data arrives → my_data_handler() called")
    print("   - When connection changes → my_status_handler() called")
    print("   - When tasks need processing → my_task_processor() called")
    print()
    print("4. Your app just waits or does other work:")
    print("   while (1) {")
    print("       vTaskDelay(pdMS_TO_TICKS(1000));")
    print("       // Your callbacks handle all the events!")
    print("   }")

if __name__ == "__main__":
    main()
    esp32_modem_example_pseudocode()