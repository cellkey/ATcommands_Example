/**
 * @file server_keepalive.c
 * @brief Keepalive (KA) send every 45s; read buffered server data (AT+CIPRXGET=2,1) and parse ACK/commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>  /* for strcasecmp/strncasecmp */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "at_command_api.h"
#include "server_keepalive.h"
#include "relay_control.h"
#include "a7670e_sequences.h"
#include "nvs_config.h"
#include "fota_modem.h"
#include "wifi_tcp_client.h"

static const char *TAG = "SRV_KA";

static void fota_modem_debug_log_snapshot(const char *reason);

static TaskHandle_t s_ka_task_handle = NULL;
static TaskHandle_t s_ring_worker_handle = NULL;
static volatile bool s_ka_task_running = false;
/* Sized FOTA: detect long stalls (same remaining count) → likely server/proxy idle timeout before +IPCLOSE. */
static uint32_t s_fota_stall_last_remain = UINT32_MAX;
static TickType_t s_fota_stall_since_tick;
static bool s_fota_stall_timing;
static TickType_t s_fota_trace_period_tick;
static volatile bool s_pending_read = false;
static volatile int s_pending_read_link = 1;
/* Tracks whether a KA ACK (1/A sequence) was seen during the current buffered-read. */
static bool s_ka_ack_since_read = false;
/* No-ACK handling: set when we send a KA, cleared when we see ACK. If timeout and still set: retry once after 8s, then reconnect. */
static bool s_ka_sent_waiting_ack = false;
static bool s_ka_retry_pending = false;
/* Track consecutive KA send (CIPSEND) failures – after 2 in a row we trigger a full modem re-init. */
static int s_ka_send_fail_count = 0;

/* Ring / CHECK_USER flow: on +CLCC we set pending; KA task (modem TCP) or ring_worker (WiFi path) runs CHUP + CHECK_USER. */
static volatile bool s_pending_ring = false;
static char s_pending_ring_caller[SERVER_RING_CALLER_MAX];
static bool s_waiting_check_user_response = false;

/* +IPCLOSE: 1,1 → server link closed; keepalive task will run reconnect */
static volatile bool s_pending_ipclose_reconnect = false;
static int s_check_user_approved = -1;  /* -1 none yet, 0 REJECT, 1 APPROVED */
static int s_approved_id = 0;            /* e.g. 302 from APPROVED302 */

/** KA payload: same as old ATMEGA working code: "1\\r\\nA\\r\\n" (6 bytes, no chunk prefix). Explicit bytes to avoid hidden chars. */
static const uint8_t KEEP_ALIVE[] = { 0x31, 0x0D, 0x0A, 0x41, 0x0D, 0x0A };
#define KEEP_ALIVE_LEN  (sizeof(KEEP_ALIVE))

/** ACKs for relay commands back to server (same 6-byte framing as KA). */
static const uint8_t ACK_KEEPOPEN[] = { 0x31, 0x0D, 0x0A, 0x42, 0x0D, 0x0A };  /* "1\r\nB\r\n" */
#define ACK_KEEPOPEN_LEN (sizeof(ACK_KEEPOPEN))
static const uint8_t ACK_CLOSE[]    = { 0x31, 0x0D, 0x0A, 0x43, 0x0D, 0x0A };  /* "1\r\nC\r\n" */
#define ACK_CLOSE_LEN    (sizeof(ACK_CLOSE))

/** OPENED ack after APPROVED: 11 bytes */
static const char OPENED_ACK[] = "6\r\nOPENED\r\n";
#define OPENED_ACK_LEN 11


/**
 * @brief Handle one line received from server: detect KA ACK and simple commands (e.g. OPENxxx).
 *
 * From reference log, KA ACK payload is: "\r\n1\r\nA" (six bytes). After line-splitting we see:
 *   line "1"
 *   line "A"
 *   possibly an empty line before "OK".
 *
 * Commands like OPEN302 arrive as:
 *   line "7"        (length or chunk size)
 *   line "OPEN302"  (command payload)
 *   ...
 */
void server_handle_incoming_line(const char *line) {
    static bool s_saw_ka_one = false;

    if (line == NULL) return;

    /* Ignore pure CIPRXGET/OK lines here; they are part of AT plumbing, not payload. */
    if (strncmp(line, "+CIPRXGET:", 10) == 0) return;
    if (strcmp(line, "OK") == 0) return;

    /* --- KA ACK detection: "\r\n1\r\nA" → lines "1", then "A" --- */
    if (!s_saw_ka_one && strcmp(line, "1") == 0) {
        s_saw_ka_one = true;
        return;
    }

    if (s_saw_ka_one && (strcmp(line, "A") == 0 || strstr(line, "A\r\n") != NULL)) {
        s_saw_ka_one = false;
        s_ka_ack_since_read = true;
        ESP_LOGI(TAG, "KA ACK.. (1/A)");
        return;
    }

    /* Any other line resets the simple KA ACK sequence state */
    s_saw_ka_one = false;

    /* --- FOTA: server sends length line "4" then "FOTA" (we only match payload line). --- */
    if (strcmp(line, "FOTA") == 0) {
        fota_on_server_invite();
        return;
    }

    /* --- CHECK_USER response (only when we sent CHECK_USER after a ring) --- */
    if (s_waiting_check_user_response) {
        if (strncmp(line, "APPROVED", 8) == 0) {
            s_check_user_approved = 1;
            s_approved_id = atoi(line + 8);  /* e.g. APPROVED302 → 302 */
            ESP_LOGI(TAG, "CHECK_USER response: APPROVED (id=%d)", s_approved_id);
            return;
        }
        if (strcmp(line, "REJECT") == 0) {
            s_check_user_approved = 0;
            ESP_LOGI(TAG, "CHECK_USER response: REJECT");
            return;
        }
    }

    /* --- OPENxyz from server: drive shared relay control (same as BLE app)
     *     Encoding: x = relay number (1=relay1, 2=relay2, 3=both)
     *               yz = duration in seconds (e.g. OPEN102 → relay 1 for 2 s)
     *     Case-insensitive: "open102", "Open102", etc. are accepted.
     */
    if (strncasecmp(line, "OPEN", 4) == 0) {
        int cmd_id = atoi(line + 4);
        int relay_num = cmd_id / 100;      /* 102/100 = 1, 305/100 = 3 */
        int duration_sec = cmd_id % 100;   /* 102%100 = 2, 305%100 = 5 */

        if (relay_num >= 1 && relay_num <= 3 && duration_sec > 0 && duration_sec <= 99) {
            relay_command_t rcmd = {
                .relay_number = (uint8_t)relay_num,
                .duration_ms = (uint32_t)duration_sec * 1000U,
                .activate = true,
            };
            snprintf(rcmd.description, sizeof(rcmd.description), "OPEN%d from server", cmd_id);
            esp_err_t rre = relay_execute_gated_server_activation(&rcmd);
            if (rre == ESP_OK) {
                ESP_LOGI(TAG, "Server OPEN%d → relay %d (%ds)", cmd_id, relay_num, duration_sec);
                if (send_at_then_raw_data(SERVER_TCP_LINK_ID, OPENED_ACK_LEN, (const uint8_t *)OPENED_ACK) == AT_RESULT_SUCCESS) {
                    ESP_LOGI(TAG, "OPENED ack sent (for OPEN%d)", cmd_id);
                } else {
                    ESP_LOGW(TAG, "OPENED ack send failed (for OPEN%d)", cmd_id);
                }
            } else if (rre == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "OPEN%d ignored (digital-input gate, GPIO22 not active)", cmd_id);
            }
        } else {
            ESP_LOGI(TAG, "Server command received: %s (id=%d, no relay mapping)", line, cmd_id);
        }
        return;
    }

    /* --- KEEPOPEN[x]: special door mode
     * x = 1 or 2 → activate that relay permanently (until CLOSE[x]).
     * x = 3 or no [x] → both relays ON, then after 2 s only relay 1 stays ON.
     * Case-insensitive: "keepopen[1]", "KeepOpen[3]", etc. are accepted.
     */
    if (strncasecmp(line, "KEEPOPEN", 8) == 0) {
        int relay_id = 3; /* default = both / special mode */
        const char *p = line + 8;
        if (*p == '[') {
            relay_id = atoi(p + 1);
        }

        if (relay_id == 1 || relay_id == 2) {
            relay_command_t r = {
                .relay_number = (uint8_t)relay_id,
                .duration_ms = 0,      /* permanent until CLOSE[x] */
                .activate = true,
            };
            snprintf(r.description, sizeof(r.description), "KEEPOPEN[%d] relay permanent", relay_id);
            if (relay_execute_command(&r) == ESP_OK) {
                ESP_LOGI(TAG, "KEEPOPEN[%d]: relay %d ON (permanent)", relay_id, relay_id);
                if (send_at_then_raw_data(SERVER_TCP_LINK_ID, ACK_KEEPOPEN_LEN, ACK_KEEPOPEN) == AT_RESULT_SUCCESS) {
                    ESP_LOGI(TAG, "KEEPOPEN[%d]: ACK sent (1\\r\\nB\\r\\n)", relay_id);
                    /* Persist KEEPOPEN state in status_reg so relay stays ON after power loss. */
                    uint16_t sr = nvs_config_get_status_reg(0x0000);
                    if (relay_id == 1) {
                        sr |= STATUS_KEEP_RELAY1;
                    } else if (relay_id == 2) {
                        sr |= STATUS_KEEP_RELAY2;
                    }
                    (void)nvs_config_set_status_reg(sr);
                } else {
                    ESP_LOGW(TAG, "KEEPOPEN[%d]: failed to send ACK", relay_id);
                }
            } else {
                ESP_LOGW(TAG, "KEEPOPEN[%d]: failed to queue relay command", relay_id);
            }
        } else {
            /* relay_id == 3 or anything else → treat as both relays ON, then relay 2 for 2 s. */
            relay_command_t r1 = {
                .relay_number = 1,
                .duration_ms = 0,          /* permanent until CLOSE or another command */
                .activate = true,
            };
            snprintf(r1.description, sizeof(r1.description), "KEEPOPEN relay1 permanent");
            if (relay_execute_command(&r1) == ESP_OK) {
                relay_command_t r2 = {
                    .relay_number = 2,
                    .duration_ms = 2000U,  /* 2 seconds auto-off */
                    .activate = true,
                };
                snprintf(r2.description, sizeof(r2.description), "KEEPOPEN relay2 2s");
                (void)relay_execute_command(&r2);
                ESP_LOGI(TAG, "KEEPOPEN[%d]: relay1 ON (permanent), relay2 ON for 2 s", relay_id);
                if (send_at_then_raw_data(SERVER_TCP_LINK_ID, ACK_KEEPOPEN_LEN, ACK_KEEPOPEN) == AT_RESULT_SUCCESS) {
                    ESP_LOGI(TAG, "KEEPOPEN[%d]: ACK sent (1\\r\\nB\\r\\n)", relay_id);
                    /* Special KEEPOPEN (both, then relay1 permanent): persist relay1 only. */
                    uint16_t sr = nvs_config_get_status_reg(0x0000);
                    sr |= STATUS_KEEP_RELAY1;
                    (void)nvs_config_set_status_reg(sr);
                } else {
                    ESP_LOGW(TAG, "KEEPOPEN[%d]: failed to send ACK", relay_id);
                }
            } else {
                ESP_LOGW(TAG, "KEEPOPEN[%d]: failed to queue relay1 command", relay_id);
            }
        }
        return;
    }

    /* --- CLOSE[x]: deactivate relays.
     * x = 1 or 2 → turn OFF that relay only.
     * x = 3 or no [x] → turn OFF both relays.
     * Case-insensitive: "close[1]", "Close[3]", "CLOSE" etc. are accepted.
     */
    if (strncasecmp(line, "CLOSE", 5) == 0) {
        int relay_id = 3; /* default = both */
        const char *p = line + 5;
        if (*p == '[') {
            relay_id = atoi(p + 1);
        }

        relay_command_t r = {
            .relay_number = 3,
            .duration_ms = 0,
            .activate = false,
        };
        if (relay_id == 1 || relay_id == 2) {
            r.relay_number = (uint8_t)relay_id;
        }
        snprintf(r.description, sizeof(r.description), "CLOSE[%d] command from server", relay_id);
        (void)relay_execute_command(&r);
        ESP_LOGI(TAG, "CLOSE[%d]: relay %d OFF%s",
                 relay_id,
                 r.relay_number,
                 (r.relay_number == 3) ? " (both)" : "");

        if (send_at_then_raw_data(SERVER_TCP_LINK_ID, ACK_CLOSE_LEN, ACK_CLOSE) == AT_RESULT_SUCCESS) {
            ESP_LOGI(TAG, "CLOSE[%d]: ACK sent (1\\r\\nC\\r\\n)", relay_id);
            /* Clear KEEPOPEN bits when server explicitly closes relays. */
            uint16_t sr = nvs_config_get_status_reg(0x0000);
            if (relay_id == 1) {
                sr &= ~STATUS_KEEP_RELAY1;
            } else if (relay_id == 2) {
                sr &= ~STATUS_KEEP_RELAY2;
            } else {
                sr &= (uint16_t)~(STATUS_KEEP_RELAY1 | STATUS_KEEP_RELAY2);
            }
            (void)nvs_config_set_status_reg(sr);
        } else {
            ESP_LOGW(TAG, "CLOSE[%d]: failed to send ACK", relay_id);
        }
        return;
    }

    /* TODO: parse other server commands and dispatch (e.g. relay, report, etc.) */
}

/* Normalize caller ID for CHECK_USER:
 * - If it starts with +972 (e.g. +972547917237), convert to local 0-prefixed form: 0547917237.
 * - Otherwise leave as-is.
 */
static void normalize_caller_id(char *caller) {
    if (caller == NULL) return;
    if (strncmp(caller, "+972", 4) != 0) return;

    size_t len = strnlen(caller, SERVER_RING_CALLER_MAX - 1);
    if (len <= 4) return; /* nothing useful after country code */

    char normalized[SERVER_RING_CALLER_MAX];
    snprintf(normalized, sizeof(normalized), "0%s", caller + 4);
    normalized[SERVER_RING_CALLER_MAX - 1] = '\0';
    strncpy(caller, normalized, SERVER_RING_CALLER_MAX - 1);
    caller[SERVER_RING_CALLER_MAX - 1] = '\0';
}

/**
 * @brief Buffered data notification: +CIPRXGET: link_id,param2. Request read; keepalive task will do AT+CIPRXGET=2,1 and parse.
 */
void server_on_ciprxget_urc(int link_id, int param2) {
    (void)param2;
    s_pending_read_link = link_id;
    s_pending_read = true;
    if (s_ka_task_handle != NULL) {
        xTaskNotifyGive(s_ka_task_handle);
    }
}

void server_on_ring(const char *caller_id) {
    if (caller_id == NULL) return;
    size_t n = strnlen(caller_id, SERVER_RING_CALLER_MAX - 1);
    if (n == 0 || n >= SERVER_RING_CALLER_MAX) return;
    memcpy(s_pending_ring_caller, caller_id, n + 1);
    s_pending_ring = true;
    if (s_ka_task_handle != NULL) {
        xTaskNotifyGive(s_ka_task_handle);
    }
    if (s_ring_worker_handle != NULL) {
        xTaskNotifyGive(s_ring_worker_handle);
    }
}

void server_on_ipclose(int link_id) {
    if (link_id != SERVER_TCP_LINK_ID) return;
    ESP_LOGI(TAG, "+IPCLOSE lk %d", link_id);
    if (fota_session_active()) {
        fota_modem_debug_log_snapshot("IPCLOSE(before_reset)");
    }
    fota_on_tcp_disconnected();
    s_pending_ipclose_reconnect = true;

    /* Normal case: keepalive task is running – let it handle emergency re-init. */
    if (s_ka_task_handle != NULL) {
        xTaskNotifyGive(s_ka_task_handle);
        return;
    }

    /* Special case: no keepalive task (e.g. connect/init failed earlier).
     * +IPCLOSE means we lost network – always trigger a fresh modem re-init. */
    ESP_LOGI(TAG, "+IPCLOSE: no KA, modem reinit");
    if (is_modem_init_active()) {
        stop_modem_init_task();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    if (!start_modem_init_task()) {
        ESP_LOGW(TAG, "Failed to start modem init task from server_on_ipclose");
    }
}
/** Modem lines that must never be fed as TCP payload during FOTA (URCs also go through response_queue). */
static bool fota_skip_modem_line(const char *p)
{
    return (strncmp(p, "+CIPRXGET:", 10) == 0 ||
            strncmp(p, "+CIPSEND:", 9) == 0 ||
            strncmp(p, "+IPCLOSE:", 9) == 0);
}

/* Last AT+CIPRXGET=2 outcome while sized FOTA (for stall / IPCLOSE logs). */
static char s_fota_mdm_snap[144];
static int s_fota_mdm_len = -999;
static int s_fota_mdm_unread = -999;

static void fota_mdm_snap_sanitize(char *dst, size_t dstsz, const char *src)
{
    if (dst == NULL || dstsz < 4) {
        return;
    }
    if (src == NULL) {
        snprintf(dst, dstsz, "(null)");
        return;
    }
    size_t di = 0;
    for (; *src != '\0' && di + 1 < dstsz; ++src) {
        unsigned char c = (unsigned char)*src;
        if (c >= 32 && c <= 126) {
            dst[di++] = (char)c;
        } else if (c == '\r' && di + 2 < dstsz) {
            dst[di++] = '\\';
            dst[di++] = 'r';
        } else if (c == '\n' && di + 2 < dstsz) {
            dst[di++] = '\\';
            dst[di++] = 'n';
        } else {
            dst[di++] = '.';
        }
    }
    dst[di] = '\0';
}

static void fota_mdm_snap_note_ok(int len, int unread)
{
    if (!fota_sized_body_incomplete()) {
        return;
    }
    s_fota_mdm_len = len;
    s_fota_mdm_unread = unread;
    fota_mdm_snap_sanitize(s_fota_mdm_snap, sizeof(s_fota_mdm_snap), last_response);
}

static void fota_mdm_snap_note_fail(void)
{
    if (!fota_sized_body_incomplete()) {
        return;
    }
    s_fota_mdm_len = -1;
    s_fota_mdm_unread = -1;
    fota_mdm_snap_sanitize(s_fota_mdm_snap, sizeof(s_fota_mdm_snap), last_response);
}

static void fota_modem_debug_log_snapshot(const char *reason)
{
    uint32_t rem = fota_body_bytes_remaining();
    ESP_LOGW(TAG,
             "FOTA modem [%s]: body_rem=%lu last_CIPread_len=%d modem_unread=%d AT_last=[%s]",
             reason, (unsigned long)rem, s_fota_mdm_len, s_fota_mdm_unread, s_fota_mdm_snap);
}

/**
 * One AT+CIPRXGET=2 cycle: issue command, drain lines until OK, feed FOTA/server.
 * @param out_unread if non-NULL, set to 4th field of +CIPRXGET: 2,... (bytes still in modem buffer), or 0 if absent.
 * @return false if AT command failed.
 */
static bool do_read_buffered_data_once(int link_id, int *out_unread) {
    char cmd[32];
    char line[LINE_BUFFER_SIZE];
    size_t payload_len = 0;
    int len = 0;
    int unread = 0;

    snprintf(cmd, sizeof(cmd), "AT+CIPRXGET=2,%d", link_id);
    if (send_at_command_ex(cmd, "+CIPRXGET: 2", 3000, false) != AT_RESULT_SUCCESS) {
        ESP_LOGW(TAG, "CIPRXGET=2 lk%d fail", link_id);
        fota_mdm_snap_note_fail();
        return false;
    }
    /* +CIPRXGET: 2,<link>,<this_read_len>[,<still_buffered>] — poll again while still_buffered > 0 during sized FOTA */
    {
        int n = sscanf(last_response, "+CIPRXGET: 2,%*d,%d,%d", &len, &unread);
        if (n < 1) {
            /* Do not assume 64 B on parse failure (was starving FOTA after +IP ERROR handling). */
            len = 0;
            unread = 0;
        } else if (n < 2) {
            unread = 0;
        }
    }
    fota_mdm_snap_note_ok(len, unread);
    if (out_unread != NULL) {
        *out_unread = unread;
    }
    ESP_LOGD(TAG, "Buffered payload %d bytes (modem unread %d)", len, unread);

    /* Reset KA-ACK flag for this read cycle. */
    s_ka_ack_since_read = false;

    /* When waiting for CHECK_USER response, reset result for this read. */
    if (s_waiting_check_user_response) {
        s_check_user_approved = -1;
    }

    while (get_next_response_line_ex(line, sizeof(line), &payload_len, 2000)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (p[0] == 'O' && p[1] == 'K' && (p[2] == '\0' || p[2] == ' ' || p[2] == '\t')) break;
        if (fota_session_active()) {
            if (fota_skip_modem_line(p)) {
                continue;
            }
            fota_feed_modem_payload((const uint8_t *)line, payload_len);
        } else {
            size_t lead = (size_t)(p - line);
            size_t pay = payload_len > lead ? payload_len - lead : 0;
            size_t zl = pay < sizeof(line) - 1 ? pay : sizeof(line) - 1;
            memmove(line, p, zl);
            line[zl] = '\0';
            server_handle_incoming_line(line);
        }
    }

    if (s_ka_ack_since_read) {
        ESP_LOGI(TAG, "KA OK..Unit Ready..");
        s_ka_sent_waiting_ack = false;
        s_ka_retry_pending = false;
        s_ka_send_fail_count = 0;
    }

    /* After CHECK_USER, next read contains APPROVEDxxx or REJECT; activate relay and send OPENED ack if approved. */
    if (s_waiting_check_user_response && s_check_user_approved >= 0) {
        if (s_check_user_approved == 1) {
            /* Parse APPROVED102 → relay 1 for 2s, same encoding as OPEN102 */
            int relay_num = s_approved_id / 100;      /* 102/100 = 1 */
            int duration_sec = s_approved_id % 100;   /* 102%100 = 2 */
            
            if (relay_num >= 1 && relay_num <= 3 && duration_sec > 0 && duration_sec <= 99) {
                relay_command_t rcmd = {
                    .relay_number = (uint8_t)relay_num,
                    .duration_ms = (uint32_t)duration_sec * 1000U,
                    .activate = true,
                };
                snprintf(rcmd.description, sizeof(rcmd.description), "APPROVED%d from server", s_approved_id);
                esp_err_t rre = relay_execute_gated_server_activation(&rcmd);
                if (rre == ESP_OK) {
                    ESP_LOGI(TAG, "APPROVED%d → relay %d (%ds)", s_approved_id, relay_num, duration_sec);
                    if (send_at_then_raw_data(SERVER_TCP_LINK_ID, OPENED_ACK_LEN, (const uint8_t *)OPENED_ACK) == AT_RESULT_SUCCESS) {
                        ESP_LOGI(TAG, "OPENED ack sent (id=%d)", s_approved_id);
                    } else {
                        ESP_LOGW(TAG, "OPENED ack send failed");
                    }
                } else if (rre == ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG, "APPROVED%d ignored (digital-input gate, GPIO22 not active)", s_approved_id);
                }
            }
        } else {
            ESP_LOGI(TAG, "Call rejected by server");
        }
        s_waiting_check_user_response = false;
        s_check_user_approved = -1;
        ESP_LOGI(TAG, "\033[1;32mUnit Ready\033[0m");
    }
    return true;
}

/** Max chained CIPRXGET=2 per wake (~826368/1500; cap avoids runaway). */
#define FOTA_CIPRXGET_CHAIN_MAX 600
/** After modem reports unread=0, TCP may still be pushing into the SIM buffer; short extra polls avoid RX window collapse mid-FOTA. */
#define FOTA_ZERO_UNREAD_PROBE_MS   2
#define FOTA_ZERO_UNREAD_PROBES     8

/**
 * @brief Read buffered data: AT+CIPRXGET=2,<link>, then drain payload lines until OK and pass to server_handle_incoming_line.
 * During sized FOTA, repeats CIPRXGET while modem reports unread bytes so we do not stall on a missed +CIPRXGET:1 URC.
 */
static void do_read_buffered_data(int link_id) {
    int unread = 0;
    if (!do_read_buffered_data_once(link_id, &unread)) {
        return;
    }
    for (int chain = 0;
         fota_sized_body_incomplete() && unread > 0 && chain < FOTA_CIPRXGET_CHAIN_MAX;
         ++chain) {
        if (!do_read_buffered_data_once(link_id, &unread)) {
            break;
        }
    }
    /* Only when modem says nothing left buffered: TCP may still be delivering; polling avoids zero-window stalls. */
    if (fota_sized_body_incomplete() && unread == 0) {
        for (int probe = 0; probe < FOTA_ZERO_UNREAD_PROBES && fota_sized_body_incomplete(); ++probe) {
            vTaskDelay(pdMS_TO_TICKS(FOTA_ZERO_UNREAD_PROBE_MS));
            if (!do_read_buffered_data_once(link_id, &unread)) {
                break;
            }
            for (int chain = 0;
                 fota_sized_body_incomplete() && unread > 0 && chain < FOTA_CIPRXGET_CHAIN_MAX;
                 ++chain) {
                if (!do_read_buffered_data_once(link_id, &unread)) {
                    break;
                }
            }
        }
    }
}

/** Modem-slave mode: hang up voice call, queue CHECK_USER on WiFi TCP (no modem server TCP). */
static void ring_worker_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "ring_worker: CHUP + CHECK_USER via WiFi");
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_pending_ring) {
            continue;
        }
        s_pending_ring = false;
        char caller[SERVER_RING_CALLER_MAX];
        strncpy(caller, s_pending_ring_caller, sizeof(caller) - 1);
        caller[sizeof(caller) - 1] = '\0';

        if (send_at_command_ex("AT+CHUP", "OK", 3000, true) != AT_RESULT_SUCCESS) {
            ESP_LOGW(TAG, "ring_worker: AT+CHUP failed");
        }
        wifi_tcp_request_check_user(caller);
    }
}

static void server_keepalive_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Keepalive task started: KA in %d s", SERVER_KA_FIRST_DELAY_SEC);

    /* One-shot notify after FOTA reboot (placeholder frame; replace in fota_modem.c). */
    fota_try_send_pending_success_notify();

    /* First KA after SERVER_KA_FIRST_DELAY_SEC (e.g. 8 s) */
    vTaskDelay(pdMS_TO_TICKS((uint32_t)SERVER_KA_FIRST_DELAY_SEC * 1000));
    if (!s_ka_task_running) {
        goto exit_task;
    }
    //ESP_LOGI(TAG, "First KA now (after %d s delay)", SERVER_KA_FIRST_DELAY_SEC);
    /* Same mechanism as OPENED ack: send_at_then_raw_data(link_id, len, payload) → AT+CIPSEND=1,len, ">", raw bytes */
    if (fota_session_active()) {
        /* FOTA started before first KA — do not count as send failure. */
    } else if (send_at_then_raw_data(SERVER_TCP_LINK_ID, (int)KEEP_ALIVE_LEN, KEEP_ALIVE) == AT_RESULT_SUCCESS) {
      /*   ESP_LOGI(TAG, "KEEP_ALIVE sent (%d bytes): \"1\\r\\nA\\r\\n\" (ATMEGA format)", (int)KEEP_ALIVE_LEN);
        ESP_LOGI(TAG, "KEEP_ALIVE bytes (hex): %02X %02X %02X %02X %02X %02X",
                 (unsigned)KEEP_ALIVE[0], (unsigned)KEEP_ALIVE[1], (unsigned)KEEP_ALIVE[2],
                 (unsigned)KEEP_ALIVE[3], (unsigned)KEEP_ALIVE[4], (unsigned)KEEP_ALIVE[5]); 
    */
        s_ka_sent_waiting_ack = true;
        s_ka_send_fail_count = 0;
    } else {
        s_ka_send_fail_count++;
        ESP_LOGW(TAG, "KA send failed (first, consecutive=%d)", s_ka_send_fail_count);
        if (s_ka_send_fail_count >= 2) {
            ESP_LOGW(TAG, "KA CIPSEND failed twice – starting full modem re-init");
            s_ka_sent_waiting_ack = false;
            s_ka_retry_pending = false;
            s_ka_task_running = false;

            /* Ensure any existing modem init task is stopped before starting a fresh one. */
            if (is_modem_init_active()) {
                stop_modem_init_task();
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            if (!start_modem_init_task()) {
                ESP_LOGW(TAG, "Failed to start modem init task after KA send failures");
            }
            goto exit_task;
        }
    }

    while (s_ka_task_running) {
        /* When waiting for ACK use shorter timeout so we detect "not ACKed" and retry sooner; else use 45 s for next KA. */
        uint32_t notify_wait_ms;
        if (fota_sized_body_incomplete()) {
            /* Sized image: wake often; shorter transfer wall time reduces server/proxy idle disconnects. */
            notify_wait_ms = 5u;
        } else if (fota_session_active()) {
            notify_wait_ms = 1000u;
        } else {
            uint32_t wait_sec = s_ka_sent_waiting_ack ? (uint32_t)SERVER_KA_ACK_TIMEOUT_SEC : (uint32_t)SERVER_KA_INTERVAL_SEC;
            notify_wait_ms = wait_sec * 1000u;
        }
        uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(notify_wait_ms));

        if (!s_ka_task_running) break;

        /* Ring: hang up, send CHECK_USER:<number>, then wait for next +CIPRXGET with APPROVED/REJECT */
        if (n > 0 && s_pending_ring) {
            s_pending_ring = false;
            char caller[SERVER_RING_CALLER_MAX];
            strncpy(caller, s_pending_ring_caller, SERVER_RING_CALLER_MAX - 1);
            caller[SERVER_RING_CALLER_MAX - 1] = '\0';
            normalize_caller_id(caller);

            if (send_at_command_ex("AT+CHUP", "OK", 3000, true) != AT_RESULT_SUCCESS) {
                ESP_LOGW(TAG, "AT+CHUP failed");
            } else {
                char payload[64];
                int plen = snprintf(payload, sizeof(payload), "15\r\nCHECK_USER:%s\r\n", caller);
                if (plen > 0 && (size_t)plen < sizeof(payload) &&
                    send_at_then_raw_data(SERVER_TCP_LINK_ID, (size_t)plen, (const uint8_t *)payload) == AT_RESULT_SUCCESS) {
                    ESP_LOGI(TAG, "CHECK_USER sent for caller %s (%d bytes)", caller, plen);
                    s_waiting_check_user_response = true;
                } else {
                    ESP_LOGW(TAG, "CHECK_USER send failed");
                }
            }
            continue;
        }

        /* Drain modem TCP buffer whenever URC set the flag — do not require n>0 (notify can be missed vs. flag). */
        if (s_pending_read) {
            s_pending_read = false;
            do_read_buffered_data(s_pending_read_link);
            continue;
        }

        /* Emergency handling: +IPCLOSE from server is treated as a hard failure.
         * Stop this keepalive task and start a full modem re-init/connect cycle
         * (including modem power-cycle inside modem_init_task). */
        if (n > 0 && s_pending_ipclose_reconnect) {
            s_pending_ipclose_reconnect = false;
            s_ka_sent_waiting_ack = false;
            s_ka_retry_pending = false;
            ESP_LOGI(TAG, "+IPCLOSE: KA stop, modem reinit");

            /* Stop this keepalive loop; modem_init_task will start a fresh keepalive after re-init. */
            s_ka_task_running = false;

            /* +IPCLOSE means we lost network – ensure a fresh modem re-init (stop any existing init task first). */
            if (is_modem_init_active()) {
                stop_modem_init_task();
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            if (!start_modem_init_task()) {
                ESP_LOGW(TAG, "Failed to start modem init task after +IPCLOSE");
            }
            goto exit_task;
        }
        /* n == 0: timeout (no pending read/ring). If previous KA was not ACKed, retry once after 8s or reconnect. */
        if (n == 0 && s_ka_sent_waiting_ack && !fota_session_active()) {
            if (!s_ka_retry_pending) {
                ESP_LOGI(TAG, "KA not ACKed, retrying in 8 s");
                vTaskDelay(pdMS_TO_TICKS(8000));
                if (!s_ka_task_running) break;
                if (send_at_then_raw_data(SERVER_TCP_LINK_ID, (int)KEEP_ALIVE_LEN, KEEP_ALIVE) == AT_RESULT_SUCCESS) {
                    ESP_LOGI(TAG, "KEEP_ALIVE retry sent");
                    s_ka_retry_pending = true;
                } else {
                    ESP_LOGW(TAG, "KA retry send failed");
                }
                continue;
            }
            ESP_LOGI(TAG, "KA not ACKed again, reconnecting modem to server");
            if (run_a7670e_reconnect() == 0) {
                ESP_LOGI(TAG, "Reconnect OK, resuming keepalive");
            } else {
                ESP_LOGW(TAG, "Reconnect failed");
            }
            s_ka_sent_waiting_ack = false;
            s_ka_retry_pending = false;
            continue;
        }

        if (fota_session_active()) {
            /* URC +CIPRXGET:1 is easy to miss; do_read also probes after unread=0 (see FOTA_ZERO_UNREAD_*). */
            do_read_buffered_data(s_pending_read_link);
            if (fota_sized_body_incomplete()) {
                TickType_t now = xTaskGetTickCount();
                if (s_fota_trace_period_tick == 0) {
                    s_fota_trace_period_tick = now;
                } else if ((now - s_fota_trace_period_tick) >= pdMS_TO_TICKS(15000)) {
                    s_fota_trace_period_tick = now;
                    ESP_LOGI(TAG,
                             "FOTA rx trace: rem=%lu last_CIPread_len=%d modem_unread=%d AT_last=[%s]",
                             (unsigned long)fota_body_bytes_remaining(), s_fota_mdm_len, s_fota_mdm_unread,
                             s_fota_mdm_snap);
                }
                uint32_t rem = fota_body_bytes_remaining();
                if (rem == s_fota_stall_last_remain) {
                    if (!s_fota_stall_timing) {
                        s_fota_stall_since_tick = xTaskGetTickCount();
                        s_fota_stall_timing = true;
                    } else if ((xTaskGetTickCount() - s_fota_stall_since_tick) > pdMS_TO_TICKS(40000)) {
                        ESP_LOGW(TAG,
                                 "FOTA: no progress ~40s (%lu B left) — see modem snapshot on next line",
                                 (unsigned long)rem);
                        fota_modem_debug_log_snapshot("no_progress_40s");
                        s_fota_stall_since_tick = xTaskGetTickCount();
                    }
                } else {
                    s_fota_stall_last_remain = rem;
                    s_fota_stall_timing = false;
                }
            } else {
                s_fota_stall_last_remain = UINT32_MAX;
                s_fota_stall_timing = false;
                s_fota_trace_period_tick = 0;
            }
            uint32_t poll_ms = fota_sized_body_incomplete() ? 3u : 120u;
            vTaskDelay(pdMS_TO_TICKS(poll_ms));
            continue;
        }

        if (send_at_then_raw_data(SERVER_TCP_LINK_ID, (int)KEEP_ALIVE_LEN, KEEP_ALIVE) == AT_RESULT_SUCCESS) {
           /*  ESP_LOGI(TAG, "KEEP_ALIVE sent (%d bytes): \"1\\r\\nA\\r\\n\" (ATMEGA format)", (int)KEEP_ALIVE_LEN);
            ESP_LOGI(TAG, "KEEP_ALIVE bytes (hex): %02X %02X %02X %02X %02X %02X",
                     (unsigned)KEEP_ALIVE[0], (unsigned)KEEP_ALIVE[1], (unsigned)KEEP_ALIVE[2],
                     (unsigned)KEEP_ALIVE[3], (unsigned)KEEP_ALIVE[4], (unsigned)KEEP_ALIVE[5]);
             */
             s_ka_sent_waiting_ack = true;
            s_ka_send_fail_count = 0;
        } else {
            s_ka_send_fail_count++;
            ESP_LOGW(TAG, "KA send failed (consecutive=%d)", s_ka_send_fail_count);
            if (s_ka_send_fail_count >= 2) {
                ESP_LOGW(TAG, "KA CIPSEND failed twice – starting full modem re-init");
                s_ka_sent_waiting_ack = false;
                s_ka_retry_pending = false;
                s_ka_task_running = false;

                if (is_modem_init_active()) {
                    stop_modem_init_task();
                    vTaskDelay(pdMS_TO_TICKS(300));
                }
                if (!start_modem_init_task()) {
                    ESP_LOGW(TAG, "Failed to start modem init task after KA send failures");
                }
                goto exit_task;
            }
        }
    }

exit_task:
    s_ka_task_handle = NULL;
    ESP_LOGI(TAG, "Keepalive task stopped");
    vTaskDelete(NULL);
}

bool server_keepalive_ring_worker_start(void) {
    if (s_ring_worker_handle != NULL) {
        return true;
    }
    BaseType_t ok = xTaskCreate(ring_worker_task, "ring_wk", 4096, NULL, 6, &s_ring_worker_handle);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ring_worker task");
        return false;
    }
    return true;
}

bool server_keepalive_task_start(void) {
    if (s_ka_task_handle != NULL) {
        ESP_LOGW(TAG, "Keepalive task already running");
        return true;
    }
    s_ka_task_running = true;
    /* line[LINE_BUFFER_SIZE] in do_read_buffered_data — need headroom after LINE_BUFFER_SIZE=1600 */
    BaseType_t ok = xTaskCreate(server_keepalive_task, "srv_ka", 8192, NULL, 5, &s_ka_task_handle);
    if (ok != pdPASS) {
        s_ka_task_running = false;
        ESP_LOGE(TAG, "Failed to create keepalive task");
        return false;
    }
    return true;
}

void server_keepalive_task_stop(void) {
    s_ka_task_running = false;
    /* Task will exit on next loop and delete itself; no need to delete here unless you want to block */
}
