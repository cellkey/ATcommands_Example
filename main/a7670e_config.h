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
#define A7670E_FW_VERSION     "3.0.01"

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

#endif /* A7670E_CONFIG_H */
