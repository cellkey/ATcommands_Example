/**
 * @file server_keepalive.h
 * @brief Keepalive (KA) to server and handling of ACK / server commands
 *
 * Protocol: first KA 8 s after connect, then every 45 s when idle (no pending read/ring).
 *   KEEP_ALIVE  = "1\r\nA\r\n"  (unit -> server)
 * Server responds with ACK (e.g. "1\r\nA\r\n"); detect in server_handle_incoming_line().
 */

#ifndef SERVER_KEEPALIVE_H
#define SERVER_KEEPALIVE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** First KA: delay in seconds after connect (desired protocol: 8 s). */
#define SERVER_KA_FIRST_DELAY_SEC  8
/** Subsequent KA: interval in seconds when idle (desired protocol: 45 s). */
#define SERVER_KA_INTERVAL_SEC    45
/** Max seconds to wait for KA ACK after sending (KA session is ~350 ms; 1 s is enough to detect no ACK). */
#define SERVER_KA_ACK_TIMEOUT_SEC 1

/** TCP connection id used for CIPSEND (match A7670E link 1). */
#define SERVER_TCP_LINK_ID        1

/** Max chars for caller ID (+CLCC → CHECK_USER), including null. */
#define SERVER_RING_CALLER_MAX    32

/**
 * @brief Start the keepalive task. Call after modem init + connect (e.g. after "In main loop..ready").
 *        Task sends KA every SERVER_KA_INTERVAL_SEC and will later react to ACK/commands.
 * @return true if task was created, false on failure.
 */
bool server_keepalive_task_start(void);

/**
 * @brief Lightweight task for modem-slave + WiFi mode: on ring, AT+CHUP then CHECK_USER via WiFi.
 *        Do not use together with server_keepalive_task_start().
 */
bool server_keepalive_ring_worker_start(void);

/**
 * @brief Stop the keepalive task (e.g. on disconnect).
 */
void server_keepalive_task_stop(void);

/**
 * @brief Call when a line has been received from the server (e.g. from +IPD payload or UART line).
 *        Implement ACK detection here; later add parsing of server commands and acknowledgements.
 * @param line  Null-terminated line (no trailing CRLF).
 */
void server_handle_incoming_line(const char *line);

/**
 * @brief Call when modem sends +CIPRXGET: <link>,<param2> (buffered data notification in CIPRXGET=1 mode).
 *        Implement: send AT+CIPRXGET=2,<link>,<len> to read buffer, then parse payload and call server_handle_incoming_line().
 * @param link_id   Connection id (e.g. 1).
 * @param param2   Second parameter (meaning from modem docs / log: e.g. length or chunk count).
 */
void server_on_ciprxget_urc(int link_id, int param2);

/**
 * @brief Call when modem sends +CLCC (incoming call / RING). Unit will hang up, send CHECK_USER:<number> to server,
 *        then wait for APPROVEDxxx or REJECT in next buffered read.
 * @param caller_id  Null-terminated caller number (e.g. "0522784873").
 */
void server_on_ring(const char *caller_id);

/**
 * @brief Call when modem sends +IPCLOSE: <link>,<reason> (TCP connection closed by server/network).
 *        For link 1 (server), triggers reconnect (same as run_a7670e_reconnect) from keepalive task.
 */
void server_on_ipclose(int link_id);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_KEEPALIVE_H */
