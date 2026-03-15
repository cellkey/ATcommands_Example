/**
 * @file tcp_task_management.c
 * @brief TCP/IP task management framework for AT command system
 * 
 * This provides a simpler alternative to HTTP mode for direct TCP/IP
 * communication with your server. Supports multiple modem types.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "at_command_api.h"
#include "modem_definitions.h"

static const char *TAG = "TCP_MGMT";

// TCP connection configuration
typedef struct {
    char server_ip[64];
    int server_port;
    char protocol[8];           // "TCP" or "UDP"
    int connection_timeout;
    int keepalive_interval;
    bool auto_reconnect;
} tcp_config_t;

// Global TCP configuration
static tcp_config_t tcp_config = {
    .server_ip = "your-server.com",
    .server_port = 8080,
    .protocol = "TCP",
    .connection_timeout = 30000,
    .keepalive_interval = 60000,
    .auto_reconnect = true
};

// TCP connection state
typedef enum {
    TCP_STATE_DISCONNECTED = 0,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED,
    TCP_STATE_SENDING,
    TCP_STATE_RECEIVING,
    TCP_STATE_ERROR
} tcp_state_t;

static tcp_state_t current_tcp_state = TCP_STATE_DISCONNECTED;

/**
 * @brief Setup TCP/IP networking (PDP context) - modem agnostic
 */
static int setup_tcp_networking(void) {
    ESP_LOGI(TAG, "Setting up TCP/IP networking for %s...", get_modem_type_name(detect_modem_type()));
    
    const modem_at_commands_t *cmd = get_modem_commands();
    
    static at_command_def_t tcp_network_commands[8];  // Dynamic array
    int cmd_count = 0;
    
    // Configure PDP context (standard across modems)
    tcp_network_commands[cmd_count++] = (at_command_def_t){
        cmd->pdp_context_cmd, "OK", 5000, true, true, "Configure PDP context", "Check APN settings"
    };
    
    // Activate PDP context (modem-specific)
    tcp_network_commands[cmd_count++] = (at_command_def_t){
        cmd->pdp_activate_cmd, "OK", 30000, true, true, "Activate PDP context", "Check network registration"
    };
    
    // Enable network registration notifications (optional, standard)
    tcp_network_commands[cmd_count++] = (at_command_def_t){
        "AT+CREG=2", "OK", 2000, true, false, "Enable network notifications", "Optional feature"
    };
    
    // Modem-specific configuration
    if (detect_modem_type() == MODEM_TYPE_SIMCOM_A7670) {
        // SIMCom specific settings
        tcp_network_commands[cmd_count++] = (at_command_def_t){
            "AT+CIPMODE=0", "OK", 2000, true, false, "Set normal TCP mode", "Use default if failed"
        };
        tcp_network_commands[cmd_count++] = (at_command_def_t){
            "AT+CIPHEAD=1", "OK", 2000, true, false, "Enable IP header info", "Optional feature"
        };
        tcp_network_commands[cmd_count++] = (at_command_def_t){
            "AT+CIPCCFG=3,2,0,1", "OK", 2000, true, false, "Configure TCP parameters", "Use defaults if failed"
        };
    } 
    else if (detect_modem_type() == MODEM_TYPE_TELIT_LE910) {
        // Telit specific settings
        tcp_network_commands[cmd_count++] = (at_command_def_t){
            "AT#SCFG=1,1,300,90,600,50", "OK", 2000, true, false, "Configure socket parameters", "Use defaults if failed"
        };
        tcp_network_commands[cmd_count++] = (at_command_def_t){
            "AT#SKTCT=1", "OK", 2000, true, false, "Set socket context", "Optional setting"
        };
    }
    
    int result = execute_command_sequence(tcp_network_commands, cmd_count, "TCP_NETWORKING");
    
    if (result >= 0) {
        ESP_LOGI(TAG, "✓ TCP networking setup completed for %s", get_modem_type_name(detect_modem_type()));
    } else {
        ESP_LOGE(TAG, "✗ TCP networking setup failed");
    }
    
    return result;
}

/**
 * @brief Open TCP connection to server - modem agnostic
 */
static int open_tcp_connection(void) {
    ESP_LOGI(TAG, "Opening TCP connection to %s:%d using %s...", 
             tcp_config.server_ip, tcp_config.server_port, get_modem_type_name(detect_modem_type()));
    
    const modem_at_commands_t *cmd = get_modem_commands();
    
    // Build connection command using modem-specific format
    char connect_cmd[128];
    int result_len = get_tcp_connect_command(connect_cmd, sizeof(connect_cmd), 
                                           tcp_config.protocol, tcp_config.server_ip, tcp_config.server_port);
    
    if (result_len >= sizeof(connect_cmd)) {
        ESP_LOGE(TAG, "TCP connect command too long");
        return -1;
    }
    
    static at_command_def_t tcp_connect_commands[3];
    int cmd_count = 0;
    
    // Setup the connection command dynamically
    strncpy((char*)tcp_connect_commands[cmd_count].command, connect_cmd, sizeof(tcp_connect_commands[cmd_count].command) - 1);
    tcp_connect_commands[cmd_count].expected_response = cmd->tcp_connect_ok;
    tcp_connect_commands[cmd_count].timeout_ms = tcp_config.connection_timeout;
    tcp_connect_commands[cmd_count].wait_for_ok = false;  // Response varies by modem
    tcp_connect_commands[cmd_count].critical = true;
    tcp_connect_commands[cmd_count].description = "Open TCP connection";
    tcp_connect_commands[cmd_count].failure_action = "Check server availability";
    cmd_count++;
    
    // Optional: Query connection status
    tcp_connect_commands[cmd_count++] = (at_command_def_t){
        cmd->tcp_status_cmd, "+", 3000, false, false, "Query connection status", "Optional check"
    };
    
    current_tcp_state = TCP_STATE_CONNECTING;
    
    int result = execute_command_sequence(tcp_connect_commands, cmd_count, "TCP_CONNECT");
    
    if (result >= 0) {
        current_tcp_state = TCP_STATE_CONNECTED;
        ESP_LOGI(TAG, "✓ TCP connection established with %s", get_modem_type_name(detect_modem_type()));
    } else {
        current_tcp_state = TCP_STATE_ERROR;
        ESP_LOGE(TAG, "✗ TCP connection failed");
    }
    
    return result;
}

/**
 * @brief Send data over TCP connection - modem agnostic
 */
static int send_tcp_data(const char *data) {
    if (current_tcp_state != TCP_STATE_CONNECTED) {
        ESP_LOGE(TAG, "Cannot send data - TCP not connected (state: %d)", current_tcp_state);
        return -1;
    }
    
    ESP_LOGI(TAG, "Sending TCP data via %s: %s", get_modem_type_name(detect_modem_type()), data);
    
    const modem_at_commands_t *cmd = get_modem_commands();
    int data_len = strlen(data);
    
    // Build send command using modem-specific format
    char send_cmd[64];
    int result_len = get_tcp_send_command(send_cmd, sizeof(send_cmd), data_len);
    
    if (result_len >= sizeof(send_cmd)) {
        ESP_LOGE(TAG, "TCP send command too long");
        return -1;
    }
    
    static at_command_def_t tcp_send_commands[4];
    int cmd_count = 0;
    
    // Step 1: Initiate send mode
    strncpy((char*)tcp_send_commands[cmd_count].command, send_cmd, sizeof(tcp_send_commands[cmd_count].command) - 1);
    tcp_send_commands[cmd_count].expected_response = cmd->tcp_send_prompt;
    tcp_send_commands[cmd_count].timeout_ms = 5000;
    tcp_send_commands[cmd_count].wait_for_ok = false;  // Wait for prompt
    tcp_send_commands[cmd_count].critical = true;
    tcp_send_commands[cmd_count].description = "Enter send mode";
    tcp_send_commands[cmd_count].failure_action = "Check connection status";
    cmd_count++;
    
    // Step 2: Send actual data (this would need special handling in real implementation)
    strncpy((char*)tcp_send_commands[cmd_count].command, data, sizeof(tcp_send_commands[cmd_count].command) - 1);
    tcp_send_commands[cmd_count].expected_response = cmd->tcp_send_ok;
    tcp_send_commands[cmd_count].timeout_ms = 10000;
    tcp_send_commands[cmd_count].wait_for_ok = false;
    tcp_send_commands[cmd_count].critical = true;
    tcp_send_commands[cmd_count].description = "Send data";
    tcp_send_commands[cmd_count].failure_action = "Retry or reconnect";
    cmd_count++;
    
    // Step 3: Optional - check connection status after send
    tcp_send_commands[cmd_count++] = (at_command_def_t){
        cmd->tcp_status_cmd, "+", 2000, false, false, "Verify connection after send", "Optional check"
    };
    
    current_tcp_state = TCP_STATE_SENDING;
    
    int result = execute_command_sequence(tcp_send_commands, cmd_count, "TCP_SEND");
    
    if (result >= 0) {
        current_tcp_state = TCP_STATE_CONNECTED;
        ESP_LOGI(TAG, "✓ TCP data sent successfully via %s", get_modem_type_name(detect_modem_type()));
    } else {
        current_tcp_state = TCP_STATE_ERROR;
        ESP_LOGE(TAG, "✗ TCP data send failed");
    }
    
    return result;
}

/**
 * @brief Close TCP connection - modem agnostic
 */
static int close_tcp_connection(void) {
    ESP_LOGI(TAG, "Closing TCP connection for %s...", get_modem_type_name(detect_modem_type()));
    
    const modem_at_commands_t *cmd = get_modem_commands();
    
    static at_command_def_t tcp_close_commands[3];
    int cmd_count = 0;
    
    // Close TCP connection
    tcp_close_commands[cmd_count++] = (at_command_def_t){
        cmd->tcp_close_cmd, cmd->tcp_close_ok, 5000, false, false, "Close TCP connection", "Force close if needed"
    };
    
    // Optional: Deactivate PDP context
    tcp_close_commands[cmd_count++] = (at_command_def_t){
        cmd->pdp_deactivate_cmd, "OK", 10000, true, false, "Deactivate PDP context", "Optional cleanup"
    };
    
    int result = execute_command_sequence(tcp_close_commands, cmd_count, "TCP_CLOSE");
    
    current_tcp_state = TCP_STATE_DISCONNECTED;
    
    if (result >= 0) {
        ESP_LOGI(TAG, "✓ TCP connection closed");
    } else {
        ESP_LOGW(TAG, "✗ TCP close had issues (connection may already be closed)");
    }
    
    return result;
}

/**
 * @brief Main TCP task management function with modem detection
 */
int manage_tcp_tasks(void) {
    ESP_LOGI(TAG, "Starting TCP task management...");
    
    // Step 0: Initialize modem with auto-detection
    ESP_LOGI(TAG, "Detecting and initializing modem...");
    if (!initialize_modem_with_detection()) {
        ESP_LOGE(TAG, "Modem initialization failed");
        return -1;
    }
    
    // Check if TCP is supported
    if (!modem_supports_feature("tcp")) {
        ESP_LOGE(TAG, "Current modem does not support TCP");
        return -1;
    }
    
    int result;
    
    // Step 1: Setup networking
    result = setup_tcp_networking();
    if (result < 0) {
        ESP_LOGE(TAG, "TCP networking setup failed");
        return -1;
    }
    
    // Step 2: Open connection
    result = open_tcp_connection();
    if (result < 0) {
        ESP_LOGE(TAG, "TCP connection failed");
        return -1;
    }
    
    // Step 3: Main communication loop
    int task_cycle = 0;
    const int max_cycles = 5;  // Limit for demo
    
    while (task_cycle < max_cycles) {
        task_cycle++;
        ESP_LOGI(TAG, "--- TCP Task Cycle %d/%d ---", task_cycle, max_cycles);
        
        // Send task request
        char request[128];
        snprintf(request, sizeof(request), "GET_TASKS:%s:%d", get_modem_type_name(detect_modem_type()), task_cycle);
        
        result = send_tcp_data(request);
        if (result < 0) {
            ESP_LOGW(TAG, "Failed to send task request");
            break;
        }
        
        // In real implementation, you'd wait for and process incoming data here
        // Incoming data format varies by modem:
        // SIMCom: +IPD,len:data
        // Telit: #SRECV,len,data
        ESP_LOGI(TAG, "Waiting for task response... (would monitor for %s)", 
                 get_modem_commands()->incoming_data_prefix);
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // Simulate processing received task
        ESP_LOGI(TAG, "Processing received task...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // Send response
        char response[128];
        snprintf(response, sizeof(response), "TASK_DONE:%s:%d:SUCCESS", 
                 get_modem_type_name(detect_modem_type()), task_cycle);
        
        result = send_tcp_data(response);
        if (result < 0) {
            ESP_LOGW(TAG, "Failed to send task response");
        }
        
        // Wait before next cycle
        ESP_LOGI(TAG, "Waiting before next task cycle...");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    
    // Step 4: Cleanup
    close_tcp_connection();
    
    ESP_LOGI(TAG, "TCP task management completed");
    return 0;
}

/**
 * @brief Get current TCP connection state
 */
tcp_state_t get_tcp_state(void) {
    return current_tcp_state;
}

/**
 * @brief Configure TCP connection parameters
 */
void configure_tcp_connection(const char *server_ip, int port, const char *protocol) {
    strncpy(tcp_config.server_ip, server_ip, sizeof(tcp_config.server_ip) - 1);
    tcp_config.server_port = port;
    strncpy(tcp_config.protocol, protocol, sizeof(tcp_config.protocol) - 1);
    
    ESP_LOGI(TAG, "TCP config updated: %s://%s:%d", protocol, server_ip, port);
}