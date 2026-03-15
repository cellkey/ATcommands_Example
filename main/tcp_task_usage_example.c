/**
 * @file tcp_task_usage_example.c
 * @brief Example usage of the TCP/IP task management framework
 * 
 * This demonstrates simpler TCP/IP communication compared to HTTP mode
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "at_command_api.h"

static const char *TAG = "TCP_EXAMPLE";

// Forward declarations for TCP functions (would be in header)
extern int setup_tcp_networking(void);
extern int open_tcp_connection(void);
extern int send_tcp_data(const char *data);
extern int close_tcp_connection(void);
extern void configure_tcp_connection(const char *server_ip, int port, const char *protocol);

/**
 * @brief Example 1: Basic TCP connection test
 */
void example_basic_tcp_connection(void) {
    ESP_LOGI(TAG, "=== Example 1: Basic TCP Connection Test ===");
    
    // Configure connection
    configure_tcp_connection("your-server.com", 8080, "TCP");
    
    // Setup networking
    ESP_LOGI(TAG, "Setting up TCP networking...");
    if (setup_tcp_networking() < 0) {
        ESP_LOGE(TAG, "✗ Networking setup failed");
        return;
    }
    ESP_LOGI(TAG, "✓ Networking ready");
    
    // Open connection
    ESP_LOGI(TAG, "Opening TCP connection...");
    if (open_tcp_connection() < 0) {
        ESP_LOGE(TAG, "✗ Connection failed");
        return;
    }
    ESP_LOGI(TAG, "✓ Connected to server");
    
    // Send test data
    ESP_LOGI(TAG, "Sending test data...");
    if (send_tcp_data("Hello from ESP32!") >= 0) {
        ESP_LOGI(TAG, "✓ Data sent successfully");
    } else {
        ESP_LOGW(TAG, "✗ Data send failed");
    }
    
    // Wait a bit (in real app, you'd process incoming data here)
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Close connection
    ESP_LOGI(TAG, "Closing connection...");
    close_tcp_connection();
    ESP_LOGI(TAG, "✓ Connection closed");
}

/**
 * @brief Example 2: Task communication protocol
 */
void example_tcp_task_protocol(void) {
    ESP_LOGI(TAG, "=== Example 2: TCP Task Communication Protocol ===");
    
    // This example shows a simple request-response protocol
    configure_tcp_connection("your-server.com", 8080, "TCP");
    
    if (setup_tcp_networking() >= 0 && open_tcp_connection() >= 0) {
        // Step 1: Request tasks from server
        ESP_LOGI(TAG, "Requesting tasks from server...");
        send_tcp_data("GET_TASKS:ESP32:001");
        
        // In real implementation, you'd wait for +IPD response here
        ESP_LOGI(TAG, "Waiting for task data... (simulated)");
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // Step 2: Process received tasks (simulated)
        const char *simulated_tasks[] = {
            "LED_CONTROL:ON",
            "SENSOR_READ:TEMP",
            "STATUS_REPORT:ALL"
        };
        
        for (int i = 0; i < 3; i++) {
            ESP_LOGI(TAG, "Processing task: %s", simulated_tasks[i]);
            
            // Simulate task execution
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // Send completion response
            char response[128];
            snprintf(response, sizeof(response), "TASK_DONE:ESP32:%s:SUCCESS", simulated_tasks[i]);
            send_tcp_data(response);
            
            ESP_LOGI(TAG, "Task completed and reported");
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        
        // Step 3: Send final status
        send_tcp_data("STATUS:ESP32:ALL_TASKS_COMPLETE");
        
        close_tcp_connection();
    }
}

/**
 * @brief Example 3: Persistent connection with heartbeat
 */
void example_tcp_persistent_connection(void) {
    ESP_LOGI(TAG, "=== Example 3: Persistent TCP Connection ===");
    
    configure_tcp_connection("your-server.com", 8080, "TCP");
    
    if (setup_tcp_networking() >= 0 && open_tcp_connection() >= 0) {
        ESP_LOGI(TAG, "✓ Persistent connection established");
        
        // Simulate persistent connection with periodic communication
        for (int cycle = 1; cycle <= 5; cycle++) {
            ESP_LOGI(TAG, "--- Connection Cycle %d ---", cycle);
            
            // Send heartbeat
            char heartbeat[64];
            snprintf(heartbeat, sizeof(heartbeat), "HEARTBEAT:ESP32:%d", cycle);
            
            if (send_tcp_data(heartbeat) >= 0) {
                ESP_LOGI(TAG, "✓ Heartbeat sent");
                
                // Wait for server response (simulated)
                ESP_LOGI(TAG, "Waiting for server response...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                // Send some data
                char data[128];
                snprintf(data, sizeof(data), "DATA:ESP32:TEMP=25.5,HUMIDITY=60.2,CYCLE=%d", cycle);
                
                if (send_tcp_data(data) >= 0) {
                    ESP_LOGI(TAG, "✓ Data transmitted");
                } else {
                    ESP_LOGW(TAG, "✗ Data transmission failed");
                    break;  // Exit on communication failure
                }
                
            } else {
                ESP_LOGW(TAG, "✗ Heartbeat failed - connection may be lost");
                break;
            }
            
            // Wait before next cycle
            ESP_LOGI(TAG, "Waiting %d seconds before next cycle...", 15);
            vTaskDelay(pdMS_TO_TICKS(15000));
        }
        
        ESP_LOGI(TAG, "Closing persistent connection...");
        close_tcp_connection();
    }
}

/**
 * @brief Example 4: TCP vs HTTP comparison
 */
void example_tcp_vs_http_comparison(void) {
    ESP_LOGI(TAG, "=== Example 4: TCP vs HTTP Comparison ===");
    
    ESP_LOGI(TAG, "TCP/IP Mode Advantages:");
    ESP_LOGI(TAG, "  ✓ Simpler command sequence");
    ESP_LOGI(TAG, "  ✓ Lower overhead (no HTTP headers)");
    ESP_LOGI(TAG, "  ✓ Persistent connections");
    ESP_LOGI(TAG, "  ✓ Real-time bidirectional communication");
    ESP_LOGI(TAG, "  ✓ Custom protocol design");
    
    ESP_LOGI(TAG, "HTTP Mode Advantages:");
    ESP_LOGI(TAG, "  ✓ Standard protocol (REST APIs)");
    ESP_LOGI(TAG, "  ✓ Better for web integration");
    ESP_LOGI(TAG, "  ✓ Built-in status codes");
    ESP_LOGI(TAG, "  ✓ Easier debugging with web tools");
    
    ESP_LOGI(TAG, "Choose TCP/IP when:");
    ESP_LOGI(TAG, "  • You need real-time communication");
    ESP_LOGI(TAG, "  • You want minimal overhead");
    ESP_LOGI(TAG, "  • You control both client and server");
    ESP_LOGI(TAG, "  • You need custom protocols");
    
    ESP_LOGI(TAG, "Choose HTTP when:");
    ESP_LOGI(TAG, "  • Integrating with web services");
    ESP_LOGI(TAG, "  • Using existing REST APIs");
    ESP_LOGI(TAG, "  • Need standard web protocols");
    ESP_LOGI(TAG, "  • Working with third-party services");
}

/**
 * @brief Demonstrate incoming data handling (conceptual)
 */
void example_tcp_incoming_data_handling(void) {
    ESP_LOGI(TAG, "=== Example 5: Incoming Data Handling (Conceptual) ===");
    
    ESP_LOGI(TAG, "In a real implementation, you would:");
    ESP_LOGI(TAG, "1. Monitor for +IPD responses in your RX handler");
    ESP_LOGI(TAG, "2. Parse the data length and content");
    ESP_LOGI(TAG, "3. Queue incoming messages for processing");
    ESP_LOGI(TAG, "4. Respond appropriately based on message type");
    
    ESP_LOGI(TAG, "Example incoming data formats:");
    ESP_LOGI(TAG, "  +IPD,25:GET_TASKS:ESP32:URGENT");
    ESP_LOGI(TAG, "  +IPD,18:LED_CONTROL:ON:5000");
    ESP_LOGI(TAG, "  +IPD,15:SENSOR_READ:ALL");
    ESP_LOGI(TAG, "  +IPD,12:HEARTBEAT_REQ");
    
    ESP_LOGI(TAG, "You would modify process_line() function to:");
    ESP_LOGI(TAG, "  • Detect +IPD messages");
    ESP_LOGI(TAG, "  • Extract data payload");
    ESP_LOGI(TAG, "  • Queue for task processing");
    ESP_LOGI(TAG, "  • Generate appropriate responses");
}

/**
 * @brief Main function to run all TCP examples
 */
void start_tcp_examples(void) {
    ESP_LOGI(TAG, "Starting TCP/IP Task Management Examples");
    
    // Wait for system to initialize
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Run examples in sequence
    example_basic_tcp_connection();
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    example_tcp_task_protocol();
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    example_tcp_vs_http_comparison();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    example_tcp_incoming_data_handling();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Note: Persistent connection example commented out as it's long-running
    // example_tcp_persistent_connection();
    
    ESP_LOGI(TAG, "All TCP examples completed");
}