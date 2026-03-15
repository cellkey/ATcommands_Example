/**
 * @file real_project_callback_usage.c
 * @brief REAL PROJECT USAGE - Where and when callbacks are triggered in YOUR AT Command project
 * 
 * This shows EXACTLY where in your AT Command project the line:
 * data_callback(received_data, data_length);
 * should be called with REAL data from the modem.
 */

#include "Modem_Config_Handling.h"
#include "at_command_api.h"
#include "esp_log.h"

static const char *TAG = "REAL_CALLBACK_USAGE";

// ============================================================================
// WHERE CALLBACKS ARE TRIGGERED IN YOUR REAL PROJECT
// ============================================================================

/**
 * @brief This is where your data callback gets triggered with REAL modem data
 * 
 * In your AT Command project, this happens in several places:
 */

// LOCATION 1: When AT commands receive data from server
void handle_at_command_data_response(const char *at_response) {
    ESP_LOGI(TAG, "📥 AT Command response received: %s", at_response);
    
    // Parse different types of AT responses that contain server data
    if (strstr(at_response, "+RECEIVE:")) {
        // Example: +RECEIVE:0,12:HELLO ESP32
        // Extract the actual data part
        char *data_start = strchr(at_response, ':');
        if (data_start) {
            data_start = strchr(data_start + 1, ':'); // Find second colon
            if (data_start) {
                data_start++; // Move past the colon
                size_t data_length = strlen(data_start);
                
                ESP_LOGI(TAG, "🎯 REAL DATA RECEIVED: '%s' (length: %zu)", data_start, data_length);
                
                // THIS IS WHERE YOUR CALLBACK GETS CALLED WITH REAL DATA!
                extern modem_data_received_cb_t data_callback;
                if (data_callback != NULL) {
                    ESP_LOGI(TAG, "📞 Calling user callback with real modem data...");
                    data_callback(data_start, data_length);  // <-- REAL CALLBACK TRIGGER!
                } else {
                    ESP_LOGW(TAG, "⚠️  No callback registered - real data ignored");
                }
            }
        }
    }
    
    // Handle HTTP response data
    else if (strstr(at_response, "+HTTPREAD:")) {
        // Example: +HTTPREAD:25,Server response data here
        char *comma = strchr(at_response, ',');
        if (comma) {
            comma++; // Move past comma to data
            size_t data_length = strlen(comma);
            
            ESP_LOGI(TAG, "🌐 HTTP DATA RECEIVED: '%s' (length: %zu)", comma, data_length);
            
            // REAL HTTP DATA CALLBACK TRIGGER!
            extern modem_data_received_cb_t data_callback;
            if (data_callback != NULL) {
                data_callback(comma, data_length);  // <-- REAL HTTP CALLBACK!
            }
        }
    }
}

// LOCATION 2: In your TCP socket data reception
void handle_tcp_socket_data(const char *socket_data, size_t length) {
    ESP_LOGI(TAG, "🔌 TCP Socket data received: '%.*s' (length: %zu)", 
             (int)length, socket_data, length);
    
    // THIS IS DIRECT TCP DATA FROM SERVER!
    extern modem_data_received_cb_t data_callback;
    if (data_callback != NULL) {
        ESP_LOGI(TAG, "📞 Calling user callback with TCP data...");
        data_callback(socket_data, length);  // <-- REAL TCP CALLBACK!
    }
}

// LOCATION 3: In your UART data reception (when parsing responses)
void parse_uart_response_for_server_data(const char *uart_line) {
    ESP_LOGI(TAG, "📟 UART line received: %s", uart_line);
    
    // Look for patterns that indicate server data
    if (strncmp(uart_line, "+IPD,", 5) == 0) {
        // ESP32 style: +IPD,12:Hello World
        char *colon = strchr(uart_line, ':');
        if (colon) {
            colon++; // Move past colon
            size_t data_length = strlen(colon);
            
            ESP_LOGI(TAG, "📡 Server data via +IPD: '%s' (length: %zu)", colon, data_length);
            
            // REAL SERVER DATA CALLBACK!
            extern modem_data_received_cb_t data_callback;
            if (data_callback != NULL) {
                data_callback(colon, data_length);  // <-- REAL SERVER DATA!
            }
        }
    }
}

// ============================================================================
// WHERE TO ADD THESE IN YOUR EXISTING PROJECT
// ============================================================================

/**
 * @brief Integration points in your existing AT Command project
 */
void show_integration_points(void) {
    ESP_LOGI(TAG, "📋 INTEGRATION POINTS IN YOUR AT COMMAND PROJECT:");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "1. IN enhanced_freertos_uart_at_commands.c:");
    ESP_LOGI(TAG, "   - In uart_rx_task() when processing received lines");
    ESP_LOGI(TAG, "   - Add: parse_uart_response_for_server_data(line);");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "2. IN at_command_api.c:");
    ESP_LOGI(TAG, "   - In send_at_command_ex() when processing responses");
    ESP_LOGI(TAG, "   - Add: handle_at_command_data_response(response);");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "3. IN Modem_Config_Handling.c:");
    ESP_LOGI(TAG, "   - In execute_communication_cycle() after receiving data");
    ESP_LOGI(TAG, "   - In modem_send_tcp_data() when getting responses");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "4. IN tcp_task_management.c:");
    ESP_LOGI(TAG, "   - In any TCP data reception functions");
    ESP_LOGI(TAG, "   - Add: handle_tcp_socket_data(data, length);");
}

// ============================================================================
// EXAMPLE: MODIFY YOUR EXISTING UART PROCESSING
// ============================================================================

/**
 * @brief Example modification to your existing UART processing
 * 
 * Add this to your uart_rx_task() in enhanced_freertos_uart_at_commands.c
 */
void enhanced_uart_processing_example(void) {
    ESP_LOGI(TAG, "📝 EXAMPLE: Enhanced UART processing with callbacks");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "In your uart_rx_task(), ADD this after reading a line:");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "// Original line processing");
    ESP_LOGI(TAG, "ESP_LOGI(TAG, \"RX: %%s\", line);");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "// NEW: Check for server data and trigger callback");
    ESP_LOGI(TAG, "parse_uart_response_for_server_data(line);");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "This will automatically call your registered callback");
    ESP_LOGI(TAG, "whenever the modem receives data from the server!");
}

// ============================================================================
// REAL EXAMPLE WITH YOUR CURRENT CODE STRUCTURE
// ============================================================================

/**
 * @brief Example using your current AT command structure
 */
void demonstrate_with_your_at_commands(void) {
    ESP_LOGI(TAG, "🎯 DEMONSTRATION: Using your existing AT command functions");
    
    // Simulate what happens when you send an AT command and get server data back
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "1. Your code sends: AT+CIPSEND=12");
    ESP_LOGI(TAG, "2. Your code sends: Hello Server");
    ESP_LOGI(TAG, "3. Modem responds: SEND OK");
    ESP_LOGI(TAG, "4. Server responds: +IPD,15:Server response");
    ESP_LOGI(TAG, "");
    
    // This is where your callback gets triggered with REAL data:
    const char *server_response = "Server response";
    size_t response_length = 15;
    
    ESP_LOGI(TAG, "5. 🎯 CALLBACK TRIGGERED with real server data:");
    ESP_LOGI(TAG, "   data_callback(\"%s\", %zu);", server_response, response_length);
    
    // Your registered callback function will be called here!
    extern modem_data_received_cb_t data_callback;
    if (data_callback != NULL) {
        ESP_LOGI(TAG, "6. 📞 Your application callback function executes!");
        data_callback(server_response, response_length);
    }
}

// ============================================================================
// SUMMARY: WHEN CALLBACKS HAPPEN IN YOUR PROJECT
// ============================================================================

void callback_timing_summary(void) {
    ESP_LOGI(TAG, "⏰ WHEN CALLBACKS HAPPEN IN YOUR AT COMMAND PROJECT:");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📡 DATA CALLBACK triggers when:");
    ESP_LOGI(TAG, "   • Server sends data via TCP/HTTP");
    ESP_LOGI(TAG, "   • AT commands return server responses");
    ESP_LOGI(TAG, "   • +IPD messages arrive from modem");
    ESP_LOGI(TAG, "   • HTTP responses contain data");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📊 STATUS CALLBACK triggers when:");
    ESP_LOGI(TAG, "   • Modem connects/disconnects");
    ESP_LOGI(TAG, "   • AT commands succeed/fail");
    ESP_LOGI(TAG, "   • Network status changes");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "⚙️  TASK CALLBACK triggers when:");
    ESP_LOGI(TAG, "   • Server sends task requests");
    ESP_LOGI(TAG, "   • Periodic task processing occurs");
    ESP_LOGI(TAG, "   • Configuration changes needed");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🎯 The callback fills parameters with REAL modem data!");
}

/*
 * ANSWER TO YOUR QUESTION:
 * 
 * YES! The line:
 * data_callback(received_data, data_length);
 * 
 * Gets called with REAL data received by the modem when:
 * 1. TCP/HTTP server sends data to your ESP32
 * 2. AT commands return responses with server data
 * 3. Modem receives +IPD or similar data indication messages
 * 4. Any network communication brings in data from external sources
 * 
 * The parameters (received_data, data_length) contain the ACTUAL data
 * that came from the server/network, not simulated data!
 * 
 * You need to add the callback trigger points to your existing UART
 * processing and AT command handling code to make this happen automatically.
 */