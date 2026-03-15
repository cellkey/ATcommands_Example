/**
 * @file modem_definitions.h
 * @brief Modem-specific AT command definitions and configuration
 * 
 * Supports multiple modem families with easy switching between them
 */

#ifndef MODEM_DEFINITIONS_H
#define MODEM_DEFINITIONS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Supported modem types
typedef enum {
    MODEM_TYPE_SIMCOM_A7670 = 0,    // SIMCom A7670, SIM800, SIM900 family
    MODEM_TYPE_TELIT_LE910,         // Telit LE910xxx family
    MODEM_TYPE_QUECTEL_EC25,        // Quectel EC25, BG96 family (future)
    MODEM_TYPE_UBLOX_SARA,          // u-blox SARA-R4/R5 family (future)
    MODEM_TYPE_AUTO_DETECT,         // Try to auto-detect modem type
    MODEM_TYPE_MAX
} modem_type_t;

// Current modem configuration - Change this to switch modem types
#define CURRENT_MODEM_TYPE    MODEM_TYPE_SIMCOM_A7670
// #define CURRENT_MODEM_TYPE    MODEM_TYPE_TELIT_LE910

// Modem capability flags
typedef struct {
    bool supports_tcp;
    bool supports_udp;
    bool supports_ssl;
    bool supports_http;
    bool supports_ftp;
    bool supports_mqtt;
    bool has_gnss;
    int max_sockets;
} modem_capabilities_t;

// Modem-specific AT commands structure
typedef struct {
    // Basic modem info
    const char *manufacturer_cmd;
    const char *model_cmd;
    const char *version_cmd;
    
    // Network commands
    const char *network_reg_cmd;
    const char *signal_quality_cmd;
    const char *operator_cmd;
    
    // PDP context commands
    const char *pdp_context_cmd;
    const char *pdp_activate_cmd;
    const char *pdp_deactivate_cmd;
    
    // TCP/IP commands
    const char *tcp_connect_cmd_format;     // Format string: protocol, server, port
    const char *tcp_send_cmd_format;        // Format string: data length
    const char *tcp_send_prompt;            // Expected prompt for data entry
    const char *tcp_close_cmd;
    const char *tcp_status_cmd;
    
    // Expected responses
    const char *tcp_connect_ok;
    const char *tcp_send_ok;
    const char *tcp_close_ok;
    const char *incoming_data_prefix;       // +IPD, #SRECV, etc.
    
    // HTTP commands (if supported)
    const char *http_init_cmd;
    const char *http_term_cmd;
    const char *http_url_cmd_format;
    
    // Configuration commands
    const char *echo_off_cmd;
    const char *error_format_cmd;
} modem_at_commands_t;

// SIMCom A7670 command set
static const modem_at_commands_t simcom_a7670_commands = {
    // Basic info
    .manufacturer_cmd = "AT+CGMI",
    .model_cmd = "AT+CGMM",
    .version_cmd = "AT+CGMR",
    
    // Network
    .network_reg_cmd = "AT+CREG?",
    .signal_quality_cmd = "AT+CSQ",
    .operator_cmd = "AT+COPS?",
    
    // PDP context
    .pdp_context_cmd = "AT+CGDCONT=1,\"IP\",\"internet\"",
    .pdp_activate_cmd = "AT+CGACT=1,1",
    .pdp_deactivate_cmd = "AT+CGACT=0,1",
    
    // TCP/IP
    .tcp_connect_cmd_format = "AT+CIPSTART=\"%s\",\"%s\",%d",
    .tcp_send_cmd_format = "AT+CIPSEND=%d",
    .tcp_send_prompt = ">",
    .tcp_close_cmd = "AT+CIPCLOSE",
    .tcp_status_cmd = "AT+CIPSTATUS",
    
    // Expected responses
    .tcp_connect_ok = "CONNECT OK",
    .tcp_send_ok = "SEND OK",
    .tcp_close_ok = "CLOSE OK",
    .incoming_data_prefix = "+IPD",
    
    // HTTP
    .http_init_cmd = "AT+HTTPINIT",
    .http_term_cmd = "AT+HTTPTERM",
    .http_url_cmd_format = "AT+HTTPPARA=\"URL\",\"%s\"",
    
    // Configuration
    .echo_off_cmd = "ATE0",
    .error_format_cmd = "AT+CMEE=1"
};

// Telit LE910xxx command set
static const modem_at_commands_t telit_le910_commands = {
    // Basic info
    .manufacturer_cmd = "AT+CGMI",
    .model_cmd = "AT+CGMM", 
    .version_cmd = "AT+CGMR",
    
    // Network
    .network_reg_cmd = "AT+CREG?",
    .signal_quality_cmd = "AT+CSQ",
    .operator_cmd = "AT+COPS?",
    
    // PDP context
    .pdp_context_cmd = "AT+CGDCONT=1,\"IP\",\"internet\"",
    .pdp_activate_cmd = "AT#SGACT=1,1",      // Telit-specific activation
    .pdp_deactivate_cmd = "AT#SGACT=1,0",    // Telit-specific deactivation
    
    // TCP/IP - Telit uses different socket commands
    .tcp_connect_cmd_format = "AT#SD=1,0,%d,\"%s\",0,0,1",  // socket_dial: connId,protocol,port,server,closure,packet,timeout
    .tcp_send_cmd_format = "AT#SSEND=1",     // Send on socket 1
    .tcp_send_prompt = ">",
    .tcp_close_cmd = "AT#SH=1",             // Socket hang up
    .tcp_status_cmd = "AT#SS",              // Socket status
    
    // Expected responses
    .tcp_connect_ok = "CONNECT",
    .tcp_send_ok = "OK",
    .tcp_close_ok = "OK", 
    .incoming_data_prefix = "#SRECV",       // Telit incoming data
    
    // HTTP - Telit has different HTTP commands
    .http_init_cmd = "AT#HTTPCFG=1,\"your-server.com\",80,0",
    .http_term_cmd = "AT#HTTPCLOSE=1",
    .http_url_cmd_format = "AT#HTTPQRY=1,0,\"%s\"",
    
    // Configuration
    .echo_off_cmd = "ATE0",
    .error_format_cmd = "AT+CMEE=1"
};

// Modem capabilities
static const modem_capabilities_t simcom_a7670_caps = {
    .supports_tcp = true,
    .supports_udp = true,
    .supports_ssl = true,
    .supports_http = true,
    .supports_ftp = true,
    .supports_mqtt = true,
    .has_gnss = true,
    .max_sockets = 8
};

static const modem_capabilities_t telit_le910_caps = {
    .supports_tcp = true,
    .supports_udp = true,
    .supports_ssl = true,
    .supports_http = true,
    .supports_ftp = true,
    .supports_mqtt = false,     // Depends on firmware version
    .has_gnss = true,
    .max_sockets = 6
};

// Function to get current modem commands
const modem_at_commands_t* get_modem_commands(void);

// Function to get current modem capabilities  
const modem_capabilities_t* get_modem_capabilities(void);

// Function to get modem type name
const char* get_modem_type_name(modem_type_t type);

// Modem type management functions
void set_modem_type(modem_type_t type);

// Auto-detection functions
modem_type_t detect_modem_type(void);
bool verify_modem_type(modem_type_t expected_type);
bool initialize_modem_with_detection(void);

// Feature support functions
bool modem_supports_feature(const char *feature);

// TCP command generation functions
int get_tcp_connect_command(char *buffer, size_t buffer_size, const char *protocol, const char *server, int port);
int get_tcp_send_command(char *buffer, size_t buffer_size, int data_length);

#ifdef __cplusplus
}
#endif

#endif // MODEM_DEFINITIONS_H