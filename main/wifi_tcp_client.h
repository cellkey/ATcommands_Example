/**
 * @file wifi_tcp_client.h
 * @brief WiFi TCP client — connects to gates.crea-cell.com:3000 over WiFi,
 *        sends HTTP GET registration, then runs the keepalive (KA) session.
 *        Mirrors the cellular modem path (server_keepalive.c) but uses a
 *        raw lwIP socket instead of AT+CIPSEND.
 *
 * Protocol summary (same as modem path):
 *   Unit → Server: GET /api/device/<unit_id>/listen HTTP/1.1
 *                  Transfer-Encoding: chunked
 *                  COPS_ID: WIFI   (not a cellular operator)
 *                  CSQ: <rssi dBm>
 *                  ...
 *   Server → Unit: HTTP/1.1 200 OK  …headers…  (then streaming body)
 *   KA (every 45 s): send "1\r\nA\r\n"  (chunked frame)
 *   Server ACK:      "1\r\nA\r\n"
 *   OPEN/KEEPOPEN/CLOSE relay commands handled; ACKs sent back via socket.
 */

#ifndef WIFI_TCP_CLIENT_H
#define WIFI_TCP_CLIENT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the WiFi TCP client task.
 *        Waits for wifi_manager_is_connected(), resolves the server hostname,
 *        opens a TCP connection, sends the GET registration, waits for 200 OK,
 *        then runs the keepalive loop indefinitely (reconnects on error).
 * @return true if task was created, false on failure.
 */
bool wifi_tcp_client_start(void);

/**
 * @brief Signal the WiFi TCP client task to stop after the current iteration.
 *        The task deletes itself when the stop flag is seen.
 */
void wifi_tcp_client_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_TCP_CLIENT_H */
