/**
 * @file a7670e_config.h
 * @brief A7670E server and device config for TCP GET registration
 */

#ifndef A7670E_CONFIG_H
#define A7670E_CONFIG_H

#include "driver/uart.h"  /* For UART_NUM_1 etc. */

/* Server (match reference log). Use hostname or IP if modem DNS fails. */
#define A7670E_SERVER_HOST    "gates.crea-cell.com"
#define A7670E_SERVER_PORT   3000
/* If connection fails with code 11, try IP: e.g. "178.62.86.70" (from your log) */

/* Unit ID: NVS is the single source for both BLE advertising name and modem GET request.
 * This default is used only when unit_id is not set in NVS. Format: crXXXXXXXX (10 chars). */
#define A7670E_UNIT_ID        "cr18061950"

/* FW version: compile-time constant used in GET request header (no NVS override). */
#define A7670E_FW_VERSION     "3.0.03"

/* Modem UART mapping (change pins to match your wiring).
 * NOTE: This board has relays on GPIO16/17, so we move the modem to UART1 on other GPIOs.
 * Wire ESP32 TX -> modem RX on A7670E_UART_TX_PIN, ESP32 RX -> modem TX on A7670E_UART_RX_PIN.
 */
#define A7670E_UART_NUM       UART_NUM_1
#define A7670E_UART_TX_PIN    4   /* example: change to free GPIO you use for TX */
#define A7670E_UART_RX_PIN    5   /* example: change to free GPIO you use for RX */

/* Modem power, status LED, and local button GPIOs. */
/* NOTE: Current hardware uses an active-LOW modem power switch: LOW = modem power enabled, HIGH = off. */
#define A7670E_MODEM_PWR_GPIO         12  /* Low = modem power enabled (active-low switch) */
#define A7670E_MODEM_STATUS_LED_GPIO  2   /* Active-high: ON during init, blink when connected */
#define A7670E_LOCAL_BUTTON_GPIO      15  /* Active-low external button with pull-up: press = LOW pulse */


/* Timeouts */
#define A7670E_CIPOPEN_TIMEOUT_MS    4500  /* wait for +CIPOPEN: 1,0 URC (modem sends OK then +CIPOPEN: 1,x) */
#define A7670E_CIPSEND_TIMEOUT_MS    1500
#define A7670E_WAIT_200_OK_MS        5000  /* wait for HTTP 200 OK (allow slow network); treat timeout as failure */

/* AT+CREG? retry: default 20 trials, 3 s between; tune based on experience (e.g. weak signal → increase). */
#define A7670E_CREG_MAX_TRIES        20
#define A7670E_CREG_RETRY_DELAY_SEC  2

/* --------------------------------------------------------------------------
 * FOTA URL download test (no flash): on server line "FOTA", HTTP GET full URL body, log size + 0xE9.
 * Does not send START_FOTA, no esp_ota_*.
 *   - WiFi connected → ESP32 esp_http_client (good TLS).
 *   - Else modem path (not WiFi-only) → SIMCOM AT+HTTP* on cellular (no WiFi needed).
 * Set your URL here when testing; leave empty string to use legacy stream FOTA only.
 *
 * CRC-32 (zlib / IEEE poly 0xEDB88320): set FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT to the value of the
 * exact .bin bytes (e.g. Python: hex(binascii.crc32(open('f.bin','rb').read()) & 0xffffffff)).
 * Use 0 to skip compare — firmware still logs computed CRC after download.
 * -------------------------------------------------------------------------- */
#define FOTA_URL_DOWNLOAD_TEST_ENABLE   1   /* 1 = on FOTA line, HTTP GET FOTA_URL_DOWNLOAD_TEST_URL (WiFi or modem) */
#define FOTA_URL_DOWNLOAD_TEST_URL      "https://litter.catbox.moe/x6145yml2v2kvhln.bin"
/* Verified for URL above: 826368 B, first byte 0xE9. Recompute if URL/file changes. 0 = skip CRC compare. */
#define FOTA_URL_DOWNLOAD_TEST_CRC32_EXPECT  0x23f025b7u

#endif /* A7670E_CONFIG_H */
