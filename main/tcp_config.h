/**
 * @file tcp_config.h
 * @brief TCP/IP configuration settings for the AT command system
 * 
 * Simpler alternative to HTTP mode for direct TCP/IP communication
 */

#ifndef TCP_CONFIG_H
#define TCP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// TCP Server Configuration
// ========================
// Modify these values to match your server setup

// Server IP or hostname
#define TCP_SERVER_IP           "your-server.com"  // or "192.168.1.100"

// Server port
#define TCP_SERVER_PORT         8080

// Protocol type
#define TCP_PROTOCOL            "TCP"              // "TCP" or "UDP"

// Connection settings
#define TCP_CONNECTION_TIMEOUT  30000              // ms
#define TCP_KEEPALIVE_INTERVAL  60000              // ms
#define TCP_AUTO_RECONNECT      true

// APN Configuration (same as HTTP)
// ================================
#define CELLULAR_APN            "internet"
#define CELLULAR_USERNAME       ""
#define CELLULAR_PASSWORD       ""

// TCP AT Command Timeouts
// =======================
#define TIMEOUT_PDP_CONTEXT     30000              // ms
#define TIMEOUT_TCP_CONNECT     15000              // ms
#define TIMEOUT_TCP_SEND        10000              // ms
#define TIMEOUT_TCP_RECEIVE     20000              // ms
#define TIMEOUT_TCP_CLOSE       5000               // ms

// Data Format Configuration
// =========================
#define TCP_MAX_DATA_SIZE       1460               // bytes (TCP MSS)
#define TCP_BUFFER_SIZE         2048               // bytes
#define TCP_HEADER_INFO         true               // Show +IPD header info

// Task Management
// ===============
#define TCP_TASK_CYCLE_LIMIT    0                  // 0 = unlimited
#define TCP_TASK_INTERVAL       10000              // ms between cycles
#define TCP_RECONNECT_ATTEMPTS  3                  // Auto-reconnect tries
#define TCP_RECONNECT_DELAY     5000               // ms between attempts

// Message Protocol (customize for your application)
// ================================================
#define MSG_GET_TASKS           "GET_TASKS:ESP32"
#define MSG_TASK_DONE           "TASK_DONE:ESP32"
#define MSG_HEARTBEAT           "HEARTBEAT:ESP32"
#define MSG_STATUS_REQUEST      "STATUS:ESP32"

// Expected server responses
#define RESP_TASK_DATA          "TASK:"
#define RESP_ACK                "ACK:"
#define RESP_ERROR              "ERROR:"
#define RESP_HEARTBEAT_OK       "HB_OK"

// Debug and Testing
// =================
#define TCP_DEBUG_VERBOSE       1                  // Detailed logging
#define TCP_SIMULATE_DATA       1                  // Use simulated responses for testing

// Example server configurations for testing
// =========================================
// Uncomment one of these for quick testing:

// Local test server
// #define TCP_SERVER_IP "192.168.1.100"
// #define TCP_SERVER_PORT 8080

// Cloud test server (if you have one)
// #define TCP_SERVER_IP "your-cloud-server.com"
// #define TCP_SERVER_PORT 8080

// TCP echo server for testing
// #define TCP_SERVER_IP "tcpbin.com"
// #define TCP_SERVER_PORT 4242

#ifdef __cplusplus
}
#endif

#endif // TCP_CONFIG_H