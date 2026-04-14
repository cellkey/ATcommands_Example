/**
 * @file fota_url_download_test.h
 * @brief Optional FOTA invite handler: HTTP(S) download verification only (no flash / no reboot).
 *
 * When enabled and FOTA_URL_DOWNLOAD_TEST_URL non-empty: server "FOTA" runs a full GET (WiFi client or
 * modem AT+HTTP*), logs size + first-byte 0xE9; does not send START_FOTA or write OTA partition.
 */

#ifndef FOTA_URL_DOWNLOAD_TEST_H
#define FOTA_URL_DOWNLOAD_TEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True while the URL download test task is running (for fota_session_active / LED). */
bool fota_url_download_test_is_busy(void);

/**
 * If URL test mode is enabled and WiFi is up, starts download task and returns true (caller skips legacy FOTA).
 * If disabled, no URL, or no WiFi, returns false (caller uses modem stream FOTA).
 */
bool fota_url_download_test_try_handle_invite(void);

#ifdef __cplusplus
}
#endif

#endif /* FOTA_URL_DOWNLOAD_TEST_H */
