/**
 * @file fota_modem.h
 * @brief ESP-IDF OTA over modem TCP — unit/server FOTA protocol.
 *
 * Server invite: 4\\r\\nFOTA\\r\\n (length line + payload line; unit reacts to line "FOTA").
 * Unit sends: a\\r\\nSTART_FOTA\\r\\n (15 bytes; length line is single char "a", not decimal "10")
 *
 * Firmware stream (after START_FOTA), one of:
 *   - Sized: first line is file size in ASCII — 1–5 decimal digits or 1–8 hex digits (e.g. c9c00), with
 *     optional trim of \\r\\n/spaces. Next bytes on the socket are raw .bin for exactly that count
 *     (no 6+3 CS/type prefix).
 *   - Raw: first byte 0xE9 (ESP32 image magic), then binary until line FOTA_DONE (9 ASCII chars;
 *     optional \\r/\\n/space trim like the size line).
 *   - Raw fallback: first segment not all-digits and not 0xE9 — treat as start of .bin until FOTA_DONE.
 *
 * Sized path: after exactly "size" bytes written, unit validates and reboots (no FOTA_DONE).
 *
 * After successful boot: unit sends 7\\r\\nFOTA_OK\\r\\n once (NVS pending flag).
 * On failure: a\\r\\nFOTA_ERROR\\r\\n (15 bytes on the wire; FOTA_ERROR is 10 chars)
 */

#ifndef FOTA_MODEM_H
#define FOTA_MODEM_H

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void fota_mark_current_app_valid_if_needed(void);

esp_err_t fota_ota_begin(void);
esp_err_t fota_ota_write(const void *data, size_t len);
esp_err_t fota_ota_end_and_reboot(void);
void fota_ota_abort(void);

void fota_set_pending_success_notify(void);
void fota_try_send_pending_success_notify(void);

/** True while FOTA transfer is active (suppress competing KA traffic if desired). */
bool fota_session_active(void);

/** Sized FOTA: still receiving image body (modem may hold more bytes without a new URC). */
bool fota_sized_body_incomplete(void);

/** Bytes left in sized body (0 if not in FOTA_ST_BODY). */
uint32_t fota_body_bytes_remaining(void);

/**
 * Server sent FOTA invite (line "FOTA" after length line "4"). Starts OTA partition, sends START_FOTA.
 */
void fota_on_server_invite(void);

/**
 * One payload fragment from AT+CIPRXGET read (may contain binary; len = exact byte count).
 */
void fota_feed_modem_payload(const uint8_t *data, size_t len);

/** TCP link lost during FOTA — abort OTA session. */
void fota_on_tcp_disconnected(void);

#ifdef __cplusplus
}
#endif

#endif /* FOTA_MODEM_H */
