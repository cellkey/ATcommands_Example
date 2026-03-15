/**
 * @file at_command_examples.c
 * @brief Implementation of AT command examples
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "at_command_examples.h"

/**
 * @brief Example: Demonstrate enhanced AT command control
 */
void example_enhanced_at_commands(void) {
    
    ESP_LOGI(AT_EXAMPLES_TAG, "=== Enhanced AT Command Control Examples ===");
    
    // Example 1: Standard command that waits for OK (default behavior)
    ESP_LOGI(AT_EXAMPLES_TAG, "1. Standard command with default OK waiting:");
    at_result_t result1 = send_at_command("AT", "OK", 2000);
    ESP_LOGI(AT_EXAMPLES_TAG, "Result: %s", (result1 == AT_RESULT_SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Example 2: Enhanced command that waits for OK explicitly
    ESP_LOGI(AT_EXAMPLES_TAG, "2. Enhanced command with explicit OK waiting:");
    at_result_t result2 = send_at_command_ex("AT+CSQ", "+CSQ:", 3000, true);
    ESP_LOGI(AT_EXAMPLES_TAG, "Result: %s", (result2 == AT_RESULT_SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Example 3: Enhanced command that DOESN'T wait for OK (just data response)
    ESP_LOGI(AT_EXAMPLES_TAG, "3. Enhanced command without OK waiting (data only):");
    at_result_t result3 = send_at_command_ex("AT+CSQ", "+CSQ:", 3000, false);
    ESP_LOGI(AT_EXAMPLES_TAG, "Result: %s", (result3 == AT_RESULT_SUCCESS) ? "SUCCESS" : "FAILED");
    
    // Example 4: Modem command that sends binary data - no OK expected
    ESP_LOGI(AT_EXAMPLES_TAG, "4. Binary data command (no OK expected):");
    at_result_t result4 = send_at_command_ex("AT+HTTPREAD", "HTTP/", 10000, false);
    ESP_LOGI(AT_EXAMPLES_TAG, "Result: %s", (result4 == AT_RESULT_SUCCESS) ? "SUCCESS" : "FAILED");
    
    ESP_LOGI(AT_EXAMPLES_TAG, "Enhanced AT command examples completed");
}

/**
 * @brief Example: Send a simple AT command and check result
 */
void example_simple_at_command(void) {
    ESP_LOGI(AT_EXAMPLES_TAG, "=== Simple AT Command Example ===");
    
    at_result_t result = send_at_command("AT", "OK", 2000);
    
    switch (result) {
        case AT_RESULT_SUCCESS:
            ESP_LOGI(AT_EXAMPLES_TAG, "Modem responded correctly!");
            break;
        case AT_RESULT_TIMEOUT:
            ESP_LOGE(AT_EXAMPLES_TAG, "Modem did not respond within timeout");
            break;
        case AT_RESULT_ERROR:
            ESP_LOGE(AT_EXAMPLES_TAG, "Modem returned an error");
            break;
        case AT_RESULT_UNEXPECTED_RESPONSE:
            ESP_LOGE(AT_EXAMPLES_TAG, "Modem response was unexpected");
            break;
    }
}

/**
 * @brief Example: Check network registration status
 */
void example_check_network_registration(void) {
    ESP_LOGI(AT_EXAMPLES_TAG, "=== Network Registration Check ===");
    
    at_result_t result = send_at_command("AT+CREG?", "+CREG:", 5000);
    
    if (result == AT_RESULT_SUCCESS) {
        ESP_LOGI(AT_EXAMPLES_TAG, "Network registration status retrieved");
        ESP_LOGI(AT_EXAMPLES_TAG, "Last response: %s", last_response);
        
        // Parse response to check registration status
        // +CREG: n,stat where stat: 0=not searching, 1=registered home, 2=searching, 3=denied, 5=registered roaming
        if (strstr(last_response, ",1") != NULL || strstr(last_response, ",5") != NULL) {
            ESP_LOGI(AT_EXAMPLES_TAG, "Modem is registered to network");
        } else {
            ESP_LOGW(AT_EXAMPLES_TAG, "Modem is not registered to network");
        }
    } else {
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to check network registration");
    }
}

/**
 * @brief Example: Check signal quality
 */
void example_check_signal_quality(void) {
    ESP_LOGI(AT_EXAMPLES_TAG, "=== Signal Quality Check ===");
    
    at_result_t result = send_at_command("AT+CSQ", "+CSQ:", 3000);
    
    if (result == AT_RESULT_SUCCESS) {
        ESP_LOGI(AT_EXAMPLES_TAG, "Signal quality response: %s", last_response);
        
        // Parse CSQ response: +CSQ: rssi,ber
        int rssi, ber;
        if (sscanf(last_response, "+CSQ: %d,%d", &rssi, &ber) == 2) {
            ESP_LOGI(AT_EXAMPLES_TAG, "RSSI: %d, BER: %d", rssi, ber);
            if (rssi == 99) {
                ESP_LOGW(AT_EXAMPLES_TAG, "Signal strength unknown");
            } else if (rssi < 10) {
                ESP_LOGW(AT_EXAMPLES_TAG, "Poor signal strength");
            } else if (rssi < 15) {
                ESP_LOGI(AT_EXAMPLES_TAG, "Fair signal strength");
            } else {
                ESP_LOGI(AT_EXAMPLES_TAG, "Good signal strength");
            }
        }
    } else {
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to get signal quality");
    }
}

/**
 * @brief Example: Send SMS message
 * @param phone_number Target phone number (e.g., "+1234567890")
 * @param message SMS text message
 */
void example_send_sms(const char *phone_number, const char *message) {
    ESP_LOGI(AT_EXAMPLES_TAG, "=== SMS Send Example ===");
    
    // Set SMS text mode
    at_result_t result = send_at_command("AT+CMGF=1", "OK", 2000);
    if (result != AT_RESULT_SUCCESS) {
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to set SMS text mode");
        return;
    }
    
    // Prepare SMS command
    char sms_cmd[64];
    snprintf(sms_cmd, sizeof(sms_cmd), "AT+CMGS=\"%s\"", phone_number);
    
    // Send SMS command (expect '>' prompt)
    result = send_at_command(sms_cmd, ">", 10000);
    if (result != AT_RESULT_SUCCESS) {
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to initiate SMS send");
        return;
    }
    
    // Send message content followed by Ctrl+Z (0x1A)
    // Note: This is simplified - real implementation would send message + 0x1A
    result = send_at_command("", "+CMGS:", 30000);  // Empty command, just wait for response
    
    if (result == AT_RESULT_SUCCESS) {
        ESP_LOGI(AT_EXAMPLES_TAG, "SMS sent successfully");
    } else {
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to send SMS");
    }
}

/**
 * @brief Example: Setup GSM network connection with APN
 * @param apn Access Point Name for your carrier
 * @param username Username for APN (can be NULL)
 * @param password Password for APN (can be NULL)
 */
void example_setup_gsm_network(const char *apn, const char *username, const char *password) {
    ESP_LOGI(AT_EXAMPLES_TAG, "=== GSM Network Setup ===");
    
    // Configure PDP context
    char pdp_cmd[128];
    snprintf(pdp_cmd, sizeof(pdp_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    
    at_result_t result = send_at_command(pdp_cmd, "OK", 5000);
    if (result != AT_RESULT_SUCCESS) {
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to configure PDP context");
        return;
    }
    
    // Set authentication if provided
    if (username && password) {
        char auth_cmd[128];
        snprintf(auth_cmd, sizeof(auth_cmd), "AT+CGAUTH=1,1,\"%s\",\"%s\"", username, password);
        result = send_at_command(auth_cmd, "OK", 5000);
        if (result != AT_RESULT_SUCCESS) {
            ESP_LOGW(AT_EXAMPLES_TAG, "Failed to set authentication (continuing anyway)");
        }
    }
    
    // Activate PDP context
    result = send_at_command("AT+CGACT=1,1", "OK", 30000);
    if (result == AT_RESULT_SUCCESS) {
        ESP_LOGI(AT_EXAMPLES_TAG, "PDP context activated successfully");
        
        // Get IP address
        result = send_at_command("AT+CGPADDR=1", "+CGPADDR:", 5000);
        if (result == AT_RESULT_SUCCESS) {
            ESP_LOGI(AT_EXAMPLES_TAG, "PDP context activated: %s", last_response);
        }
    } else {
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to activate PDP context");
    }
}

/**
 * @brief Example: HTTP GET request
 * @param url URL to fetch
 */
void example_http_get(const char *url) {
    ESP_LOGI(AT_EXAMPLES_TAG, "=== HTTP GET Example ===");
    
    // Initialize HTTP service
    at_result_t result = send_at_command("AT+HTTPINIT", "OK", 5000);
    if (result != AT_RESULT_SUCCESS) {
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to initialize HTTP service");
        return;
    }
    
    // Set HTTP parameters
    result = send_at_command("AT+HTTPPARA=\"CID\",1", "OK", 2000);
    if (result != AT_RESULT_SUCCESS) {
        send_at_command("AT+HTTPTERM", "OK", 2000);  // Cleanup
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to set HTTP CID parameter");
        return;
    }
    
    // Set URL
    char url_cmd[256];
    snprintf(url_cmd, sizeof(url_cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
    result = send_at_command(url_cmd, "OK", 5000);
    if (result != AT_RESULT_SUCCESS) {
        send_at_command("AT+HTTPTERM", "OK", 2000);  // Cleanup
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to set HTTP URL parameter");
        return;
    }
    
    // Start HTTP GET action
    result = send_at_command("AT+HTTPACTION=0", "OK", 2000);
    if (result != AT_RESULT_SUCCESS) {
        send_at_command("AT+HTTPTERM", "OK", 2000);  // Cleanup
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to start HTTP GET action");
        return;
    }
    
    // Wait for HTTP action to complete and read response
    // In real implementation, you'd wait for +HTTPACTION: response first
    result = send_at_command("AT+HTTPREAD", "+HTTPREAD:", 10000);
    if (result == AT_RESULT_SUCCESS) {
        ESP_LOGI(AT_EXAMPLES_TAG, "HTTP GET completed successfully");
        ESP_LOGI(AT_EXAMPLES_TAG, "Response data: %s", last_response);
    } else {
        ESP_LOGE(AT_EXAMPLES_TAG, "Failed to read HTTP response");
    }
    
    // Cleanup HTTP service
    send_at_command("AT+HTTPTERM", "OK", 2000);
}

/**
 * @brief Example: Complete modem functionality test
 */
void example_complete_modem_test(void) {

    ESP_LOGI(AT_EXAMPLES_TAG, "=== Complete Modem Test ===");
    
    // Basic AT command
    example_simple_at_command();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Check SIM card
    if (send_at_command("AT+CPIN?", "READY", 5000) == AT_RESULT_SUCCESS) {
        ESP_LOGI(AT_EXAMPLES_TAG, "SIM card is ready");
        
        // Check network registration
        example_check_network_registration();
        if (strstr(last_response, ",1") != NULL || strstr(last_response, ",5") != NULL) {
            ESP_LOGI(AT_EXAMPLES_TAG, "Modem is connected to network");
            
            // Check signal quality
            vTaskDelay(pdMS_TO_TICKS(1000));
            example_check_signal_quality();
            
            // Get modem information
            vTaskDelay(pdMS_TO_TICKS(1000));
            send_at_command("AT+CGMI", "OK", 3000);  // Manufacturer
            send_at_command("AT+CGMM", "OK", 3000);  // Model  
            send_at_command("AT+CGMR", "OK", 3000);  // Firmware version
            send_at_command("AT+CGSN", "OK", 3000);  // IMEI
            
            ESP_LOGI(AT_EXAMPLES_TAG, "Complete modem test finished successfully");
        }
    } else {
        ESP_LOGE(AT_EXAMPLES_TAG, "SIM card not ready or not inserted");
    }
}