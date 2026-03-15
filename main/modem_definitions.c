/**
 * @file modem_definitions.c
 * @brief Implementation of modem-specific functionality-A7670E or LE910xx
 * #include "at_command_api.h"
 */

#include <string.h>
#include "esp_log.h"
#include "modem_definitions.h"
#include "at_command_api.h"

static const char *TAG = "MODEM_DEF";

// Current modem type (can be changed at runtime)
static modem_type_t current_modem_type = CURRENT_MODEM_TYPE;

/**
 * @brief Get AT commands for current modem type
 */
const modem_at_commands_t* get_modem_commands(void) {
    
    switch (current_modem_type) {
        case MODEM_TYPE_SIMCOM_A7670:
            return &simcom_a7670_commands;
            
        case MODEM_TYPE_TELIT_LE910:
            return &telit_le910_commands;
            
        default:
            ESP_LOGW(TAG, "Unknown modem type %d, defaulting to SIMCom A7670", current_modem_type);
            return &simcom_a7670_commands;
    }
}

/**
 * @brief Get capabilities for current modem type
 */
const modem_capabilities_t* get_modem_capabilities(void) {
    switch (current_modem_type) {
        case MODEM_TYPE_SIMCOM_A7670:
            return &simcom_a7670_caps;
            
        case MODEM_TYPE_TELIT_LE910:
            return &telit_le910_caps;
            
        default:
            ESP_LOGW(TAG, "Unknown modem type %d, defaulting to SIMCom capabilities", current_modem_type);
            return &simcom_a7670_caps;
    }
}

/**
 * @brief Get human-readable modem type name
 */
const char* get_modem_type_name(modem_type_t type) {
    switch (type) {
        case MODEM_TYPE_SIMCOM_A7670: return "SIMCom A7670";
        case MODEM_TYPE_TELIT_LE910: return "Telit LE910xxx";
        case MODEM_TYPE_QUECTEL_EC25: return "Quectel EC25";
        case MODEM_TYPE_UBLOX_SARA: return "u-blox SARA";
        case MODEM_TYPE_AUTO_DETECT: return "Auto-detect";
        default: return "Unknown";
    }
}

/**
 * @brief Set the current modem type
 */
void set_modem_type(modem_type_t type) {
    if (type < MODEM_TYPE_MAX) {
        current_modem_type = type;
        ESP_LOGI(TAG, "Modem type set to: %s", get_modem_type_name(type));
    } else {
        ESP_LOGE(TAG, "Invalid modem type: %d", type);
    }
}

/**
 * @brief Get the current modem type
 */
modem_type_t get_current_modem_type(void) {
    return current_modem_type;
}

/**
 * @brief Auto-detect modem type by querying manufacturer
 */
modem_type_t detect_modem_type(void) {
    ESP_LOGI(TAG, "Attempting to auto-detect modem type...");
    
    // Query manufacturer
    at_result_t result = send_at_command("AT+CGMI", "OK", 3000);
    if (result != AT_RESULT_SUCCESS) {
        ESP_LOGW(TAG, "Failed to query manufacturer, using default");
        return CURRENT_MODEM_TYPE;
    }
    
    // Check the response in last_response
    if (strstr(last_response, "SIMCOM") != NULL || strstr(last_response, "SIMCom") != NULL) {
        ESP_LOGI(TAG, "Detected SIMCom modem");
        return MODEM_TYPE_SIMCOM_A7670;
    }
    else if (strstr(last_response, "Telit") != NULL || strstr(last_response, "TELIT") != NULL) {
        ESP_LOGI(TAG, "Detected Telit modem");
        return MODEM_TYPE_TELIT_LE910;
    }
    else if (strstr(last_response, "Quectel") != NULL || strstr(last_response, "QUECTEL") != NULL) {
        ESP_LOGI(TAG, "Detected Quectel modem (using future support)");
        return MODEM_TYPE_QUECTEL_EC25;
    }
    else if (strstr(last_response, "u-blox") != NULL || strstr(last_response, "U-BLOX") != NULL) {
        ESP_LOGI(TAG, "Detected u-blox modem (using future support)");
        return MODEM_TYPE_UBLOX_SARA;
    }
    else {
        ESP_LOGW(TAG, "Unknown manufacturer in response: %s", last_response);
        ESP_LOGW(TAG, "Defaulting to SIMCom A7670 commands");
        return MODEM_TYPE_SIMCOM_A7670;
    }
}

/**
 * @brief Verify that the expected modem type matches actual hardware
 */
bool verify_modem_type(modem_type_t expected_type) {
    modem_type_t detected = detect_modem_type();
    
    if (detected == expected_type) {
        ESP_LOGI(TAG, "✓ Modem verification passed: %s", get_modem_type_name(expected_type));
        return true;
    } else {
        ESP_LOGW(TAG, "✗ Modem mismatch - Expected: %s, Detected: %s", 
                 get_modem_type_name(expected_type), get_modem_type_name(detected));
        return false;
    }
}

/**
 * @brief Initialize modem with auto-detection
 */
bool initialize_modem_with_detection(void) {
    ESP_LOGI(TAG, "Initializing modem with auto-detection...");
    
    // Try auto-detection
    modem_type_t detected_type = detect_modem_type();
    
    if (detected_type != MODEM_TYPE_MAX) {
        set_modem_type(detected_type);
        
        // Get the capabilities for this modem type
        const modem_capabilities_t *caps = get_modem_capabilities();
        
        ESP_LOGI(TAG, "✓ Modem initialized: %s", get_modem_type_name(detected_type));
        ESP_LOGI(TAG, "  TCP: %s, UDP: %s, SSL: %s, HTTP: %s", 
                 caps->supports_tcp ? "Yes" : "No",
                 caps->supports_udp ? "Yes" : "No", 
                 caps->supports_ssl ? "Yes" : "No",
                 caps->supports_http ? "Yes" : "No");
        ESP_LOGI(TAG, "  Max sockets: %d, GNSS: %s", 
                 caps->max_sockets,
                 caps->has_gnss ? "Yes" : "No");
        
        return true;
    } else {
        ESP_LOGE(TAG, "✗ Failed to detect modem type");
        return false;
    }
}

/**
 * @brief Get a formatted TCP connect command for current modem
 */
int get_tcp_connect_command(char *buffer, size_t buffer_size, const char *protocol, const char *server, int port) {
    const modem_at_commands_t *commands = get_modem_commands();
    
    return snprintf(buffer, buffer_size, commands->tcp_connect_cmd_format, protocol, server, port);
}

/**
 * @brief Get a formatted TCP send command for current modem
 */
int get_tcp_send_command(char *buffer, size_t buffer_size, int data_length) {
    const modem_at_commands_t *commands = get_modem_commands();
    
    return snprintf(buffer, buffer_size, commands->tcp_send_cmd_format, data_length);
}

/**
 * @brief Check if current modem supports a specific feature
 */
bool modem_supports_feature(const char *feature) {

    const modem_capabilities_t *caps = get_modem_capabilities();
    
    if (strcmp(feature, "tcp") == 0) return caps->supports_tcp;
    if (strcmp(feature, "udp") == 0) return caps->supports_udp;
    if (strcmp(feature, "ssl") == 0) return caps->supports_ssl;
    if (strcmp(feature, "http") == 0) return caps->supports_http;
    if (strcmp(feature, "ftp") == 0) return caps->supports_ftp;
    if (strcmp(feature, "mqtt") == 0) return caps->supports_mqtt;
    if (strcmp(feature, "gnss") == 0) return caps->has_gnss;
    
    ESP_LOGW(TAG, "Unknown feature query: %s", feature);
    return false;
}