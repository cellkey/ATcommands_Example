/**
 * @file wifi_tcp_client.c
 * @brief WiFi TCP client: persistent connection to gates.crea-cell.com:3000.
 *
 * Flow per connection attempt:
 *  1. Wait for wifi_manager_is_connected()
 *  2. DNS getaddrinfo() → socket() → connect()
 *  3. Send HTTP GET registration (same format as modem path, COPS_ID=WIFI)
 *  4. Read HTTP response headers; confirm "200"
 *  5. Keepalive loop:
 *       - Every SERVER_KA_FIRST_DELAY_SEC seconds (first time), then every
 *         SERVER_KA_INTERVAL_SEC seconds: send "1\r\nA\r\n"
 *       - On recv: feed bytes into line parser → handle_server_line()
 *       - handle_server_line() replicates server_keepalive.c logic but sends
 *         ACKs directly via the open socket instead of AT+CIPSEND.
 *  6. On error / WiFi loss / server close: set server_connected=false, close
 *     socket, wait 5 s, restart from step 1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>    /* strncasecmp */
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "wifi_tcp_client.h"
#include "wifi_manager.h"
#include "nvs_config.h"
#include "a7670e_config.h"
#include "relay_control.h"
#include "server_keepalive.h"   /* SERVER_KA_FIRST_DELAY_SEC, SERVER_KA_INTERVAL_SEC */

static const char *TAG = "WIFI_TCP";

/* -----------------------------------------------------------------------
 * Protocol constants — identical to server_keepalive.c (modem path).
 * Server and unit speak the same custom binary protocol regardless of
 * transport layer.
 * --------------------------------------------------------------------- */
static const uint8_t KEEP_ALIVE[]  = { 0x31, 0x0D, 0x0A, 0x41, 0x0D, 0x0A }; /* "1\r\nA\r\n" */
static const uint8_t ACK_KEEPOPEN[]= { 0x31, 0x0D, 0x0A, 0x42, 0x0D, 0x0A }; /* "1\r\nB\r\n" */
static const uint8_t ACK_CLOSE[]   = { 0x31, 0x0D, 0x0A, 0x43, 0x0D, 0x0A }; /* "1\r\nC\r\n" */
static const char    OPENED_ACK[]  = "6\r\nOPENED\r\n";                        /* 11 bytes    */

/* -----------------------------------------------------------------------
 * Task & socket state
 * --------------------------------------------------------------------- */
static TaskHandle_t  s_task    = NULL;
static volatile bool s_stop    = false;
static volatile int  s_sock    = -1;   /* active socket while in KA loop */

/* -----------------------------------------------------------------------
 * Line-parser state (used only from wifi_tcp_task — no lock needed).
 * --------------------------------------------------------------------- */
static char s_lbuf[512];
static int  s_llen       = 0;
static bool s_saw_ka_one = false;

/* -----------------------------------------------------------------------
 * send_all(): loop until every byte is written or a real error occurs.
 * Returns true on success, false on error.
 * --------------------------------------------------------------------- */
static bool send_all(int sock, const void *data, size_t len) {
    const uint8_t *p   = (const uint8_t *)data;
    size_t         rem = len;
    while (rem > 0) {
        int sent = send(sock, p, rem, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue; /* retry */
            ESP_LOGW(TAG, "send_all() error after %u/%u bytes: %d",
                     (unsigned)(len - rem), (unsigned)len, errno);
            return false;
        }
        p   += sent;
        rem -= (size_t)sent;
    }
    return true;
}

/* -----------------------------------------------------------------------
 * Helper: send bytes on the active socket (ACKs from command handler).
 * Logs on error; does not abort the KA loop (caller decides).
 * --------------------------------------------------------------------- */
static void wifi_sock_send(const void *data, size_t len) {
    int fd = s_sock;
    if (fd < 0) return;
    if (!send_all(fd, data, len)) {
        ESP_LOGW(TAG, "wifi_sock_send: failed (%zu bytes)", len);
    }
}

/* -----------------------------------------------------------------------
 * handle_server_line()
 * Mirrors server_handle_incoming_line() (server_keepalive.c) but sends
 * ACKs via wifi_sock_send() instead of AT+CIPSEND.
 * --------------------------------------------------------------------- */
static void handle_server_line(const char *line) {
    if (line == NULL || line[0] == '\0') return;

    ESP_LOGD(TAG, "srv> [%s]", line);

    /* ---- KA ACK: two consecutive lines "1" then "A" ---- */
    if (!s_saw_ka_one && strcmp(line, "1") == 0) {
        s_saw_ka_one = true;
        return;
    }
    if (s_saw_ka_one && strcmp(line, "A") == 0) {
        s_saw_ka_one = false;
        ESP_LOGI(TAG, "KA ACK received (1/A)");
        return;
    }
    s_saw_ka_one = false;   /* any other line resets the two-step state */

    /* ---- FOTA ---- */
    if (strcmp(line, "FOTA") == 0) {
        /* WiFi OTA is a future feature; cellular FOTA requires the modem. */
        ESP_LOGW(TAG, "FOTA invite received — WiFi OTA not yet implemented");
        return;
    }

    /* ---- OPENxyz: timed relay open ----
     *   x   = relay number (1, 2, 3=both)
     *   yz  = duration in seconds (01-99)
     *   e.g. OPEN102 → relay 1 for 2 s
     */
    if (strncasecmp(line, "OPEN", 4) == 0) {
        int cmd_id    = atoi(line + 4);
        int relay_num = cmd_id / 100;
        int dur_sec   = cmd_id % 100;
        if (relay_num >= 1 && relay_num <= 3 && dur_sec >= 1 && dur_sec <= 99) {
            relay_command_t rcmd = {
                .relay_number = (uint8_t)relay_num,
                .duration_ms  = (uint32_t)dur_sec * 1000U,
                .activate     = true,
            };
            snprintf(rcmd.description, sizeof(rcmd.description),
                     "OPEN%d WiFi-srv", cmd_id);
            if (relay_execute_command(&rcmd) == ESP_OK) {
                ESP_LOGI(TAG, "OPEN%d → relay %d for %d s", cmd_id, relay_num, dur_sec);
                wifi_sock_send(OPENED_ACK, sizeof(OPENED_ACK) - 1); /* exclude '\0' */
            } else {
                ESP_LOGW(TAG, "OPEN%d: relay_execute_command failed", cmd_id);
            }
        } else {
            ESP_LOGI(TAG, "OPEN%d: no relay mapping (relay=%d, dur=%d s)",
                     cmd_id, relay_num, dur_sec);
        }
        return;
    }

    /* ---- KEEPOPEN[x]: permanent relay ON until CLOSE[x] ----
     *   [1] or [2] → that relay permanent
     *   [3] or bare KEEPOPEN → relay1 permanent + relay2 for 2 s
     */
    if (strncasecmp(line, "KEEPOPEN", 8) == 0) {
        int relay_id = 3; /* default = both */
        const char *p = line + 8;
        if (*p == '[') relay_id = atoi(p + 1);

        if (relay_id == 1 || relay_id == 2) {
            relay_command_t r = {
                .relay_number = (uint8_t)relay_id,
                .duration_ms  = 0,          /* 0 = permanent until CLOSE */
                .activate     = true,
            };
            snprintf(r.description, sizeof(r.description),
                     "KEEPOPEN[%d] WiFi perm", relay_id);
            if (relay_execute_command(&r) == ESP_OK) {
                wifi_sock_send(ACK_KEEPOPEN, sizeof(ACK_KEEPOPEN));
                uint16_t sr = nvs_config_get_status_reg(0x0000);
                if (relay_id == 1) sr |= STATUS_KEEP_RELAY1;
                else               sr |= STATUS_KEEP_RELAY2;
                nvs_config_set_status_reg(sr);
                ESP_LOGI(TAG, "KEEPOPEN[%d]: relay ON (permanent), ACK sent", relay_id);
            } else {
                ESP_LOGW(TAG, "KEEPOPEN[%d]: relay_execute_command failed", relay_id);
            }
        } else {
            /* relay_id == 3 (or anything else): relay1 permanent + relay2 for 2 s */
            relay_command_t r1 = {
                .relay_number = 1, .duration_ms = 0,    .activate = true,
            };
            relay_command_t r2 = {
                .relay_number = 2, .duration_ms = 2000, .activate = true,
            };
            snprintf(r1.description, sizeof(r1.description), "KEEPOPEN r1 WiFi perm");
            snprintf(r2.description, sizeof(r2.description), "KEEPOPEN r2 WiFi 2s");
            if (relay_execute_command(&r1) == ESP_OK) {
                relay_execute_command(&r2);
                wifi_sock_send(ACK_KEEPOPEN, sizeof(ACK_KEEPOPEN));
                uint16_t sr = nvs_config_get_status_reg(0x0000);
                sr |= STATUS_KEEP_RELAY1;
                nvs_config_set_status_reg(sr);
                ESP_LOGI(TAG, "KEEPOPEN[3]: r1 perm, r2 2 s, ACK sent");
            } else {
                ESP_LOGW(TAG, "KEEPOPEN[3]: relay1 execute failed");
            }
        }
        return;
    }

    /* ---- CLOSE[x]: deactivate relays ----
     *   [1] or [2] → that relay only
     *   [3] or bare CLOSE → both relays
     */
    if (strncasecmp(line, "CLOSE", 5) == 0) {
        int relay_id = 3; /* default = both */
        const char *p = line + 5;
        if (*p == '[') relay_id = atoi(p + 1);

        relay_command_t r = {
            .relay_number = (relay_id == 1 || relay_id == 2)
                            ? (uint8_t)relay_id : (uint8_t)3,
            .duration_ms  = 0,
            .activate     = false,
        };
        snprintf(r.description, sizeof(r.description),
                 "CLOSE[%d] WiFi-srv", relay_id);
        relay_execute_command(&r);
        wifi_sock_send(ACK_CLOSE, sizeof(ACK_CLOSE));

        uint16_t sr = nvs_config_get_status_reg(0x0000);
        if      (relay_id == 1) sr &= ~STATUS_KEEP_RELAY1;
        else if (relay_id == 2) sr &= ~STATUS_KEEP_RELAY2;
        else  sr &= (uint16_t)~(STATUS_KEEP_RELAY1 | STATUS_KEEP_RELAY2);
        nvs_config_set_status_reg(sr);
        ESP_LOGI(TAG, "CLOSE[%d]: relay%s OFF, ACK sent",
                 relay_id, (r.relay_number == 3) ? "s 1+2" : "");
        return;
    }

    ESP_LOGI(TAG, "Unhandled server line: [%s]", line);
}

/* -----------------------------------------------------------------------
 * process_received()
 * Feed raw bytes into the line-splitter state machine. Calls
 * handle_server_line() for each complete \r\n-terminated line.
 * Only called from wifi_tcp_task — no locking needed.
 * --------------------------------------------------------------------- */
static void process_received(const uint8_t *dae /Hta, int n) {
    for (int i = 0; i < n; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            /* Strip trailing \r added by the CR before LF */
            if (s_llen > 0 && s_lbuf[s_llen - 1] == '\r') s_llen--;
            s_lbuf[s_llen] = '\0';
            handle_server_line(s_lbuf);
            s_llen = 0;
        } else {
            if (s_llen < (int)sizeof(s_lbuf) - 1) 
            s_lbuf[s_llen++] = c;
        }
    }
}

/* -----------------------------------------------------------------------
 * read_http_headers()
 * Reads from sock until "\r\n\r\n" (end of HTTP response headers) or
 * A7670E_WAIT_200_OK_MS timeout.  Returns true if the status line
 * contains "200".  Any body bytes already received after the blank line
 * are fed into process_received() so they are not lost.
 * --------------------------------------------------------------------- */
static bool read_http_headers(int sock) {
    char hdr[768];
    int  hdr_len = 0;
    TickType_t deadline = xTaskGetTickCount() +
                          pdMS_TO_TICKS(A7670E_WAIT_200_OK_MS);

    while (xTaskGetTickCount() < deadline) {
        int n = recv(sock, hdr + hdr_len,
                     (int)sizeof(hdr) - 1 - hdr_len, 0);
        if (n > 0) {
            hdr_len += n;
            hdr[hdr_len] = '\0';
            char *body = strstr(hdr, "\r\n\r\n");
            if (body) {
                bool ok = (strstr(hdr, " 200 ") != NULL ||
                           strstr(hdr, " 200\r") != NULL);
                body += 4; /* skip the blank line itself */
                int body_n = hdr_len - (int)(body - hdr);
                if (ok && body_n > 0) {
                    process_received((const uint8_t *)body, body_n);
                }
                if (!ok) {
                    /* Log the status line for diagnosis */
                    char *eol = strchr(hdr, '\n');
                    if (eol) *eol = '\0';
                    ESP_LOGE(TAG, "Server replied: %s", hdr);
                }
                return ok;
            }
            if (hdr_len >= (int)sizeof(hdr) - 2) {
                /* Buffer full without finding blank line — unlikely */
                return strstr(hdr, " 200") != NULL;
            }
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
             continue;
              return false;
        } else {                                            
            return false; /* server closed before sending headers */
        }
    }
    ESP_LOGE(TAG, "Timeout waiting for 200 OK");
    return false;
}

/* -----------------------------------------------------------------------
 * wifi_tcp_task()
 * Main task: connect → register → keepalive → reconnect on error.
 * --------------------------------------------------------------------- */
static void wifi_tcp_task(void *arg) {
    char unit_id[NVS_CONFIG_MAX_LEN];
    char fw_ver [NVS_CONFIG_MAX_LEN];
    char get_buf[512];

    while (!s_stop) {

        /* ---- Phase 0: wait for WiFi association ---- */
        ESP_LOGI(TAG, "Waiting for WiFi …");
        while (!wifi_manager_is_connected() && !s_stop) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (s_stop) break;

        /* ---- Phase 1: DNS resolve ---- */
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", A7670E_SERVER_PORT);
        const struct addrinfo hints = {
            .ai_family   = AF_INET,
            .ai_socktype = SOCK_STREAM,
        };
        struct addrinfo *res = NULL;
        ESP_LOGI(TAG, "DNS %s:%s …", A7670E_SERVER_HOST, port_str);
        if (getaddrinfo(A7670E_SERVER_HOST, port_str, &hints, &res) != 0 ||
            res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed — retry in 10 s");
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        /* ---- Phase 2: create socket + connect ---- */
        int sock = socket(res->ai_family, res->ai_socktype, 0);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket() failed (%d)", errno);
            freeaddrinfo(res);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        /* 1 s recv timeout so the KA timer loop can fire on schedule */
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

      //  ESP_LOGI(TAG, "Connecting to server …");
        if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "connect() failed (%d) — retry in 5 s", errno);
            close(sock);
            freeaddrinfo(res);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        freeaddrinfo(res);
        ESP_LOGI(TAG, "WIFI connected to:  %s:%d", A7670E_SERVER_HOST, A7670E_SERVER_PORT);

        /* ---- Phase 3: send GET registration ---- */
        nvs_config_get_string(NVS_KEY_UNIT_ID, unit_id, sizeof(unit_id), A7670E_UNIT_ID);
        nvs_config_get_string(NVS_KEY_FW_VER,  fw_ver,  sizeof(fw_ver),  A7670E_FW_VERSION);
        int rssi = 0;
        esp_wifi_sta_get_rssi(&rssi);
        int n = snprintf(get_buf, sizeof(get_buf),
                         "GET /api/device/%s/listen HTTP/1.1\r\n"
                         "Transfer-Encoding: chunked\r\n"
                         "FW_VERSION: %s\r\n"
                         "COPS_ID: WIFI\r\n"
                         "CSQ: %d\r\n"
                         "SIM_ID: 0\r\n"
                         "\r\n",
                         unit_id, fw_ver, rssi);
        if (n <= 0 || n >= (int)sizeof(get_buf)) {
            ESP_LOGE(TAG, "GET buffer overflow");
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (!send_all(sock, get_buf, (size_t)n)) {
            ESP_LOGE(TAG, "GET send failed after partial write");
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        ESP_LOGI(TAG, "GET sent (%d bytes) | unit=%s fw=%s rssi=%d dBm",
                 n, unit_id, fw_ver, rssi);

        /* ---- Phase 4: wait for 200 OK ---- */
        /* Make the socket known to wifi_sock_send before any body bytes arrive */
        s_sock       = sock;
        s_llen       = 0;
        s_saw_ka_one = false;

        if (!read_http_headers(sock)) {
            ESP_LOGE(TAG, "No 200 OK — disconnecting, retry in 10 s");
            s_sock = -1;
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        ESP_LOGI(TAG, "200 OK — Server Connection Established");
        wifi_manager_set_server_connected(true);

        /* ---- Phase 5: keepalive loop ---- */
        TickType_t last_ka   = xTaskGetTickCount();
        uint32_t   ka_delay  = SERVER_KA_FIRST_DELAY_SEC * 1000U; /* ms */

        while (!s_stop && wifi_manager_is_connected()) {
            /* Send keepalive when timer fires */
            uint32_t elapsed = (uint32_t)(
                (xTaskGetTickCount() - last_ka) * portTICK_PERIOD_MS);
            if (elapsed >= ka_delay) {
                ESP_LOGI(TAG, "→ KA →");
                if (!send_all(sock, KEEP_ALIVE, sizeof(KEEP_ALIVE))) {
                    ESP_LOGE(TAG, "KA sending failed");
                    break;
                }
                last_ka  = xTaskGetTickCount();
                ka_delay = SERVER_KA_INTERVAL_SEC * 1000U;
            }

            /* Non-blocking recv (1 s timeout via SO_RCVTIMEO) */
            uint8_t rx[256];
            int rxn = recv(sock, rx, sizeof(rx), 0);
            if (rxn > 0) {
                process_received(rx, rxn);
            } else if (rxn == 0) {
                ESP_LOGI(TAG, "Server closed TCP connection");
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGW(TAG, "recv() error: %d — reconnecting", errno);
                break;
            }
            /* errno == EAGAIN/EWOULDBLOCK: recv timed out, loop to check KA timer */
        }

        /* ---- Cleanup before next connection attempt ---- */
        wifi_manager_set_server_connected(false);
        s_sock = -1;
        close(sock);
        ESP_LOGI(TAG, "Disconnected. Reconnect in 5 s …");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */
bool wifi_tcp_client_start(void) {
    if (s_task != NULL) {
        ESP_LOGW(TAG, "already running");
        return true;
    }
    s_stop = false;
    BaseType_t r = xTaskCreate(wifi_tcp_task, "wifi_tcp",
                               4096, NULL, 5, &s_task);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return false;
    }
    ESP_LOGI(TAG, "WiFi TCP client task started");
    return true;
}

void wifi_tcp_client_stop(void) {
    s_stop = true;
}
