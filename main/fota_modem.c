/**
 * @file fota_modem.c
 * @brief OTA over modem: sized or raw stream + esp_ota_ops.
 *
 * Sized path: server may send file size as a text line (CRLF-terminated on the wire).
 * Size line: decimal ASCII digits (1–5) or hexadecimal (1–8, e.g. `c9c00`). Trim leading/trailing
 * spaces and line endings. Raw .bin follows immediately (no 9-byte CS/type prefix).
 */

#include <ctype.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"

/* First byte of valid ESP32 app image (esp_image_header_t). */
#define FOTA_ESP_IMAGE_MAGIC 0xE9

#include "at_command_api.h"
#include "server_keepalive.h"
#include "fota_modem.h"

static const char *TAG = "FOTA";

#define FOTA_NVS_NAMESPACE "fota"
#define FOTA_NVS_KEY_PENDING "pend_ok"

static const char START_FOTA_FRAME[] = "a\r\nSTART_FOTA\r\n"; /* 15 bytes: "a" length line + CRLF + START_FOTA + CRLF */
static const char FOTA_OK_FRAME[]    = "7\r\nFOTA_OK\r\n";
/* Wire length 15: 'a' + CRLF + "FOTA_ERROR"(10) + CRLF — strlen would be 15; sizeof-1 locks it at compile time. */
static const char FOTA_ERR_FRAME[]   = "a\r\nFOTA_ERROR\r\n";
_Static_assert(sizeof(FOTA_ERR_FRAME) - 1u == 15u, "FOTA_ERR_FRAME wire length must be 15");

static esp_ota_handle_t s_ota_handle;
static const esp_partition_t *s_update_partition;

typedef enum {
    FOTA_ST_IDLE = 0,
    FOTA_ST_RECV,       /* classifying start: size line, 0xE9, or raw */
    FOTA_ST_BODY,       /* after digit size: write exactly s_body_remaining bytes */
    FOTA_ST_RAW_BIN,    /* until FOTA_DONE */
} fota_session_st_t;

static fota_session_st_t s_sess = FOTA_ST_IDLE;
static uint32_t s_body_remaining;
/** Sized body: total bytes expected (for progress logs). */
static uint32_t s_fota_body_total;
static uint32_t s_fota_body_logged_done;

static void body_progress_note_rx(void)
{
    if (s_fota_body_total == 0 || s_sess != FOTA_ST_BODY) {
        return;
    }
    uint32_t done = s_fota_body_total - s_body_remaining;
    while (done >= s_fota_body_logged_done + 65536u) {
        s_fota_body_logged_done += 65536u;
        unsigned pct = (unsigned)(100u * done / s_fota_body_total);
        ESP_LOGI(TAG, "body rx %lu / %lu B (%u%%)", (unsigned long)done, (unsigned long)s_fota_body_total, pct);
    }
}

/* Forward declarations (static helpers call these before definitions). */
esp_err_t fota_ota_begin(void);
esp_err_t fota_ota_end_and_reboot(void);

static void send_err_frame(void)
{
    (void)send_at_then_raw_data(SERVER_TCP_LINK_ID, sizeof(FOTA_ERR_FRAME) - 1u, (const uint8_t *)FOTA_ERR_FRAME);
}

static bool all_digits(const uint8_t *p, size_t len)
{
    if (len < 1 || len > 5) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)p[i])) {
            return false;
        }
    }
    return true;
}

static uint32_t digits_to_u32(const uint8_t *p, size_t len)
{
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        v = v * 10u + (uint32_t)(p[i] - '0');
    }
    return v;
}

static bool all_hex_digits(const uint8_t *p, size_t len)
{
    if (len < 1 || len > 8) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)p[i])) {
            return false;
        }
    }
    return true;
}

static uint32_t hex_to_u32(const uint8_t *p, size_t len)
{
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        v *= 16u;
        if (c >= '0' && c <= '9') {
            v += (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v += 10u + (uint32_t)(c - 'a');
        } else {
            v += 10u + (uint32_t)(c - 'A');
        }
    }
    return v;
}

/** Trim leading/trailing whitespace and line endings; returns adjusted pointer and length (may be 0). */
static void trim_ascii_line(const uint8_t *data, size_t len, const uint8_t **out_ptr, size_t *out_len)
{
    size_t a = 0;
    size_t b = len;
    while (a < b && (data[a] == ' ' || data[a] == '\t')) {
        a++;
    }
    while (b > a && (data[b - 1] == '\r' || data[b - 1] == '\n' || data[b - 1] == ' ' || data[b - 1] == '\t')) {
        b--;
    }
    *out_ptr = data + a;
    *out_len = b - a;
}

static esp_err_t ota_write_chunk(const void *data, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }
    if (s_ota_handle == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t e = esp_ota_write(s_ota_handle, data, len);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(e));
    }
    return e;
}

static void session_reset(void)
{
    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }
    s_update_partition = NULL;
    s_sess = FOTA_ST_IDLE;
    s_body_remaining = 0;
    s_fota_body_total = 0;
    s_fota_body_logged_done = 0;
}

static void finalize_and_reboot(void)
{
    esp_err_t err = fota_ota_end_and_reboot();
    if (err != ESP_OK) {
        send_err_frame();
        session_reset();
    }
}

bool fota_session_active(void)
{
    return s_sess != FOTA_ST_IDLE;
}

bool fota_sized_body_incomplete(void)
{
    return s_sess == FOTA_ST_BODY && s_body_remaining > 0;
}

uint32_t fota_body_bytes_remaining(void)
{
    return (s_sess == FOTA_ST_BODY) ? s_body_remaining : 0u;
}

void fota_on_tcp_disconnected(void)
{
    if (!fota_session_active()) {
        return;
    }
    ESP_LOGW(TAG, "tcp drop");
    session_reset();
}

void fota_on_server_invite(void)
{
    if (fota_session_active()) {
        ESP_LOGW(TAG, "invite dup");
        return;
    }

    esp_err_t e = fota_ota_begin();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin fail");
        send_err_frame();
        return;
    }

    if (send_at_then_raw_data(SERVER_TCP_LINK_ID, strlen(START_FOTA_FRAME), (const uint8_t *)START_FOTA_FRAME) != AT_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "START_FOTA tx fail");
        send_err_frame();
        session_reset();
        return;
    }

    s_sess = FOTA_ST_RECV;
    s_body_remaining = 0;
    ESP_LOGI(TAG, "START_FOTA sent");
}

void fota_feed_modem_payload(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || s_sess == FOTA_ST_IDLE) {
        return;
    }

    /* Control line end of transfer (ASCII only). Server may send "FOTA_DONE\\r\\n" as one chunk. */
    {
        const uint8_t *ctrl;
        size_t ctrl_len;
        trim_ascii_line(data, len, &ctrl, &ctrl_len);
        if (ctrl_len == 9 && memcmp(ctrl, "FOTA_DONE", 9) == 0) {
            if (s_sess == FOTA_ST_RAW_BIN) {
                ESP_LOGI(TAG, "DONE, reboot");
                finalize_and_reboot();
            } else {
                ESP_LOGW(TAG, "DONE bad st %d", (int)s_sess);
                send_err_frame();
                session_reset();
            }
            return;
        }
    }

    switch (s_sess) {
    case FOTA_ST_RECV: {
        if (len >= 1 && data[0] == FOTA_ESP_IMAGE_MAGIC) {
            ESP_LOGI(TAG, "raw 0xE9");
            s_sess = FOTA_ST_RAW_BIN;
            if (ota_write_chunk(data, len) != ESP_OK) {
                send_err_frame();
                session_reset();
            }
            return;
        }
        const uint8_t *size_field;
        size_t size_len;
        trim_ascii_line(data, len, &size_field, &size_len);

        if (size_len == 0) {
            return;
        }
        /* Stray UART/URC fragments (e.g. lone "+") must not trigger raw-bin path. */
        if (size_field[0] == '+') {
            ESP_LOGD(TAG, "skip +' %uB", (unsigned)size_len);
            return;
        }

        {
            int log_chars = (size_len > 96) ? 96 : (int)size_len;
            ESP_LOGI(TAG, "size header \"%.*s\" (len=%u)", log_chars, (const char *)size_field,
                     (unsigned)size_len);
        }

        if (all_digits(size_field, size_len)) {
            uint32_t sz = digits_to_u32(size_field, size_len);
            if (sz == 0 || s_update_partition == NULL || sz > s_update_partition->size) {
                ESP_LOGE(TAG, "bad size %lu (max %lu)", (unsigned long)sz,
                         s_update_partition ? (unsigned long)s_update_partition->size : 0UL);
                send_err_frame();
                session_reset();
                return;
            }
            s_body_remaining = sz;
            s_sess = FOTA_ST_BODY;
            s_fota_body_total = sz;
            s_fota_body_logged_done = 0;
            ESP_LOGI(TAG, "body %lu dec", (unsigned long)sz);
            return;
        }
        if (all_hex_digits(size_field, size_len)) {
            uint32_t sz = hex_to_u32(size_field, size_len);
            if (sz == 0 || s_update_partition == NULL || sz > s_update_partition->size) {
                ESP_LOGE(TAG, "bad hex size %lu (max %lu)", (unsigned long)sz,
                         s_update_partition ? (unsigned long)s_update_partition->size : 0UL);
                send_err_frame();
                session_reset();
                return;
            }
            s_body_remaining = sz;
            s_sess = FOTA_ST_BODY;
            s_fota_body_total = sz;
            s_fota_body_logged_done = 0;
            ESP_LOGI(TAG, "body %lu hex", (unsigned long)sz);
            return;
        }
        /* Backward compatibility: no text header, treat as raw .bin */
        ESP_LOGI(TAG, "raw til DONE");
        s_sess = FOTA_ST_RAW_BIN;
        if (ota_write_chunk(data, len) != ESP_OK) {
            send_err_frame();
            session_reset();
        }
        return;
    }
    case FOTA_ST_BODY: {
        while (len > 0 && s_body_remaining > 0) {
            size_t n = len < s_body_remaining ? len : s_body_remaining;
            if (ota_write_chunk(data, n) != ESP_OK) {
                send_err_frame();
                session_reset();
                return;
            }
            s_body_remaining -= n;
            body_progress_note_rx();
            data += n;
            len -= n;
        }
        if (s_body_remaining == 0) {
            ESP_LOGI(TAG, "end, reboot");
            finalize_and_reboot();
            return;
        }
        if (len > 0) {
            ESP_LOGW(TAG, "tail %uB ignored", (unsigned)len);
        }
        return;
    }
    case FOTA_ST_RAW_BIN:
        if (ota_write_chunk(data, len) != ESP_OK) {
            send_err_frame();
            session_reset();
        }
        return;
    default:
        break;
    }
}

void fota_mark_current_app_valid_if_needed(void)
{
#if CONFIG_APP_ROLLBACK_ENABLE
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return;
    }
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
        if (e == ESP_OK) {
            ESP_LOGI(TAG, "OTA: app ok, rollback off");
        } else {
            ESP_LOGW(TAG, "esp_ota_mark_app_valid_cancel_rollback: %s", esp_err_to_name(e));
        }
    }
#endif
}

esp_err_t fota_ota_begin(void)
{
    if (s_ota_handle != 0) {
        ESP_LOGW(TAG, "OTA busy");
        return ESP_ERR_INVALID_STATE;
    }

    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (s_update_partition == NULL) {
        ESP_LOGE(TAG, "no OTA part");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_begin(s_update_partition, OTA_SIZE_UNKNOWN, &s_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_ota_handle = 0;
        s_update_partition = NULL;
    }
    return err;
}

esp_err_t fota_ota_write(const void *data, size_t len)
{
    if (s_ota_handle == 0 || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_ota_write(s_ota_handle, data, len);
}

void fota_ota_abort(void)
{
    session_reset();
}

esp_err_t fota_ota_end_and_reboot(void)
{
    if (s_ota_handle == 0 || s_update_partition == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_ota_end(s_ota_handle);
    s_ota_handle = 0;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        s_update_partition = NULL;
        s_sess = FOTA_ST_IDLE;
        return err;
    }

    err = esp_ota_set_boot_partition(s_update_partition);
    s_update_partition = NULL;
    s_sess = FOTA_ST_IDLE;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }

    fota_set_pending_success_notify();
    ESP_LOGI(TAG, "boot set, rst");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

void fota_set_pending_success_notify(void)
{
    nvs_handle_t h;
    if (nvs_open(FOTA_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(%s) failed", FOTA_NVS_NAMESPACE);
        return;
    }
    esp_err_t e = nvs_set_u8(h, FOTA_NVS_KEY_PENDING, 1);
    if (e == ESP_OK) {
        e = nvs_commit(h);
    }
    nvs_close(h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "NVS pending FOTA notify: %s", esp_err_to_name(e));
    }
}

void fota_try_send_pending_success_notify(void)
{
    nvs_handle_t h;
    if (nvs_open(FOTA_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    uint8_t v = 0;
    esp_err_t e = nvs_get_u8(h, FOTA_NVS_KEY_PENDING, &v);
    nvs_close(h);
    if (e != ESP_OK || v != 1) {
        return;
    }

    const size_t frame_len = strlen(FOTA_OK_FRAME);
    if (send_at_then_raw_data(SERVER_TCP_LINK_ID, frame_len, (const uint8_t *)FOTA_OK_FRAME) != AT_RESULT_SUCCESS) {
        ESP_LOGW(TAG, "FOTA_OK tx fail");
        return;
    }

    if (nvs_open(FOTA_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, FOTA_NVS_KEY_PENDING);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "FOTA_OK sent");
}
