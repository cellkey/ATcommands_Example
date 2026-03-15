/**
 * @file config_uart.h
 * @brief Config shell for NVS updates: SET/GET/LIST/REBOOT. Uses console (same UART as debug/flash).
 */

#ifndef CONFIG_UART_H
#define CONFIG_UART_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start config shell task. Call after nvs_config_init().
 * Reads from console (stdin = same UART as idf.py monitor). No extra UART needed.
 * Commands (one per line): SET key=value, GET key, LIST, REBOOT.
 * Keys: unit_id, fw_ver, apn, host, port
 */
bool config_uart_start(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_UART_H */
