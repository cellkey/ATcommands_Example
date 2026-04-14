/**
 * @file fota_url_download_test.c
 * @brief After server "FOTA": HTTP(S) full-body download check only (no esp_ota_*, no START_FOTA).
 *
 * Path selection:
 *   - WiFi STA connected → esp_http_client (robust TLS + binary stream).
 *   - Else modem-capable build (not WiFi-only) → SIMCOM AT+HTTP* over cellular (no WiFi required).
 */

#include "fota_url_download_test.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "a7670e_config.h"
#include "at_command_api.h"
#include "nvs_config.h"
#include "wifi_manager.h"

static const char *TAG = "FOTA_URL";

static volatile bool s_busy;

bool fota_url_download_test_is_busy(void)
{
    return s_busy;
}

#if FOTA_URL_DOWNLOAD_TEST_ENABLE

#define FOTA_HTTPREAD_CHUNK  1200

/** CRC-32 = zlib crc32() / Python binascii.crc32(& 0xffffffff): pass 0 on first chunk, chain return value. */
static uint32_t crc32_zlib_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1u) ^ (0xEDB88320u & ((crc & 1u) ? 0xFFFFFFFFu : 0u));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void log_and_check_crc(uint32_t crc, int nbytes)
{
    ESP_LOGI(TAG, "CRC-32 of %d B = 0x%08x", nbytes, (unsigned int)crc);
    if (FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT == 0u) {
        ESP_LOGI(TAG, "CRC compare disabled (expected=0): download-only OK if HTTP 200 and full body read");
    } else if (crc == FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT) {
        ESP_LOGI(TAG, "CRC-32 MATCH expected 0x%08x",
                 (unsigned int)FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT);
    } else {
        ESP_LOGE(TAG, "CRC-32 MISMATCH expected 0x%08x got 0x%08x",
                 (unsigned int)FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT, (unsigned int)crc);
    }
}

static esp_err_t download_via_wifi(const char *url, int *out_total, bool *magic_ok, uint32_t *out_crc)
{
    *out_total = 0;
    *magic_ok = false;
    *out_crc = 0;
    uint32_t crc = 0;

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 120000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_http_client_open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int64_t hdr_len = esp_http_client_fetch_headers(client);
    int code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "[WiFi] HTTP status=%d, content_length=%" PRId64, code, hdr_len);

    if (code < 200 || code >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    uint8_t buf[2048];
    bool first = true;
    int total = 0;
    for (;;) {
        int r = esp_http_client_read(client, (char *)buf, sizeof(buf));
        if (r < 0) {
            err = ESP_FAIL;
            break;
        }
        if (r == 0) {
            break;
        }
        if (first && r > 0) {
            *magic_ok = (buf[0] == 0xE9);
            ESP_LOGI(TAG, "[WiFi] first byte 0x%02x %s", buf[0], *magic_ok ? "(0xE9 OK)" : "(not 0xE9)");
            first = false;
        }
        crc = crc32_zlib_update(crc, buf, (size_t)r);
        total += r;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (hdr_len >= 0 && (int64_t)total != hdr_len) {
        ESP_LOGW(TAG, "[WiFi] read %d vs Content-Length %" PRId64, total, hdr_len);
    }

    *out_total = total;
    *out_crc = crc;
    if (err == ESP_OK && total > 0 && FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT != 0u &&
        crc != FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT) {
        return ESP_FAIL;
    }
    return (err == ESP_OK && total > 0) ? ESP_OK : ESP_FAIL;
}

static void modem_http_ssl_bootstrap(void)
{
    /* Best-effort HTTPS on SIMCOM; exact keys vary by firmware — tune if handshake fails. */
    (void)send_at_command_ex("AT+CSSLCFG=\"sslversion\",0,3", "OK", 8000, true);
    (void)send_at_command_ex("AT+CSSLCFG=\"authmode\",0,0", "OK", 8000, true);
    (void)send_at_command_ex("AT+CSSLCFG=\"ignorelocaltime\",0,1", "OK", 8000, true);
    (void)send_at_command_ex("AT+HTTPPARA=\"SSLCFG\",0", "OK", 8000, true);
}

static esp_err_t download_via_modem_http(const char *url, int *out_total, bool *magic_ok, uint32_t *out_crc)
{
    *out_total = 0;
    *magic_ok = false;
    *out_crc = 0;
    uint32_t crc = 0;

    char cmd[MAX_AT_COMMAND_LEN];

    (void)send_at_command_ex("AT+HTTPTERM", "OK", 5000, true);

    if (send_at_command_ex("AT+HTTPINIT", "OK", 15000, true) != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "[modem] HTTPINIT failed");
        (void)send_at_command_ex("AT+HTTPTERM", "OK", 3000, true);
        return ESP_FAIL;
    }

    (void)send_at_command_ex("AT+HTTPPARA=\"CID\",1", "OK", 8000, true);

    if (strncmp(url, "https://", 8) == 0) {
        modem_http_ssl_bootstrap();
    }

    int n = snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
    if (n <= 0 || n >= (int)sizeof(cmd)) {
        ESP_LOGE(TAG, "[modem] URL too long for AT buffer (%d)", n);
        (void)send_at_command_ex("AT+HTTPTERM", "OK", 3000, true);
        return ESP_ERR_INVALID_SIZE;
    }

    if (send_at_command_ex(cmd, "OK", 15000, true) != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "[modem] HTTPPARA URL failed");
        (void)send_at_command_ex("AT+HTTPTERM", "OK", 3000, true);
        return ESP_FAIL;
    }

    if (send_at_command_ex("AT+HTTPACTION=0", "+HTTPACTION:", 180000, false) != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "[modem] HTTPACTION timeout");
        (void)send_at_command_ex("AT+HTTPTERM", "OK", 3000, true);
        return ESP_FAIL;
    }

    int http_st = 0;
    int data_len = 0;
    if (sscanf(last_matched_response, "+HTTPACTION: %*d,%d,%d", &http_st, &data_len) != 2) {
        ESP_LOGE(TAG, "[modem] parse HTTPACTION: [%s]", last_matched_response);
        (void)send_at_command_ex("AT+HTTPTERM", "OK", 3000, true);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[modem] HTTPACTION status=%d len=%d", http_st, data_len);
    if (http_st != 200 || data_len <= 0) {
        (void)send_at_command_ex("AT+HTTPTERM", "OK", 3000, true);
        return ESP_FAIL;
    }

    int total = 0;
    bool checked_magic = false;

    for (int offset = 0; offset < data_len; ) {
        int want = data_len - offset;
        if (want > FOTA_HTTPREAD_CHUNK) {
            want = FOTA_HTTPREAD_CHUNK;
        }

        n = snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=%d,%d", offset, want);
        if (n <= 0 || n >= (int)sizeof(cmd)) {
            break;
        }

        if (send_at_command_ex(cmd, "+HTTPREAD:", 120000, false) != AT_RESULT_SUCCESS) {
            ESP_LOGE(TAG, "[modem] HTTPREAD fail at offset %d", offset);
            (void)send_at_command_ex("AT+HTTPTERM", "OK", 3000, true);
            return ESP_FAIL;
        }

        int read_n = 0;
        if (sscanf(last_matched_response, "+HTTPREAD: %d", &read_n) != 1 || read_n < 0) {
            ESP_LOGE(TAG, "[modem] parse HTTPREAD: [%s]", last_matched_response);
            (void)send_at_command_ex("AT+HTTPTERM", "OK", 3000, true);
            return ESP_FAIL;
        }

        size_t got = 0;
        while (got < (size_t)read_n) {
            char line[LINE_BUFFER_SIZE];
            size_t len = 0;
            if (!get_next_response_line_ex(line, sizeof(line), &len, 10000)) {
                ESP_LOGE(TAG, "[modem] body fragment timeout got=%u need=%u", (unsigned)got, (unsigned)read_n);
                (void)send_at_command_ex("AT+HTTPTERM", "OK", 3000, true);
                return ESP_FAIL;
            }
            size_t need = (size_t)read_n - got;
            size_t take = len < need ? len : need;
            if (len > need) {
                ESP_LOGW(TAG, "[modem] RX line %u B > need %u B (0x0A in .bin splits UART lines)", (unsigned)len,
                         (unsigned)need);
            }
            if (!checked_magic && take > 0 && total == 0) {
                *magic_ok = ((uint8_t)line[0] == 0xE9);
                ESP_LOGI(TAG, "[modem] first payload byte 0x%02x %s", (uint8_t)line[0],
                         *magic_ok ? "(0xE9 OK)" : "(not 0xE9)");
                checked_magic = true;
            }
            crc = crc32_zlib_update(crc, (const uint8_t *)line, take);
            got += take;
            total += (int)take;
        }

        /* Trailing OK after payload */
        {
            char line[LINE_BUFFER_SIZE];
            (void)get_next_response_line(line, sizeof(line), 3000);
        }

        offset += read_n;
    }

    (void)send_at_command_ex("AT+HTTPTERM", "OK", 8000, true);

    if (total != data_len) {
        ESP_LOGW(TAG, "[modem] read %d vs expected %d", total, data_len);
    }

    *out_total = total;
    *out_crc = crc;
    ESP_LOGI(TAG, "[modem] FOTA URL test done — %d bytes (no firmware write)", total);
    if (total > 0 && FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT != 0u && crc != FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT) {
        return ESP_FAIL;
    }
    return (total > 0) ? ESP_OK : ESP_FAIL;
}

static void url_test_task(void *arg)
{
    (void)arg;
    const char *url = FOTA_URL_DOWNLOAD_TEST_URL;

    int total = 0;
    bool magic = false;
    uint32_t crc = 0;
    esp_err_t e = ESP_FAIL;

    if (wifi_manager_is_connected()) {
        ESP_LOGI(TAG, "Using WiFi for URL test");
        e = download_via_wifi(url, &total, &magic, &crc);
    } else if (nvs_config_connectivity_mode() != NVS_CONN_WIFI_ONLY) {
        ESP_LOGI(TAG, "Using modem AT+HTTP for URL test (no WiFi)");
        e = download_via_modem_http(url, &total, &magic, &crc);
    } else {
        ESP_LOGE(TAG, "WiFi-only mode and STA not connected — cannot run URL test");
    }

    if (total > 0) {
        log_and_check_crc(crc, total);
    }

    if (e == ESP_OK) {
        ESP_LOGI(TAG, "URL test OK: full download %d bytes, first byte 0xE9=%s", total, magic ? "yes" : "no");
    } else {
        ESP_LOGW(TAG, "URL test failed (HTTP/read/timeout or CRC mismatch when expected CRC != 0)");
    }

    s_busy = false;
    vTaskDelete(NULL);
}

#endif /* FOTA_URL_DOWNLOAD_TEST_ENABLE */

bool fota_url_download_test_try_handle_invite(void)
{
#if !FOTA_URL_DOWNLOAD_TEST_ENABLE
    return false;
#else
    if (FOTA_URL_DOWNLOAD_TEST_URL[0] == '\0') {
        return false;
    }
    if (s_busy) {
        ESP_LOGW(TAG, "URL test already running");
        return true;
    }

    nvs_connectivity_mode_t m = nvs_config_connectivity_mode();
    if (m == NVS_CONN_WIFI_ONLY && !wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "URL test: WiFi-only but STA down");
        return false;
    }
    if (m != NVS_CONN_WIFI_ONLY && !wifi_manager_is_connected()) {
        /* Modem path — allowed without WiFi */
        ESP_LOGI(TAG, "URL test: cellular HTTP (no WiFi)");
    }

    s_busy = true;
    ESP_LOGI(TAG, "Starting FOTA URL download test: %s", FOTA_URL_DOWNLOAD_TEST_URL);
    BaseType_t ok = xTaskCreate(url_test_task, "fota_url_test", 10240, NULL, 5, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate fota_url_test failed");
        s_busy = false;
        return false;
    }
    return true;
#endif
}
