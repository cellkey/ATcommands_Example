/**
 * @file Modem_Config_Handling.h
 * @brief Consolidated modem configuration and communication handler
 * 
 * Production-ready modem management combining HTTP, TCP/IP, and task handling
 * into a single, efficient interface. Supports multiple modem types.
 * 
 */

#ifndef MODEM_CONFIG_HANDLING_H
#define MODEM_CONFIG_HANDLING_H

#include <stdbool.h>
#include <stdint.h>
#include "at_command_api.h"
#include "modem_definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

// Consolidated configuration structure
typedef struct {
    // Modem settings
    modem_type_t modem_type;
    bool auto_detect_modem;
    
    // Network settings
    char apn[32];
    char username[32];
    char password[32];
    
    // Server settings (supports both HTTP and TCP)
    char server_address[128];
    int server_port;
    char protocol[8];           // "HTTP", "TCP", "UDP"
    
    // Communication settings
    uint32_t connection_timeout;
    uint32_t response_timeout;
    uint8_t retry_attempts;
    uint32_t retry_delay;
    
    // Task management
    bool enable_periodic_tasks;
    uint32_t task_interval;
    uint8_t max_task_cycles;    // 0 = unlimited
    
    // Power management
    bool enable_power_saving;
    uint32_t idle_timeout;
    
} modem_config_t;

// Communication mode enumeration
typedef enum {
    COMM_MODE_HTTP_REST = 0,    // HTTP REST API communication
    COMM_MODE_TCP_SOCKET,       // Direct TCP socket communication
    COMM_MODE_UDP_SOCKET,       // UDP socket communication
    COMM_MODE_MIXED             // Support multiple modes
} communication_mode_t;

// Task status enumeration
typedef enum {
    TASK_STATUS_IDLE = 0,
    TASK_STATUS_CONNECTING,
    TASK_STATUS_CONNECTED,
    TASK_STATUS_SENDING,
    TASK_STATUS_RECEIVING,
    TASK_STATUS_PROCESSING,
    TASK_STATUS_ERROR,
    TASK_STATUS_DISCONNECTED
} task_status_t;

// Callback function types for user application
typedef void (*modem_data_received_cb_t)(const char *data, size_t length);
typedef void (*modem_status_changed_cb_t)(task_status_t status);
typedef bool (*modem_task_processor_cb_t)(const char *task_data, char *response, size_t response_size);

// Main modem management functions
bool modem_handler_init(const modem_config_t *config);
bool modem_handler_start(void);
bool modem_handler_stop(void);
void modem_handler_cleanup(void);

// Configuration management
bool modem_set_config(const modem_config_t *config);
bool modem_get_config(modem_config_t *config);
bool modem_save_config_to_nvs(void);
bool modem_load_config_from_nvs(void);

// Communication functions
bool modem_send_data(const char *data, size_t length);
bool modem_send_http_request(const char *endpoint, const char *data);
bool modem_send_tcp_data(const char *data);
bool modem_connect_to_server(void);
bool modem_disconnect_from_server(void);

// Status and monitoring
task_status_t modem_get_status(void);
bool modem_is_connected(void);
int modem_get_signal_strength(void);
bool modem_get_network_info(char *operator_name, size_t name_size);

// Callback registration
void modem_register_data_callback(modem_data_received_cb_t callback);
void modem_register_status_callback(modem_status_changed_cb_t callback);
void modem_register_task_processor(modem_task_processor_cb_t callback);

// Callback trigger functions (for testing and manual activation)
void modem_trigger_data_callback(const char *data, size_t length);
void modem_trigger_status_callback(task_status_t status);
bool modem_trigger_task_callback(const char *task_data, char *response, size_t response_size);

// Utility functions
bool modem_test_connection(void);
bool modem_reset_connection(void);
void modem_get_diagnostics(char *buffer, size_t buffer_size);

// Default configuration presets
modem_config_t modem_get_default_config(void);
modem_config_t modem_get_http_config(const char *server, int port);
modem_config_t modem_get_tcp_config(const char *server, int port);

#ifdef __cplusplus
}
#endif

#endif // MODEM_CONFIG_HANDLING_H