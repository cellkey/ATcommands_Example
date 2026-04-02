/**
 * @file a7670e_sequences.c
 * @brief A7670E modem init and network sequences (table-driven)
 *
 * Command order and responses per SIMCOM_A7670E_MODEM_INIT&TCP_CONNECTION.log.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "a7670e_sequences.h"
#include "a7670e_config.h"
#include "nvs_config.h"
#include "at_command_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "A7670E";

/** Scan binary-safe for HTTP status substring. */
static bool payload_has_200_ok(const char *buf, size_t len)
{
    static const char pat[] = "200 OK";
    const size_t plen = sizeof(pat) - 1u;
    if (buf == NULL || len < plen) {
        return false;
    }
    for (size_t i = 0; i + plen <= len; ++i) {
        if (memcmp(buf + i, pat, plen) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Wait for HTTP "200 OK" while TCP is in modem buffer (CIPRXGET=1).
 * Avoids AT+CIPRXGET=0 + wait_for_line_containing, which pushes server data as +IPD on the UART
 * and fills the AT response_queue when the server spams (e.g. stale FOTA file after reboot).
 */
static at_result_t wait_for_http_200_via_ciprxget(int link_id, uint32_t timeout_ms)
{
    char cmd[40];
    char line[LINE_BUFFER_SIZE];
    size_t payload_len = 0;
    TickType_t start = xTaskGetTickCount();
    const TickType_t tmo = pdMS_TO_TICKS(timeout_ms);

    snprintf(cmd, sizeof(cmd), "AT+CIPRXGET=2,%d", link_id);

    while ((xTaskGetTickCount() - start) < tmo) {
        if (send_at_command_ex(cmd, "+CIPRXGET: 2", 3000, false) != AT_RESULT_SUCCESS) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        bool saw_200 = false;
        while (get_next_response_line_ex(line, sizeof(line), &payload_len, 500)) {
            const char *p = line;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (p[0] == 'O' && p[1] == 'K' && (p[2] == '\0' || p[2] == ' ' || p[2] == '\t')) {
                break;
            }
            if (payload_has_200_ok(line, payload_len)) {
                saw_200 = true;
            }
        }
        if (saw_200) {
            return AT_RESULT_SUCCESS;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return AT_RESULT_TIMEOUT;
}

/* Device info collected for GET part 2 (81 bytes) */
typedef struct {
    char fw_version[32];
    char cops_id[24];
    int  csq;
    char sim_id[32];
} a7670e_device_info_t;

static int collect_device_info(a7670e_device_info_t *info) {
    /* Do NOT blindly zero existing info here; allow partial updates.
     * Each command tries to fill its own field; failures leave previous/default
     * contents intact. We return 0 if at least one field was collected.
     */
    bool any_ok = false;
    const char *p = NULL;

    if (send_at_command_ex("AT+CGMR", "+CGMR:", 3000, false) == AT_RESULT_SUCCESS) {
        const char *src = strstr(last_matched_response, "+CGMR:") ? last_matched_response : last_response;
        p = strstr(src, "+CGMR:");
        if (p) {
            p += 6;
            while (*p == ' ') p++;
            snprintf(info->fw_version, sizeof(info->fw_version), "%.24s", p);
            any_ok = true;
        }
    }

    if (send_at_command_ex("AT+COPS?", "+COPS:", 5000, false) == AT_RESULT_SUCCESS) {
        const char *src = strstr(last_matched_response, "+COPS:") ? last_matched_response : last_response;
        p = strchr(src, '"');
        if (p) {
            p++;
            const char *q = strchr(p, '"');
            if (q) {
                snprintf(info->cops_id, sizeof(info->cops_id), "%.*s****", (int)(q - p), p);
                any_ok = true;
            }
        }
    }

    if (send_at_command_ex("AT+CSQ", "+CSQ:", 3000, false) == AT_RESULT_SUCCESS) {
        const char *src = strstr(last_matched_response, "+CSQ:") ? last_matched_response : last_response;
        p = strstr(src, "+CSQ:");
        if (p) {
            p += 5;
            info->csq = atoi(p);
            any_ok = true;
        }
    }

    if (send_at_command_ex("AT+CICCID", "+ICCID:", 3000, false) == AT_RESULT_SUCCESS) {
        const char *src = strstr(last_matched_response, "+ICCID:") ? last_matched_response : last_response;
        p = strstr(src, "+ICCID:");
        if (p) {
            p += 7;
            while (*p == ' ') p++;
            snprintf(info->sim_id, sizeof(info->sim_id), "%.24s", p);
            any_ok = true;
        }
    }

    return any_ok ? 0 : -1;
}

/**
 * @brief Wait for network registration using AT+CREG?.
 *
 * Modem may report:
 *   +CREG: 0,2  (searching)
 *   +CREG: 0,3  (registration denied)
 *   +CREG: 0,0/0,4/... (not registered)
 *
 * We only continue when stat is 1 (home) or 5 (roaming). Otherwise we retry
 * AT+CREG? every `delay_sec` seconds, up to `max_tries`. If we never see
 * 0,1 or 0,5 we treat init as failed and do NOT continue to next steps.
 */
static int wait_for_network_registration(int max_tries, int delay_sec)
{
    const char *cmd = "AT+CREG?";

    for (int attempt = 1; attempt <= max_tries; ++attempt) {
        if (send_at_command_ex(cmd, "+CREG:", 5000, false) != AT_RESULT_SUCCESS) {
            ESP_LOGW(TAG, "AT+CREG? attempt %d/%d failed (no +CREG:), retrying in %d s",
                     attempt, max_tries, delay_sec);
        } else {
            int n = 0, stat = 0;
            /* Use last_matched_response: process_line overwrites last_response with "OK" before we read. */
            const char *src = strstr(last_matched_response, "+CREG:") ? last_matched_response : last_response;
            const char *p = strstr(src, "+CREG:");
            if (p != NULL) {
                p += 6; /* skip "+CREG:" */
                while (*p == ' ' || *p == '\t') p++;
                /* Parse <n>,<stat> */
                if (sscanf(p, "%d,%d", &n, &stat) == 2) {
                    ESP_LOGI(TAG, "AT+CREG? response: +CREG: %d,%d (attempt %d/%d)",
                             n, stat, attempt, max_tries);
                    if (stat == 1 || stat == 5) {
                        ESP_LOGI(TAG, "Network registered (stat=%d) – continuing init", stat);
                        return 0;
                    }
                } else {
                    ESP_LOGW(TAG, "AT+CREG? parse failed for line: %s", src);
                }
            } else {
                ESP_LOGW(TAG, "AT+CREG? got +CREG: but could not find line in last_matched_response/last_response");
            }
            ESP_LOGI(TAG, "Not registered yet (need +CREG: 0,1 or 0,5). Retrying in %d s", delay_sec);
        }

        vTaskDelay(pdMS_TO_TICKS((uint32_t)delay_sec * 1000));
    }

    ESP_LOGE(TAG, "Network registration failed: no +CREG: 0,1 or 0,5 after %d attempts", max_tries);
    return -1;
}

/**
 * @brief Basic AT test with retries: ensure modem responds with OK.
 */
static int wait_for_basic_at(int max_tries, int delay_sec)
{
    for (int attempt = 1; attempt <= max_tries; ++attempt) {
        if (send_at_command("AT", "OK", 2000) == AT_RESULT_SUCCESS) {
            ESP_LOGI(TAG, "Basic AT test OK (attempt %d/%d)", attempt, max_tries);
            return 0;
        }
        ESP_LOGW(TAG, "Basic AT test failed (attempt %d/%d), retrying in %d s",
                 attempt, max_tries, delay_sec);
        vTaskDelay(pdMS_TO_TICKS((uint32_t)delay_sec * 1000));
    }
    ESP_LOGE(TAG, "Basic AT test failed after %d attempts", max_tries);
    return -1;
}

/**
 * @brief Signal quality check with retries. Treat +CSQ: 99,99 as \"no signal yet\" and retry.
 */
static int wait_for_signal_quality(int max_tries, int delay_sec)
{
    const char *cmd = "AT+CSQ";

    for (int attempt = 1; attempt <= max_tries; ++attempt) {
        if (send_at_command_ex(cmd, "+CSQ:", 3000, false) != AT_RESULT_SUCCESS) {
            ESP_LOGW(TAG, "AT+CSQ attempt %d/%d failed (no +CSQ:), retrying in %d s",
                     attempt, max_tries, delay_sec);
        } else {
            int rssi = 0, qual = 0;
            const char *src = strstr(last_matched_response, "+CSQ:") ? last_matched_response : last_response;
            const char *p = strstr(src, "+CSQ:");
            if (p != NULL) {
                p += 5; /* skip \"+CSQ:\" */
                while (*p == ' ' || *p == '\t') p++;
                if (sscanf(p, "%d,%d", &rssi, &qual) == 2) {
                    ESP_LOGI(TAG, "AT+CSQ response: +CSQ: %d,%d (attempt %d/%d)",
                             rssi, qual, attempt, max_tries);
                    if (rssi == 99 && qual == 99) {
                        ESP_LOGI(TAG, "Signal not ready yet (+CSQ: 99,99). Retrying in %d s", delay_sec);
                    } else {
                        ESP_LOGI(TAG, "Signal OK (+CSQ: %d,%d) – continuing init", rssi, qual);
                        return 0;
                    }
                } else {
                    ESP_LOGW(TAG, "AT+CSQ parse failed for line: %s", src);
                }
            } else {
                ESP_LOGW(TAG, "AT+CSQ got +CSQ: but could not find line in last_matched_response/last_response");
            }
        }

        vTaskDelay(pdMS_TO_TICKS((uint32_t)delay_sec * 1000));
    }

    ESP_LOGE(TAG, "Signal quality check failed: +CSQ not usable after %d attempts", max_tries);
    return -1;
}

/**
 * @brief Ensure network is open using AT+NETOPEN? and, if needed, AT+NETOPEN with retries.
 *
 * Spec (Project AT_command_params.txt):
 *   AT+NETOPEN?   → +NETOPEN: 1  (already open) – no action
 *                  if +NETOPEN: 0 → issue AT+NETOPEN
 *   AT+NETOPEN    → +NETOPEN: 0  (open ok), up to 2 trials, 2 s apart.
 */
static int ensure_network_open(void)
{
    int status = -1;

    /* 1) Query current NETOPEN status. */
    if (send_at_command_ex("AT+NETOPEN?", "+NETOPEN:", 10000, false) == AT_RESULT_SUCCESS) {
        const char *src = strstr(last_matched_response, "+NETOPEN:") ? last_matched_response : last_response;
        const char *p = strstr(src, "+NETOPEN:");
        if (p) {
            p += 9; /* skip \"+NETOPEN:\" */
            while (*p == ' ' || *p == '\t') p++;
            if (sscanf(p, "%d", &status) == 1) {
                ESP_LOGI(TAG, "AT+NETOPEN? response: +NETOPEN: %d", status);
                if (status == 1) {
                    ESP_LOGI(TAG, "Network already open (+NETOPEN: 1)");
                    return 0;
                }
            } else {
                ESP_LOGW(TAG, "AT+NETOPEN? parse failed for line: %s", src);
            }
        } else {
            ESP_LOGW(TAG, "AT+NETOPEN? got +NETOPEN: but could not find it in last_matched_response/last_response");
        }
    } else {
        ESP_LOGW(TAG, "AT+NETOPEN? failed (no +NETOPEN:), will try to open network anyway");
    }

    ESP_LOGI(TAG, "Network not open (status=%d). Trying AT+NETOPEN up to 2 times", status);

    /* 2) Try to open network with AT+NETOPEN, expecting +NETOPEN: 0, up to 2 trials. */
    for (int attempt = 1; attempt <= 2; ++attempt) {
        if (send_at_command_ex("AT+NETOPEN", "+NETOPEN:", 15000, false) == AT_RESULT_SUCCESS) {
            const char *src = strstr(last_matched_response, "+NETOPEN:") ? last_matched_response : last_response;
            const char *p = strstr(src, "+NETOPEN:");
            int open_status = -1;
            if (p) {
                p += 9;
                while (*p == ' ' || *p == '\t') p++;
                if (sscanf(p, "%d", &open_status) == 1) {
                    ESP_LOGI(TAG, "AT+NETOPEN response: +NETOPEN: %d (attempt %d/2)", open_status, attempt);
                    if (open_status == 0) {
                        ESP_LOGI(TAG, "Network open OK (+NETOPEN: 0)");
                        return 0;
                    }
                    ESP_LOGW(TAG, "AT+NETOPEN returned unexpected status %d", open_status);
                } else {
                    ESP_LOGW(TAG, "AT+NETOPEN parse failed for line: %s", src);
                }
            } else {
                ESP_LOGW(TAG, "AT+NETOPEN got +NETOPEN: but could not find it in last_matched_response/last_response");
            }
        } else {
            ESP_LOGW(TAG, "AT+NETOPEN attempt %d/2 failed (no +NETOPEN:)", attempt);
        }

        if (attempt < 2) {
            ESP_LOGI(TAG, "Retrying AT+NETOPEN in 2 s...");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    ESP_LOGE(TAG, "Network open failed: AT+NETOPEN did not yield +NETOPEN: 0 after 2 attempts");
    return -1;
}

/* Phase 1: Basic init (AT → … → CGDCONT). Order matches reference log. */
static const at_command_def_t a7670e_init_phase1[] = {
    { "AT",              "OK",       2000, true,  true,  "Basic AT test",                    "Check modem power/UART" },
    { "AT+CSQ",          "+CSQ:",    3000, true,  true,  "Signal quality",                   "Check antenna/SIM" },
    { "AT&F;E0",         "OK",       2000, true,  false, "Factory defaults + echo off",     "Retry or check modem" },
    { "AT+CFUN=1",       "OK",       5000, true,  true,  "Full functionality",               "Check SIM/power" },
    { "AT+CLIP=1",       "OK",       2000, true,  false, "Caller ID on",                     "Optional" },
    { "AT+CGMR",         "+CGMR:",   3000, true,  false, "Firmware version (for GET part 2)", "Optional" },
    { "AT+CGSN",         "OK",       3000, true,  false, "IMEI (for GET part 2)",            "Optional" },
    { "AT+CIPCCFG?",     "+CIPCCFG:", 2000, true,  false, "CIP config query",                "Can be ignored" },
    { "AT+CIPRXGET=0",   "OK",       2000, true,  false, "Non-buffered URC during init",    "Optional" },
    { "AT+COPS?",        "+COPS:",   5000, true,  false, "Operator (for GET part 2)",        "Optional" },
    { "AT+CICCID",       "+ICCID:",  3000, true,  false, "SIM ID (for GET part 2)",         "Optional" },
    { "AT+CGATT?",       "+CGATT:",  3000, true,  true,  "GPRS attach",                     "Wait for attach" },
    { "AT+CGACT=1,1",    "OK",       15000, true, true,  "Activate PDP context 1",           "Check APN/operator" },
    { "AT+CIPMODE?",     "+CIPMODE:", 2000, true, false, "CIP mode (non-transparent)",       "Optional" },
    { "AT+CGDCONT=1,\"IP\",\"internet\"", "OK", 5000, true, true, "PDP context 1 IP internet", "Check APN" },
};

/* Phase 2: Network open. We now use ensure_network_open() based on AT+NETOPEN? / AT+NETOPEN logic.
 * This table is only used for IPADDR (optional). */
static const at_command_def_t a7670e_init_phase2[] = {
    { "AT+IPADDR",       "+IPADDR:", 5000, true,  false, "Get IP address",                  "Optional" },
};

#define PHASE1_LEN  (sizeof(a7670e_init_phase1) / sizeof(a7670e_init_phase1[0]))
#define PHASE2_LEN  (sizeof(a7670e_init_phase2) / sizeof(a7670e_init_phase2[0]))

int run_a7670e_init(void)
{
    ESP_LOGI(TAG, "A7670E init: phase 1 (%d commands + CREG wait), then phase 2", (int)PHASE1_LEN);

    /* Basic AT + CSQ handled with explicit retry logic (see Project AT_command_params.txt). */
    if (wait_for_basic_at(5, 3) != 0) {
        ESP_LOGE(TAG, "A7670E init: basic AT test failed – aborting init");
        return -1;
    }
    if (wait_for_signal_quality(15, 3) != 0) {
        ESP_LOGE(TAG, "A7670E init: signal quality check failed – aborting init");
        return -1;
    }

    /* Explicit network registration wait using AT+CREG? with retries. */
    if (wait_for_network_registration(A7670E_CREG_MAX_TRIES, A7670E_CREG_RETRY_DELAY_SEC) != 0) {
        ESP_LOGE(TAG, "Network registration did not reach +CREG: 0,1 or 0,5 – aborting init");
        return -1;
    }

    /* Run remaining phase1 commands except last (CGDCONT); we send CGDCONT with NVS APN below.
     * Skip first two entries (AT, CSQ) – already handled above. */
    int remaining = (int)(PHASE1_LEN - 1) - 2; /* total-1 (skip CGDCONT) minus 2 already run */
    int r1_tail = 0;
    if (remaining > 0) {
        r1_tail = execute_command_sequence(&a7670e_init_phase1[2], remaining, "A7670E_INIT_P1_TAIL");
        if (r1_tail < 0) {
            ESP_LOGE(TAG, "A7670E init phase 1 (tail) failed (critical error)");
            return -1;
        }
        if (r1_tail < remaining) {
            ESP_LOGW(TAG, "A7670E init phase 1 (tail): %d/%d OK (non-critical failures)", r1_tail, remaining);
        }
    }
    int r1_total = 2 + r1_tail; /* count AT + CSQ as successful when we reach here */

    /* PDP context with fixed APN ("internet"). NVS key 'apn' no longer used. */
    const char *apn = "internet";
    char cgdcont_cmd[96];
    snprintf(cgdcont_cmd, sizeof(cgdcont_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    if (send_at_command(cgdcont_cmd, "OK", 5000) != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "CGDCONT (APN %s) failed", apn);
        return -1;
    }
    ESP_LOGI(TAG, "PDP context APN: %s", apn);

    /* Phase 2: ensure network is open (AT+NETOPEN? / AT+NETOPEN) and optionally get IP address. */
    int r2 = 0;
    if (ensure_network_open() != 0) {
        ESP_LOGE(TAG, "A7670E init phase 2 failed: network not open");
        return -1;
    }

    /* IPADDR is optional; failure does not abort init. */
    r2 = execute_command_sequence(a7670e_init_phase2, PHASE2_LEN, "A7670E_NETWORK");
    if (r2 < 0) {
        ESP_LOGW(TAG, "A7670E IPADDR query failed (non-critical)");
        r2 = 0;
    } else if (r2 < (int)PHASE2_LEN) {
        ESP_LOGW(TAG, "A7670E init phase 2 (IPADDR): %d/%d OK", r2, (int)PHASE2_LEN);
    }

    ESP_LOGI(TAG, "A7670E init complete: phase1 %d/%d (CREG gated), phase2 %d/%d",
             r1_total, (int)PHASE1_LEN, r2, (int)PHASE2_LEN);
    return r1_total;
}

/* GET buffer: request line + Transfer-Encoding + device info (x2), no padding. */
#define GET_BUF_SIZE  320

/**
 * @brief Connect to server, send GET (exact length, no padding) in one CIPSEND, wait for 200 OK, set CIPRXGET=1.
 * Call after run_a7670e_init(). Returns 0 on success.
 */
int run_a7670e_connect_and_register(void) {
    char cmd[128];
    char get_msg[GET_BUF_SIZE];
    size_t get_len;
    a7670e_device_info_t info;
    char unit_id[NVS_CONFIG_MAX_LEN];
    char fw_ver[32];
    const char *host = A7670E_SERVER_HOST;
    const int port = A7670E_SERVER_PORT;

    /* unit_id and fw_ver from NVS:
     *  - unit_id: single source for GET path and BLE advertising name.
     *  - fw_ver: if NVS key 'fw_ver' is not set, fall back to compile-time A7670E_FW_VERSION.
     */
    nvs_config_get_string(NVS_KEY_UNIT_ID, unit_id, sizeof(unit_id), A7670E_UNIT_ID);
    nvs_config_get_string(NVS_KEY_FW_VER, fw_ver, sizeof(fw_ver), A7670E_FW_VERSION);

    ESP_LOGI(TAG, "GET will use unit_id=%s fw_ver=%s (NVS with default from a7670e_config.h)", unit_id, fw_ver);
    if (strncmp(unit_id, "cr", 2) != 0)
        ESP_LOGW(TAG, "unit_id missing \"cr\" prefix; use SET unit_id=cr18061950 then REBOOT for correct ID");

    /* Give network a moment after NETOPEN before opening TCP */
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* Collect device info before CIPOPEN so we don't send AT traffic between CIPOPEN and CIPSEND.
     * info is zero-initialized; collect_device_info will fill whatever it can and leave the rest.
     */
    memset(&info, 0, sizeof(info));
    if (collect_device_info(&info) != 0) {
        ESP_LOGW(TAG, "Device info collect had errors, using defaults/partial values");
    }

    ESP_LOGI(TAG, "Connecting to %s:%d ...", host, port);
    snprintf(cmd, sizeof(cmd), "AT+CIPOPEN=1,\"TCP\",\"%s\",%d", host, port);
    /* Reference: modem sends OK then +CIPOPEN: 1,0 (success) or +CIPOPEN: 1,11 (fail). Wait for +CIPOPEN: URC. */
    if (send_at_command_ex(cmd, "+CIPOPEN:", A7670E_CIPOPEN_TIMEOUT_MS, false) != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "CIPOPEN failed (timeout or no +CIPOPEN: response)");
        ESP_LOGE(TAG, "Check unit ID (a7670e_config.h), modem data, DNS; or try server IP");
        return -1;
    }
    if (strstr(last_response, ",0") == NULL) {
        ESP_LOGE(TAG, "CIPOPEN failed: modem returned %s (need +CIPOPEN: 1,0 for success)", last_response);
        ESP_LOGE(TAG, "Check unit ID (a7670e_config.h), modem data, DNS; or try server IP");
        return -1;
    }
    ESP_LOGI(TAG, "CIPOPEN success (+CIPOPEN: 1,0). Link ready for GET.");
    vTaskDelay(pdMS_TO_TICKS(300));  /* short settle before CIPSEND */

    /* Buffered RX *before* GET so server data (incl. huge stale bodies) stays in modem RAM, not +IPD on UART. */
    if (send_at_command("AT+CIPRXGET=1", "OK", 2000) != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "CIPRXGET=1 failed — cannot avoid UART flood on large TCP RX");
        return -1;
    }
    ESP_LOGI(TAG, "CIPRXGET=1 (buffered) before GET");

    /* Build full GET: unit_id and fw_ver from NVS (defaults from a7670e_config.h). */
    int n = snprintf(get_msg, sizeof(get_msg),
                     "GET /api/device/%s/listen HTTP/1.1\r\n"
                     "Transfer-Encoding: chunked\r\n"
                     "FW_VERSION: %s\r\n"
                     "COPS_ID: %s\r\n"
                     "CSQ: %d\r\n"
                     "SIM_ID: %s\r\n"
                     "\r\n",
                     unit_id,
                     fw_ver,
                     info.cops_id[0] ? info.cops_id : "0",
                     info.csq,
                     info.sim_id[0] ? info.sim_id : "0");
    if (n < 0 || (size_t)n >= sizeof(get_msg)) {
        ESP_LOGE(TAG, "GET message too long");
        return -1;
    }
    /* snprintf return value = exact byte count of final string (no null); use for AT+CIPSEND=1,len */
    get_len = (size_t)n;

    /* Debug: log GET request string (CRLF shown as \r\n in log) */
    ESP_LOGI(TAG, "GET payload (%u bytes): %.*s", (unsigned)get_len, (int)get_len, get_msg);

    ESP_LOGI(TAG, "GET (%u bytes) in one CIPSEND", (unsigned)get_len);
    if (send_at_then_raw_data(1, (int)get_len, (const uint8_t *)get_msg) != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "GET send failed");
        return -1;
    }
    ESP_LOGI(TAG, "GET (%u bytes) sent", (unsigned)get_len);

    if (wait_for_http_200_via_ciprxget(1, A7670E_WAIT_200_OK_MS) != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "Did not receive HTTP 200 OK (CIPRXGET=2 poll)");
        return -1;
    }
    ESP_LOGI(TAG, "***Got 200 OK from server***");

    ESP_LOGI(TAG, "In main loop..ready");
    return 0;
}

/**
 * @brief Close TCP link 1 and connect again (CIPOPEN + GET + 200 OK + CIPRXGET=1).
 * Use when KA is not ACKed after retry (e.g. after server_keepalive 8s retry).
 * @return 0 on success, -1 on failure.
 */
int run_a7670e_reconnect(void) {
    if (send_at_command("AT+CIPCLOSE=1", "OK", 5000) != AT_RESULT_SUCCESS) {
        ESP_LOGW(TAG, "CIPCLOSE=1 failed or timeout (continuing)");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    return run_a7670e_connect_and_register();
}
