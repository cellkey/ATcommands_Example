/**
 * @file at_command_api.h
 * @brief Public API for the AT command system
 * 
 * This header exposes the public interface of the AT command system
 * so that examples and other modules can use the AT command functionality.
 */

#ifndef AT_COMMAND_API_H
#define AT_COMMAND_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

// Constants
/* >= modem CIPRXGET payload (~1500) so one binary segment without \\n is not truncated. */
#define LINE_BUFFER_SIZE      1600
/* Long AT lines e.g. AT+HTTPPARA="URL","https://..." */
#define MAX_AT_COMMAND_LEN    512
#define UART_NUM_AT           UART_NUM_1           // UART port for AT commands

// AT Command result enumeration
typedef enum 
{
    AT_RESULT_SUCCESS = 0,
    AT_RESULT_TIMEOUT,
    AT_RESULT_ERROR,
    AT_RESULT_UNEXPECTED_RESPONSE
} at_result_t;

// External variables (accessible from examples)
extern char last_response[LINE_BUFFER_SIZE];
/** Last matched data line from send_at_command_ex (not overwritten by later "OK"); use for CREG etc. */
extern char last_matched_response[LINE_BUFFER_SIZE];

// Enhanced command structure for command sequences
typedef struct {
    const char *command;
    const char *expected_response;
    uint32_t timeout_ms;
    bool wait_for_ok;           // Use enhanced API control
    bool critical;              // If true, failure stops the sequence
    const char *description;
    const char *failure_action; // What to do if this command fails
} at_command_def_t;

// Core AT command functions
at_result_t send_at_command(const char *command, const char *expected_response, uint32_t timeout_ms);
at_result_t send_at_command_ex(const char *command, const char *expected_response, uint32_t timeout_ms, bool wait_for_ok);

// Command sequence execution
int execute_command_sequence(const at_command_def_t *sequence, int sequence_length, const char *sequence_name);

/** Wait for a line containing substr from modem (e.g. "200 OK"). Call when no AT command is in progress. */
at_result_t wait_for_line_containing(const char *substr, uint32_t timeout_ms);

/** Get next line from modem response queue (e.g. after AT+CIPRXGET=2 to drain payload). Returns true if got line. */
bool get_next_response_line(char *buf, size_t buf_size, uint32_t timeout_ms);

/**
 * Same as get_next_response_line but returns original byte length (may include embedded \\0 before end).
 * @param out_len  if non-NULL, set to payload length in queue (not including your added NUL).
 */
bool get_next_response_line_ex(char *buf, size_t buf_size, size_t *out_len, uint32_t timeout_ms);

/** Send raw bytes on UART (e.g. after CIPSEND ">"). Use after send_at_command_ex(..., ">", ..., false). */
void uart_send_raw_bytes(const uint8_t *data, size_t len);

/** A7670E: send AT+CIPSEND=socket_id,len, wait for ">", send payload, wait for +CIPSEND/OK. */
at_result_t send_at_then_raw_data(int socket_id, size_t len, const uint8_t *payload);

// Task control functions
bool start_modem_init_task(void);
bool stop_modem_init_task(void);
bool is_modem_init_active(void);

// TCP task management functions
int manage_tcp_tasks(void);


#ifdef __cplusplus
}
#endif

#endif // AT_COMMAND_API_H